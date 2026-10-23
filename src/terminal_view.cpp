#include "terminal_view.h"
#include "conpty_session.h"
#include "settings_store.h"
#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDir>
#include <QFontMetricsF>
#include <QInputMethod>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QTextLayout>
#include <QTextOption>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace {
VTermModifier modifiers(Qt::KeyboardModifiers value) {
    int result = 0;
    if (value.testFlag(Qt::ShiftModifier)) result |= VTERM_MOD_SHIFT;
    if (value.testFlag(Qt::AltModifier)) result |= VTERM_MOD_ALT;
    if (value.testFlag(Qt::ControlModifier)) result |= VTERM_MOD_CTRL;
    return VTermModifier(result);
}
bool before(QPoint a, QPoint b) { return a.y() < b.y() || (a.y() == b.y() && a.x() < b.x()); }
}

struct TerminalView::RowLayout {
    struct Cell { int column, start, length, width, visual; qreal left, right; };
    struct PaintRun { QGlyphRun glyphs; QColor color; };
    std::shared_ptr<QTextLayout> paragraph;
    QTextLine line;
    QVector<Cell> cells;
    QVector<PaintRun> paintRuns;
};

TerminalView::TerminalView(SettingsStore* settings, ShellProfile profile, QString cwd, QWidget* parent, bool autoStart)
    : QWidget(parent), settings_(settings), profile_(std::move(profile)),
      directory_(cwd.isEmpty() ? QDir::homePath() : std::move(cwd)), model_(100, 30, settings->settings.scrollback, this) {
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_InputMethodEnabled);
    setAutoFillBackground(false);
    setMouseTracking(true);
    setCursor(Qt::IBeamCursor);
    setMinimumSize(360, 180);
    setAccessibleName(profile_.label + QStringLiteral(" terminal"));
    connect(&model_, &TerminalModel::changed, this, [this] { invalidateLayouts(); update(); });
    connect(&model_, &TerminalModel::titleChanged, this, &TerminalView::titleChanged);
    connect(&model_, &TerminalModel::directoryChanged, this, [this](const QString& value) { directory_ = value; });
    connect(&model_, &TerminalModel::responseReady, this, [this](const QByteArray& bytes) { lastWriteOk_ = session_ && running_ && session_->write(bytes) && lastWriteOk_; });
    connect(&model_, &TerminalModel::bell, this, [] { QApplication::beep(); });
    blinkTimer_.setInterval(560);
    connect(&blinkTimer_, &QTimer::timeout, this, [this] { blink_ = !blink_; if (hasFocus()) { invalidateLayouts(); update(); } });
    blinkTimer_.start();
    applySettings();
    if (autoStart) QTimer::singleShot(0, this, &TerminalView::startSession);
}
TerminalView::~TerminalView() { shutdown(); }
bool TerminalView::isRunning() const { return running_ && session_ && session_->isRunning(); }
QString TerminalView::currentDirectory() const { return directory_; }

void TerminalView::startSession() {
    if (shuttingDown_) return;
    session_ = new ConPtySession(this);
    connect(session_, &ConPtySession::outputReady, this, [this](const QByteArray& bytes) {
        const int history = model_.historyRows();
        model_.feed(bytes);
        if (scrollOffset_) scrollOffset_ = std::min(model_.historyRows(), scrollOffset_ + std::max(0, model_.historyRows() - history));
    });
    connect(session_, &ConPtySession::exited, this, [this](int code) {
        running_ = false;
        preedit_.clear();
        const QString suffix = code == 0 ? QString() : QStringLiteral(" (%1)").arg(code);
        const int history = model_.historyRows();
        model_.feed(QStringLiteral("\r\n\x1b[38;5;244m[session ended%1 - press Enter to restart]\x1b[0m\r\n").arg(suffix).toUtf8());
        if (scrollOffset_) scrollOffset_ = std::min(model_.historyRows(), scrollOffset_ + std::max(0, model_.historyRows() - history));
        showToast(tr("Session ended%1. Press Enter to restart").arg(suffix));
        emit stateChanged(false);
        update();
    });
    connect(session_, &ConPtySession::errorOccurred, this, [this](const QString& error) { showToast(error); });
    QString error;
    running_ = session_->start(profile_, directory_, model_.columns(), model_.screenRows(), &error);
    if (!running_) {
        model_.feed(QStringLiteral("\r\n\x1b[91mCould not start %1: %2\x1b[0m\r\n").arg(profile_.label, error).toUtf8());
        showToast(tr("Shell failed to start"));
    }
    emit stateChanged(running_);
}
void TerminalView::shutdown() {
    shuttingDown_ = true;
    blinkTimer_.stop();
    if (session_) { disconnect(session_, nullptr, this, nullptr); session_->stop(); delete session_; session_ = nullptr; }
    running_ = false;
}
void TerminalView::restart() {
    if (session_) { disconnect(session_, nullptr, this, nullptr); session_->stop(); delete session_; session_ = nullptr; }
    running_ = false; scrollOffset_ = 0; clearSelection(); preedit_.clear(); model_.reset();
    shuttingDown_ = false;
    startSession();
    if (running_) showToast(tr("Restarted %1").arg(profile_.label));
}
void TerminalView::applySettings() {
    const auto& settings = settings_->settings;
    font_ = QFont(settings.font_family);
    font_.setFamilies({ settings.font_family, QStringLiteral("Vazir Code Hack"), QStringLiteral("Cascadia Mono"), QStringLiteral("Segoe UI Emoji") });
    font_.setPointSizeF(settings.font_size);
    font_.setStyleHint(QFont::Monospace);
    QFontMetricsF metrics(font_);
    cellWidth_ = std::max(6.0, metrics.horizontalAdvance(u'M'));
    cellHeight_ = std::max(10.0, metrics.height() + 4.0);
    model_.setHistoryLimit(settings.scrollback);
    updateGrid(); invalidateLayouts(); update();
}
void TerminalView::updateGrid() {
    const int columns = std::max(2, int((width() - leftPadding_ - rightPadding_) / cellWidth_));
    const int rows = std::max(1, int((height() - topPadding_ - bottomPadding_) / cellHeight_));
    if (columns == model_.columns() && rows == model_.screenRows()) return;
    model_.resize(columns, rows);
    if (session_) session_->resize(columns, rows);
    scrollOffset_ = std::min(scrollOffset_, model_.historyRows());
    clearSelection();
    emit gridChanged(columns, rows);
}
void TerminalView::resizeEvent(QResizeEvent* event) { QWidget::resizeEvent(event); updateGrid(); }
void TerminalView::invalidateLayouts() { layoutDirty_ = true; }
int TerminalView::firstVisibleRow() const { return std::max(0, model_.historyRows() - scrollOffset_); }

void TerminalView::ensureLayouts() const {
    if (!layoutDirty_) return;
    layouts_.clear(); layouts_.resize(model_.rowCount());
    const int lastVisible = std::min(model_.rowCount(), firstVisibleRow() + model_.screenRows());
    for (int wanted = firstVisibleRow(); wanted < lastVisible; ++wanted) {
        if (layouts_[wanted]) continue;
        int first = wanted, last = wanted;
        while (first > 0 && model_.row(first).continuation) --first;
        while (last + 1 < model_.rowCount() && model_.row(last + 1).continuation) ++last;
        QString text;
        QVector<QTextLayout::FormatRange> formats;
        QVector<std::shared_ptr<RowLayout>> paragraphRows;
        for (int index = first; index <= last; ++index) {
            const auto row = model_.row(index);
            auto layout = std::make_shared<RowLayout>();
            for (int column = 0; column < row.cells.size(); ++column) {
                const auto& cell = row.cells[column];
                if (!cell.width) continue;
                const QString value = cell.text.isEmpty() ? QStringLiteral(" ") : cell.text;
                const int start = text.size();
                text += value;
                layout->cells.append({column, start, int(value.size()), cell.width, 0, 0, 0});
                QTextCharFormat format;
                format.setForeground(cell.conceal || (cell.blink && !blink_) ? Qt::transparent : cell.foreground);
                format.setFontWeight(cell.bold ? QFont::Bold : QFont::Normal);
                format.setFontItalic(cell.italic); format.setFontUnderline(cell.underline); format.setFontStrikeOut(cell.strike);
                if (!formats.isEmpty() && formats.last().start + formats.last().length == start && formats.last().format == format)
                    formats.last().length += value.size();
                else formats.append({start, int(value.size()), format});
            }
            if (index != last) text += QChar::LineSeparator;
            paragraphRows.append(layout);
        }
        auto paragraph = std::make_shared<QTextLayout>(text, font_);
        QTextOption option;
        option.setWrapMode(QTextOption::NoWrap);
        option.setTextDirection(Qt::LayoutDirectionAuto);
        option.setAlignment(Qt::AlignLeft);
        paragraph->setTextOption(option);
        paragraph->setFormats(formats);
        paragraph->beginLayout();
        for (auto& row : paragraphRows) {
            row->paragraph = paragraph;
            row->line = paragraph->createLine();
            if (!row->line.isValid()) continue;
            row->line.setLineWidth(10000000);
            row->line.setPosition({0, 0});
            for (auto& cell : row->cells) {
                qreal leading = row->line.cursorToX(cell.start, QTextLine::Leading);
                qreal trailing = row->line.cursorToX(cell.start, QTextLine::Trailing);
                cell.left = std::min(leading, trailing); cell.right = std::max(leading, trailing);
                // Cursor positions are ambiguous at bidi run boundaries; shaped ranges are not.
                if (cell.right - cell.left < 0.01) {
                    QRectF glyphBounds;
                    for (const auto& run : row->line.glyphRuns(cell.start, cell.length)) glyphBounds = glyphBounds.united(run.boundingRect());
                    if (glyphBounds.width() > 0.01) { cell.left = glyphBounds.left(); cell.right = glyphBounds.right(); }
                }
                if (cell.right - cell.left < 0.01) cell.right = cell.left + cellWidth_ * cell.width;
            }
            QVector<int> order;
            for (int index = 0; index < row->cells.size(); ++index) order.append(index);
            std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return row->cells[a].left < row->cells[b].left; });
            int visual = 0;
            for (int index : order) { row->cells[index].visual = visual; visual += row->cells[index].width; }
        }
        paragraph->endLayout();
        for (int index = first; index <= last; ++index) {
            auto& row = paragraphRows[index - first];
            const auto source = model_.row(index);
            for (const auto& shaped : row->line.glyphRuns(-1, -1, QTextLayout::RetrieveAll)) {
                const auto glyphs = shaped.glyphIndexes();
                const auto positions = shaped.positions();
                const auto indexes = shaped.stringIndexes();
                QList<quint32> groupGlyphs;
                QList<QPointF> groupPositions;
                QColor groupColor;
                auto flush = [&] {
                    if (groupGlyphs.isEmpty()) return;
                    QGlyphRun run;
                    run.setRawFont(shaped.rawFont());
                    run.setRightToLeft(shaped.isRightToLeft());
                    run.setGlyphIndexes(groupGlyphs); run.setPositions(groupPositions);
                    row->paintRuns.append({run, groupColor});
                    groupGlyphs.clear(); groupPositions.clear();
                };
                for (int glyph = 0; glyph < glyphs.size() && glyph < indexes.size(); ++glyph) {
                    auto cell = std::upper_bound(row->cells.begin(), row->cells.end(), indexes[glyph], [](qsizetype position, const RowLayout::Cell& item) { return position < item.start; });
                    if (cell == row->cells.begin()) continue;
                    --cell;
                    if (indexes[glyph] >= cell->start + cell->length) continue;
                    const auto& style = source.cells[cell->column];
                    const QColor color = style.conceal || (style.blink && !blink_) ? QColor(Qt::transparent) : style.foreground;
                    if (!groupGlyphs.isEmpty() && color != groupColor) flush();
                    groupColor = color;
                    QPointF position = positions[glyph];
                    position.rx() += cell->visual * cellWidth_ - cell->left;
                    groupGlyphs.append(glyphs[glyph]); groupPositions.append(position);
                }
                flush();
            }
            layouts_[index] = row;
        }
    }
    layoutDirty_ = false;
}

QRectF TerminalView::cellRect(int row, int column) const {
    ensureLayouts();
    const qreal top = topPadding_ + (row - firstVisibleRow()) * cellHeight_;
    if (row >= 0 && row < layouts_.size() && layouts_[row]) {
        for (const auto& cell : layouts_[row]->cells)
            if (column >= cell.column && column < cell.column + cell.width)
                return {leftPadding_ + cell.visual * cellWidth_, top, cell.width * cellWidth_, cellHeight_};
    }
    return {leftPadding_ + column * cellWidth_, top, cellWidth_, cellHeight_};
}
QPoint TerminalView::cellAt(const QPointF& position) const {
    ensureLayouts();
    const int row = std::clamp(int((position.y() - topPadding_) / cellHeight_) + firstVisibleRow(), firstVisibleRow(),
                               std::min(model_.rowCount() - 1, firstVisibleRow() + model_.screenRows() - 1));
    const int visual = std::clamp(int((position.x() - leftPadding_) / cellWidth_), 0, model_.columns() - 1);
    if (row < layouts_.size() && layouts_[row]) for (const auto& cell : layouts_[row]->cells)
        if (visual >= cell.visual && visual < cell.visual + cell.width) return {cell.column, row};
    return {visual, row};
}
bool TerminalView::selected(int row, int column) const {
    if (anchor_.y() < 0 || focus_.y() < 0) return false;
    if (visualSelection_) column = int(std::round((cellRect(row, column).left() - leftPadding_) / cellWidth_));
    if (rectangular_) return row >= std::min(anchor_.y(), focus_.y()) && row <= std::max(anchor_.y(), focus_.y())
        && column >= std::min(anchor_.x(), focus_.x()) && column < std::max(anchor_.x(), focus_.x()) + (visualSelection_ ? 1 : 0);
    const auto start = before(anchor_, focus_) ? anchor_ : focus_;
    auto end = before(anchor_, focus_) ? focus_ : anchor_;
    if (visualSelection_) ++end.rx();
    return !before({column, row}, start) && before({column, row}, end);
}
QString TerminalView::selectedText() const {
    if (anchor_.y() < 0 || focus_.y() < 0) return {};
    if (visualSelection_) {
        QString text;
        const int last = std::min(model_.rowCount() - 1, std::max(anchor_.y(), focus_.y()));
        for (int index = std::max(0, std::min(anchor_.y(), focus_.y())); index <= last; ++index) {
            const auto row = model_.row(index);
            QString part;
            for (int column = 0; column < row.cells.size(); ++column) if (row.cells[column].width && selected(index, column))
                part += row.cells[column].text.isEmpty() ? QStringLiteral(" ") : row.cells[column].text;
            const bool wraps = !rectangular_ && index < last && model_.row(index + 1).continuation;
            if (!wraps) while (part.endsWith(u' ')) part.chop(1);
            text += part;
            if (index < last && !wraps) text += u'\n';
        }
        return text;
    }
    if (!rectangular_) return model_.textRange(anchor_, focus_);
    QStringList lines;
    for (int row = std::min(anchor_.y(), focus_.y()); row <= std::max(anchor_.y(), focus_.y()); ++row)
        lines.append(model_.textRange({std::min(anchor_.x(), focus_.x()), row}, {std::max(anchor_.x(), focus_.x()), row}));
    return lines.join(u'\n');
}

void TerminalView::paintEvent(QPaintEvent*) {
    ensureLayouts();
    QPainter painter(this);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.setRenderHint(QPainter::Antialiasing);
    QColor tint(5, 9, 16); tint.setAlphaF(settings_->settings.terminal_tint); painter.fillRect(rect(), tint);
    QColor glow(settings_->settings.accent); glow.setAlpha(38);
    painter.fillRect(QRectF(0, 0, width(), 1.4), glow);
    const QRectF textRect(leftPadding_, topPadding_, model_.columns() * cellWidth_, model_.screenRows() * cellHeight_);
    painter.save(); painter.setClipRect(textRect);
    for (int index = firstVisibleRow(); index < std::min(model_.rowCount(), firstVisibleRow() + model_.screenRows()); ++index) {
        const auto row = model_.row(index);
        const auto& layout = layouts_[index];
        if (!layout || !layout->line.isValid()) continue;
        for (const auto& cell : layout->cells) {
            const auto target = cellRect(index, cell.column);
            const auto& data = row.cells[cell.column];
            if (!data.defaultBackground) painter.fillRect(target, data.background);
            if (selected(index, cell.column)) { QColor color(settings_->settings.accent); color.setAlpha(hasFocus() ? 78 : 48); painter.fillRect(target, color); }
        }
        const QPointF origin(leftPadding_, topPadding_ + (index - firstVisibleRow()) * cellHeight_ + (cellHeight_ - layout->line.height()) / 2);
        for (const auto& run : layout->paintRuns) { painter.setPen(run.color); painter.drawGlyphRun(origin, run.glyphs); }
        for (const auto& cell : layout->cells) {
            const auto& data = row.cells[cell.column];
            if (data.conceal || (data.blink && !blink_)) continue;
            const auto target = cellRect(index, cell.column);
            painter.setPen(data.foreground);
            if (data.underline) painter.drawLine(QPointF(target.left(), target.bottom() - 2), QPointF(target.right(), target.bottom() - 2));
            if (data.strike) painter.drawLine(QPointF(target.left(), target.center().y()), QPointF(target.right(), target.center().y()));
        }
    }
    if (hasFocus() && scrollOffset_ == 0 && model_.cursorVisible() && (blink_ || !model_.cursorBlink())) {
        const QPoint cursor = model_.cursor();
        const QRectF target = cellRect(cursor.y(), cursor.x());
        QColor color(settings_->settings.accent);
        painter.setPen(QPen(color, 1.4));
        if (model_.cursorShape() == VTERM_PROP_CURSORSHAPE_UNDERLINE) painter.drawLine(target.bottomLeft(), target.bottomRight());
        else if (model_.cursorShape() == VTERM_PROP_CURSORSHAPE_BAR_LEFT) painter.drawLine(target.topLeft(), target.bottomLeft());
        else { color.setAlpha(45); painter.fillRect(target, color); painter.drawRect(target.adjusted(0.5, 0.5, -0.5, -0.5)); }
        if (!preedit_.isEmpty()) { painter.setFont(font_); painter.setPen(QColor(settings_->settings.accent)); painter.drawText(target.bottomLeft(), preedit_); }
    }
    painter.restore();
    if (hasFocus()) {
        QColor edge(settings_->settings.accent); edge.setAlpha(18);
        painter.setPen(QPen(edge, 1));
        painter.drawLine(0, height() - 1, width(), height() - 1);
    }
    if (model_.historyRows()) {
        const qreal track = model_.screenRows() * cellHeight_;
        const qreal thumb = std::max(24.0, track * model_.screenRows() / model_.rowCount());
        const qreal top = topPadding_ + (track - thumb) * (1.0 - double(scrollOffset_) / model_.historyRows());
        painter.fillRect(QRectF(width() - 9, top, 4, thumb), QColor(settings_->settings.accent));
    }
    painter.setFont(font_);
    if (!toast_.isEmpty()) {
        const qint64 remaining = 1500 - (toastClock_.isValid() ? toastClock_.elapsed() : 0);
        const int alpha = remaining > 250 ? 235 : int(235.0 * std::max<qint64>(0, remaining) / 250.0);
        const qreal boxWidth = std::min(width() - 24.0, QFontMetricsF(font_).horizontalAdvance(toast_) + 26);
        const QRectF box(width() - 12 - boxWidth, height() - 43, boxWidth, 28);
        painter.setPen(QPen(QColor(255, 255, 255, 34), 1));
        painter.setBrush(QColor(11, 17, 27, alpha));
        painter.drawRoundedRect(box, 9, 9);
        painter.setPen(QColor(214, 229, 244, alpha));
        painter.drawText(box, Qt::AlignCenter, toast_);
    }
    if (!running_ && session_) {
        const QString message = tr("Session ended - press Enter to restart");
        const qreal boxWidth = std::min(width() - 24.0, QFontMetricsF(font_).horizontalAdvance(message) + 30);
        const QRectF box(12, height() - 43, boxWidth, 28);
        painter.setPen(QPen(QColor(255, 122, 135, 80), 1));
        painter.setBrush(QColor(31, 18, 20, 232));
        painter.drawRoundedRect(box, 9, 9);
        painter.setPen(QColor(QStringLiteral("#ff9aa4")));
        painter.drawText(box, Qt::AlignCenter, message);
    }
}

void TerminalView::clearSelection() { anchor_ = focus_ = {-1, -1}; rectangular_ = visualSelection_ = false; update(); }
void TerminalView::selectAll() { anchor_ = {0, 0}; focus_ = {model_.columns(), model_.rowCount() - 1}; rectangular_ = visualSelection_ = false; update(); }
bool TerminalView::copySelection(bool clear) {
    const QString text = selectedText(); if (text.isEmpty()) return false;
    QApplication::clipboard()->setText(text); showToast(tr("Copied %1 characters").arg(text.size()));
    if (clear) clearSelection(); return true;
}
bool TerminalView::pasteClipboard() {
    if (!isRunning()) { showToast(tr("Session has ended")); return false; }
    QString value = QApplication::clipboard()->text(); if (value.isEmpty()) return false;
    value.replace(QStringLiteral("\r\n"), QStringLiteral("\n")); value.replace(u'\r', u'\n'); value.replace(u'\n', u'\r'); value.remove(QChar(0));
    lastWriteOk_ = true; model_.paste(value);
    if (!lastWriteOk_) return false;
    showToast(tr("Pasted"));
    scrollOffset_ = 0; blink_ = true; invalidateLayouts(); update(); return true;
}
bool TerminalView::runCommand(const QString& command) {
    if (!isRunning()) return false;
    QString value = command;
    while (value.endsWith(u'\r') || value.endsWith(u'\n')) value.chop(1);
    while (value.startsWith(u'\r') || value.startsWith(u'\n')) value.remove(0, 1);
    if (value.isEmpty()) return false;
    value.replace(QStringLiteral("\r\n"), QStringLiteral("\n")); value.replace(u'\r', u'\n'); value.replace(u'\n', u'\r');
    if (!session_->write((value + u'\r').toUtf8())) return false;
    clearSelection(); scrollOffset_ = 0; setFocus(); return true;
}
void TerminalView::clearScrollback() { model_.clearHistory(); model_.feed(QByteArrayLiteral("\x1b[2J\x1b[H")); scrollOffset_ = 0; clearSelection(); }
void TerminalView::showToast(const QString& value) {
    toast_ = value;
    toastClock_.restart();
    update();
    QTimer::singleShot(1500, this, [this, value] { if (toast_ == value) { toast_.clear(); update(); } });
}
void TerminalView::scrollBy(int rows) { scrollOffset_ = std::clamp(scrollOffset_ + rows, 0, model_.historyRows()); invalidateLayouts(); update(); }

void TerminalView::sendRaw(const QByteArray& bytes) {
    if (!isRunning() || bytes.isEmpty()) return;
    lastWriteOk_ = session_->write(bytes);
    scrollOffset_ = 0; blink_ = true; invalidateLayouts(); update();
}

void TerminalView::sendKey(QKeyEvent* event) {
    const auto mods = modifiers(event->modifiers());
    VTermKey key = VTERM_KEY_NONE;
    switch (event->key()) {
    case Qt::Key_Return: case Qt::Key_Enter: key = VTERM_KEY_ENTER; break;
    case Qt::Key_Tab: case Qt::Key_Backtab: key = VTERM_KEY_TAB; break;
    case Qt::Key_Backspace: key = VTERM_KEY_BACKSPACE; break;
    case Qt::Key_Escape: key = VTERM_KEY_ESCAPE; break;
    case Qt::Key_Up: key = VTERM_KEY_UP; break; case Qt::Key_Down: key = VTERM_KEY_DOWN; break;
    case Qt::Key_Left: key = VTERM_KEY_LEFT; break; case Qt::Key_Right: key = VTERM_KEY_RIGHT; break;
    case Qt::Key_Insert: key = VTERM_KEY_INS; break; case Qt::Key_Delete: key = VTERM_KEY_DEL; break;
    case Qt::Key_Home: key = VTERM_KEY_HOME; break; case Qt::Key_End: key = VTERM_KEY_END; break;
    case Qt::Key_PageUp: key = VTERM_KEY_PAGEUP; break; case Qt::Key_PageDown: key = VTERM_KEY_PAGEDOWN; break;
    default: if (event->key() >= Qt::Key_F1 && event->key() <= Qt::Key_F35) key = VTermKey(VTERM_KEY_FUNCTION(event->key() - Qt::Key_F1 + 1)); break;
    }
    if (key != VTERM_KEY_NONE) model_.key(key, mods);
    else if (event->modifiers().testFlag(Qt::ControlModifier) && !event->modifiers().testFlag(Qt::AltModifier)
             && event->key() >= Qt::Key_A && event->key() <= Qt::Key_Z)
        model_.text(QString(QChar(event->key()).toLower()), mods);
    else if (!event->text().isEmpty()) model_.text(event->text(), VTermModifier(mods & ~VTERM_MOD_SHIFT));
    scrollOffset_ = 0; blink_ = true; invalidateLayouts(); update();
}
void TerminalView::keyPressEvent(QKeyEvent* event) {
    const auto mods = event->modifiers(); const int key = event->key();
    const bool ctrl = mods.testFlag(Qt::ControlModifier), shift = mods.testFlag(Qt::ShiftModifier), alt = mods.testFlag(Qt::AltModifier);
    if (!running_ && (key == Qt::Key_Return || key == Qt::Key_Enter)) { restart(); event->accept(); return; }
    if (key == Qt::Key_Escape && !selectedText().isEmpty()) { clearSelection(); event->accept(); return; }
    if (ctrl && !alt && key == Qt::Key_C && (shift || !selectedText().isEmpty())) { copySelection(); event->accept(); return; }
    if (ctrl && !alt && key == Qt::Key_X && !selectedText().isEmpty()) { copySelection(true); event->accept(); return; }
    if ((ctrl && !alt && key == Qt::Key_V) || (shift && !ctrl && key == Qt::Key_Insert)) { pasteClipboard(); event->accept(); return; }
    if (ctrl && key == Qt::Key_Insert) { copySelection(); event->accept(); return; }
    if (ctrl && shift && key == Qt::Key_A) { selectAll(); event->accept(); return; }
    if (ctrl && (key == Qt::Key_Plus || key == Qt::Key_Equal || key == Qt::Key_Minus || key == Qt::Key_0)) { emit zoomRequested(key == Qt::Key_0 ? 0 : key == Qt::Key_Minus ? -1 : 1); event->accept(); return; }
    if (ctrl && shift && key == Qt::Key_T) { emit newTabRequested(); event->accept(); return; }
    if (ctrl && shift && key == Qt::Key_W) { emit closeTabRequested(); event->accept(); return; }
    if (ctrl && key == Qt::Key_Comma) { emit settingsRequested(); event->accept(); return; }
    if (shift && (key == Qt::Key_PageUp || key == Qt::Key_PageDown)) { scrollBy((key == Qt::Key_PageUp ? 1 : -1) * (model_.screenRows() - 1)); event->accept(); return; }
    if (ctrl && shift && (key == Qt::Key_Home || key == Qt::Key_End)) { scrollBy(key == Qt::Key_Home ? model_.historyRows() : -model_.historyRows()); event->accept(); return; }
    if (key == Qt::Key_Backspace) {
        sendRaw(QByteArray(1, ctrl ? '\x17' : shift ? '\x08' : '\x7f'));
        event->accept(); return;
    }
    if (ctrl && !alt) {
        if (key == Qt::Key_Space) { sendRaw(QByteArray(1, '\0')); event->accept(); return; }
        if (key == Qt::Key_BracketLeft) { sendRaw(QByteArray(1, '\x1b')); event->accept(); return; }
        if (key == Qt::Key_Backslash) { sendRaw(QByteArray(1, '\x1c')); event->accept(); return; }
        if (key == Qt::Key_BracketRight) { sendRaw(QByteArray(1, '\x1d')); event->accept(); return; }
        if (key == Qt::Key_AsciiCircum) { sendRaw(QByteArray(1, '\x1e')); event->accept(); return; }
        if (key == Qt::Key_Underscore) { sendRaw(QByteArray(1, '\x1f')); event->accept(); return; }
        if (key == Qt::Key_I || key == Qt::Key_J || key == Qt::Key_M) {
            sendRaw(QByteArray(1, char(key - Qt::Key_A + 1))); event->accept(); return;
        }
    }
    sendKey(event); event->accept();
}

void TerminalView::mousePressEvent(QMouseEvent* event) {
    setFocus();
    const auto cell = cellAt(event->position());
    if (model_.mouseReporting() && !event->modifiers().testFlag(Qt::ShiftModifier)) {
        const int button = event->button() == Qt::LeftButton ? 1 : event->button() == Qt::MiddleButton ? 2 : 3;
        model_.mouse(cell.y() - model_.historyRows(), cell.x(), button, true, modifiers(event->modifiers())); event->accept(); return;
    }
    if (event->button() == Qt::MiddleButton) { pasteClipboard(); event->accept(); return; }
    if (event->button() == Qt::LeftButton) {
        if (clickTimer_.isValid() && clickTimer_.elapsed() < QApplication::doubleClickInterval()
            && (event->position().toPoint() - lastClickPosition_).manhattanLength() < QApplication::startDragDistance()) ++clickCount_;
        else clickCount_ = 1;
        clickTimer_.restart(); lastClickPosition_ = event->position().toPoint();
        if (clickCount_ >= 3) { selectLine(cell); dragging_ = false; if (settings_->settings.copy_on_select) copySelection(); event->accept(); return; }
        const QPoint visual(std::clamp(int((event->position().x() - leftPadding_) / cellWidth_), 0, model_.columns() - 1), cell.y());
        if (!event->modifiers().testFlag(Qt::ShiftModifier) || anchor_.y() < 0) { anchor_ = visual; focus_ = {-1, -1}; }
        else focus_ = visual;
        visualSelection_ = true; dragging_ = true; rectangular_ = event->modifiers().testFlag(Qt::AltModifier);
        update(); event->accept(); return;
    }
    QWidget::mousePressEvent(event);
}
void TerminalView::mouseMoveEvent(QMouseEvent* event) {
    const auto cell = cellAt(event->position());
    if (model_.mouseReporting() && !event->modifiers().testFlag(Qt::ShiftModifier)) {
        model_.mouseMove(cell.y() - model_.historyRows(), cell.x(), modifiers(event->modifiers())); event->accept(); return;
    }
    if (dragging_) {
        if (event->position().y() < topPadding_) scrollBy(1);
        if (event->position().y() > height() - bottomPadding_) scrollBy(-1);
        focus_ = {std::clamp(int((event->position().x() - leftPadding_) / cellWidth_), 0, model_.columns() - 1), cell.y()};
        update(); event->accept(); return;
    }
    QWidget::mouseMoveEvent(event);
}
void TerminalView::mouseReleaseEvent(QMouseEvent* event) {
    if (model_.mouseReporting() && !event->modifiers().testFlag(Qt::ShiftModifier)) {
        const auto cell = cellAt(event->position()); const int button = event->button() == Qt::LeftButton ? 1 : event->button() == Qt::MiddleButton ? 2 : 3;
        model_.mouse(cell.y() - model_.historyRows(), cell.x(), button, false, modifiers(event->modifiers())); event->accept(); return;
    }
    if (event->button() == Qt::LeftButton && dragging_) { dragging_ = false; if (settings_->settings.copy_on_select) copySelection(); event->accept(); return; }
    QWidget::mouseReleaseEvent(event);
}
void TerminalView::selectWord(QPoint point) {
    visualSelection_ = rectangular_ = false;
    const auto row = model_.row(point.y());
    auto word = [&](int column) { const auto text = row.cells.value(column).text; return !text.isEmpty() && (text.front().isLetterOrNumber() || QStringLiteral("_-./\\:@~#%+=,;\"'").contains(text.front())); };
    int first = point.x(), last = point.x() + std::max(1, row.cells.value(point.x()).width);
    if (word(first)) { while (first > 0 && word(first - 1)) --first; while (last < row.cells.size() && word(last)) ++last; }
    anchor_ = {first, point.y()}; focus_ = {last, point.y()}; update();
}
void TerminalView::selectLine(QPoint point) { visualSelection_ = rectangular_ = false; anchor_ = {0, point.y()}; focus_ = {model_.columns(), point.y()}; update(); }
void TerminalView::mouseDoubleClickEvent(QMouseEvent* event) { if (event->button() == Qt::LeftButton) { clickCount_ = 2; clickTimer_.restart(); lastClickPosition_ = event->position().toPoint(); selectWord(cellAt(event->position())); dragging_ = false; if (settings_->settings.copy_on_select) copySelection(); event->accept(); } else QWidget::mouseDoubleClickEvent(event); }
void TerminalView::wheelEvent(QWheelEvent* event) {
    const int steps = event->angleDelta().y() ? event->angleDelta().y() / 120 : event->pixelDelta().y() / 24;
    if (event->modifiers().testFlag(Qt::ControlModifier)) emit zoomRequested(steps >= 0 ? 1 : -1);
    else if (model_.mouseReporting() && !event->modifiers().testFlag(Qt::ShiftModifier)) {
        const auto cell = cellAt(event->position()); for (int i = 0; i < std::abs(steps); ++i) model_.mouse(cell.y() - model_.historyRows(), cell.x(), steps > 0 ? 4 : 5, true, modifiers(event->modifiers()));
    } else scrollBy(steps * 3);
    event->accept();
}
void TerminalView::contextMenuEvent(QContextMenuEvent* event) {
    QMenu menu(this);
    menu.setStyleSheet(QStringLiteral(
        "QMenu{background:#0d1420;color:#e6eefb;border:1px solid #344256;border-radius:8px;padding:6px;font:10pt 'Segoe UI';}"
        "QMenu::item{padding:7px 26px 7px 22px;border-radius:4px;} QMenu::item:selected{background:%1;color:#05121a;}"
        "QMenu::item:disabled{color:#77869b;} QMenu::separator{height:1px;background:#304053;margin:5px 8px;}").arg(settings_->settings.accent));
    auto* copy = menu.addAction(tr("Copy    Ctrl+C"), this, [this] { copySelection(); }); copy->setEnabled(!selectedText().isEmpty());
    auto* paste = menu.addAction(tr("Paste    Ctrl+V"), this, [this] { pasteClipboard(); }); paste->setEnabled(isRunning());
    menu.addAction(tr("Select All"), this, &TerminalView::selectAll);
    menu.addAction(tr("Clear screen and scrollback"), this, &TerminalView::clearScrollback);
    menu.addAction(tr("Restart Shell"), this, &TerminalView::restart); menu.addSeparator();
    menu.addAction(tr("New Tab    Ctrl+Shift+T"), this, &TerminalView::newTabRequested);
    menu.addAction(tr("Close Tab    Ctrl+Shift+W"), this, &TerminalView::closeTabRequested); menu.addSeparator();
    menu.addAction(tr("Settings    Ctrl+,"), this, &TerminalView::settingsRequested); menu.exec(event->globalPos());
}
void TerminalView::inputMethodEvent(QInputMethodEvent* event) { preedit_ = event->preeditString(); if (!event->commitString().isEmpty()) model_.text(event->commitString()); update(); event->accept(); }
QVariant TerminalView::inputMethodQuery(Qt::InputMethodQuery query) const {
    if (query == Qt::ImCursorRectangle) { const auto cursor = model_.cursor(); return cellRect(cursor.y(), cursor.x()).toRect(); }
    if (query == Qt::ImEnabled) return true;
    if (query == Qt::ImFont) return font_;
    return QWidget::inputMethodQuery(query);
}
void TerminalView::focusInEvent(QFocusEvent* event) { blink_ = true; model_.focus(true); update(); QWidget::focusInEvent(event); }
void TerminalView::focusOutEvent(QFocusEvent* event) { model_.focus(false); update(); QWidget::focusOutEvent(event); }
