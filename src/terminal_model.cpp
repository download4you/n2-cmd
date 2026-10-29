#include "terminal_model.h"
#include <QUrl>
#include <algorithm>

namespace {
bool isDefaultTitle(const QString& title) {
    const QString low = title.toLower();
    if (low.startsWith(QStringLiteral("administrator:")) || low == QStringLiteral("administrator")) return true;
    if (low.contains(QStringLiteral("windows powershell")) || low.contains(QStringLiteral("command prompt"))) return true;
    if (low.contains(QStringLiteral("\\system32\\cmd.exe")) || low.contains(QStringLiteral("/system32/cmd.exe"))) return true;
    if (low.contains(QStringLiteral("cmd.exe - ")) || low.contains(QStringLiteral("powershell.exe - "))
        || low.contains(QStringLiteral("pwsh.exe - "))) return true;
    if (low.endsWith(QStringLiteral("powershell.exe")) || low.endsWith(QStringLiteral("pwsh.exe"))
        || low.endsWith(QStringLiteral("cmd.exe"))) return true;
    return low == QStringLiteral("powershell") || low == QStringLiteral("cmd") || low == QStringLiteral("conhost");
}

QString sessionTitle(const QString& value) {
    QString title = value;
    title.remove(QChar(0));
    if (isDefaultTitle(title)) return {};
    auto characters = title.toUcs4();
    if (characters.size() > 72) {
        characters.resize(71);
        characters.append(0x2026);
        title = QString::fromUcs4(reinterpret_cast<const char32_t*>(characters.constData()), characters.size());
    }
    return title;
}

QString cellsText(const TerminalRow& row, int first, int last, bool trim) {
    QString text;
    last = std::min(last, int(row.cells.size()));
    while (last > first && row.cells[last - 1].width != 0 && row.cells[last - 1].text.isEmpty()) --last;
    for (int column = std::max(0, first); column < std::min(last, int(row.cells.size())); ++column) {
        const auto& cell = row.cells[column];
        if (cell.width == 0) continue;
        text += cell.text.isEmpty() ? QStringLiteral(" ") : cell.text;
    }
    if (trim) while (text.endsWith(u' ')) text.chop(1);
    return text;
}
}

TerminalModel::TerminalModel(int columns, int rows, int history, QObject* parent)
    : QObject(parent), term_(vterm_new(std::max(1, rows), std::max(2, columns))),
      state_(vterm_obtain_state(term_)), screen_(vterm_obtain_screen(term_)), historyLimit_(std::max(1, history)) {
    vterm_set_utf8(term_, 1);
    static const VTermScreenCallbacks callbacks = {
        damageCallback, nullptr, cursorCallback, propertyCallback, bellCallback, nullptr,
        pushCallback, nullptr, clearCallback, pushReflowCallback
    };
    static const VTermStateFallbacks fallbacks = { nullptr, nullptr, oscCallback, nullptr, nullptr, nullptr, nullptr };
    vterm_screen_set_callbacks(screen_, &callbacks, this);
    vterm_screen_set_unrecognised_fallbacks(screen_, &fallbacks, this);
    vterm_output_set_callback(term_, outputCallback, this);
    vterm_screen_enable_altscreen(screen_, 1);
    vterm_screen_enable_reflow(screen_, true);
    vterm_screen_set_damage_merge(screen_, VTERM_DAMAGE_SCREEN);
    VTermColor foreground, background;
    vterm_color_rgb(&foreground, 230, 238, 251);
    vterm_color_rgb(&background, 7, 16, 26);
    vterm_screen_set_default_colors(screen_, &foreground, &background);
    const struct { int index, red, green, blue; } palette[] = {
        {0, 0x1a, 0x22, 0x30}, {1, 0xff, 0x6b, 0x7a}, {2, 0x5e, 0xe6, 0xa8}, {3, 0xff, 0xcf, 0x66},
        {4, 0x5a, 0xa9, 0xff}, {5, 0xd6, 0x8b, 0xff}, {6, 0x49, 0xc9, 0xe8}, {7, 0xd7, 0xe2, 0xf1},
        {8, 0x5c, 0x6b, 0x80}, {9, 0xff, 0x8a, 0x96}, {10, 0x7c, 0xf0, 0xbd}, {11, 0xff, 0xe0, 0x8a},
        {12, 0x7f, 0xc1, 0xff}, {13, 0xe3, 0xa8, 0xff}, {14, 0x7f, 0xe0, 0xf2}, {15, 0xff, 0xff, 0xff},
    };
    for (const auto& entry : palette) {
        VTermColor color;
        vterm_color_rgb(&color, entry.red, entry.green, entry.blue);
        vterm_state_set_palette_color(state_, entry.index, &color);
    }
    vterm_screen_reset(screen_, 1);
}

TerminalModel::~TerminalModel() { vterm_free(term_); }
int TerminalModel::columns() const { int value; vterm_get_size(term_, nullptr, &value); return value; }
int TerminalModel::screenRows() const { int value; vterm_get_size(term_, &value, nullptr); return value; }
int TerminalModel::historyRows() const { return alternate_ ? 0 : int(history_.size()); }
int TerminalModel::rowCount() const { return historyRows() + screenRows(); }

void TerminalModel::rememberLines() {
    beforeContinuation_.resize(screenRows());
    for (int row = 0; row < screenRows(); ++row)
        beforeContinuation_[row] = vterm_state_get_lineinfo(state_, row)->continuation;
    pushedInOperation_ = 0;
}

void TerminalModel::feed(const QByteArray& bytes) {
    // Preserve pre-scroll metadata: libvterm shifts lineinfo before its screen callback.
    for (char byte : bytes) {
        rememberLines();
        vterm_input_write(term_, &byte, 1);
    }
    vterm_screen_flush_damage(screen_);
    emit changed();
}

TerminalCell TerminalModel::convert(const VTermScreenCell& source) const {
    TerminalCell cell;
    if (source.chars[0] == UINT32_MAX) { cell.width = 0; return cell; }
    cell.width = std::max(1, int(source.width));
    for (uint32_t value : source.chars) {
        if (!value) break;
        if (value <= 0x10ffff) {
            const char32_t point = value;
            cell.text += QString::fromUcs4(&point, 1);
        }
    }
    VTermColor fg = source.fg, bg = source.bg;
    cell.defaultBackground = VTERM_COLOR_IS_DEFAULT_BG(&bg) && !source.attrs.reverse;
    vterm_screen_convert_color_to_rgb(screen_, &fg);
    vterm_screen_convert_color_to_rgb(screen_, &bg);
    cell.foreground = QColor(fg.rgb.red, fg.rgb.green, fg.rgb.blue);
    cell.background = QColor(bg.rgb.red, bg.rgb.green, bg.rgb.blue);
    if (source.attrs.reverse) std::swap(cell.foreground, cell.background);
    cell.bold = source.attrs.bold; cell.italic = source.attrs.italic;
    cell.underline = source.attrs.underline; cell.strike = source.attrs.strike;
    cell.blink = source.attrs.blink; cell.conceal = source.attrs.conceal;
    return cell;
}

TerminalRow TerminalModel::row(int index) const {
    if (index < 0 || index >= rowCount()) return {};
    if (index < historyRows()) return history_[size_t(index)];
    index -= historyRows();
    TerminalRow result;
    result.continuation = vterm_state_get_lineinfo(state_, index)->continuation;
    result.cells.reserve(columns());
    for (int column = 0; column < columns(); ++column) {
        VTermScreenCell cell{};
        vterm_screen_get_cell(screen_, { index, column }, &cell);
        result.cells.append(convert(cell));
    }
    return result;
}

QString TerminalModel::plainText() const {
    int last = rowCount() - 1;
    while (last >= 0 && cellsText(row(last), 0, row(last).cells.size(), true).isEmpty()) --last;
    if (last < 0) return {};
    return textRange({0, 0}, {int(row(last).cells.size()), last});
}

QString TerminalModel::textRange(QPoint start, QPoint end) const {
    if (start.y() > end.y() || (start.y() == end.y() && start.x() > end.x())) std::swap(start, end);
    QString result;
    for (int index = std::max(0, start.y()); index <= std::min(end.y(), rowCount() - 1); ++index) {
        const auto current = row(index);
        const bool wraps = index < rowCount() - 1 && row(index + 1).continuation;
        result += cellsText(current, index == start.y() ? start.x() : 0,
                           index == end.y() ? end.x() : current.cells.size(), !wraps);
        if (index != end.y() && !wraps) result += u'\n';
    }
    return result;
}

void TerminalModel::resize(int width, int height) {
    width = std::max(2, width); height = std::max(1, height);
    if (width == columns() && height == screenRows()) return;
    rememberLines();
    vterm_set_size(term_, height, width);
    vterm_screen_flush_damage(screen_);
    emit changed();
}
void TerminalModel::reset() { history_.clear(); vterm_screen_reset(screen_, 1); emit changed(); }
void TerminalModel::clearHistory() { history_.clear(); emit changed(); }
void TerminalModel::setHistoryLimit(int limit) {
    historyLimit_ = std::max(1, limit);
    while (int(history_.size()) > historyLimit_) history_.pop_front();
    emit changed();
}
QPoint TerminalModel::cursor() const { VTermPos pos; vterm_state_get_cursorpos(state_, &pos); return {pos.col, pos.row + historyRows()}; }
void TerminalModel::key(VTermKey key, VTermModifier modifiers) { vterm_keyboard_key(term_, key, modifiers); }
void TerminalModel::text(const QString& text, VTermModifier modifiers) { for (char32_t value : text.toUcs4()) vterm_keyboard_unichar(term_, value, modifiers); }
void TerminalModel::paste(const QString& text) { vterm_keyboard_start_paste(term_); emit responseReady(text.toUtf8()); vterm_keyboard_end_paste(term_); }
void TerminalModel::mouse(int row, int column, int button, bool pressed, VTermModifier modifiers) { vterm_mouse_move(term_, row, column, modifiers); vterm_mouse_button(term_, button, pressed, modifiers); }
void TerminalModel::mouseMove(int row, int column, VTermModifier modifiers) { vterm_mouse_move(term_, row, column, modifiers); }
void TerminalModel::focus(bool focused) { if (focused) vterm_state_focus_in(state_); else vterm_state_focus_out(state_); }
int TerminalModel::damageCallback(VTermRect, void*) { return 1; }
int TerminalModel::cursorCallback(VTermPos, VTermPos, int visible, void* user) { static_cast<TerminalModel*>(user)->cursorVisible_ = visible; return 1; }
int TerminalModel::propertyCallback(VTermProp prop, VTermValue* value, void* user) {
    auto* self = static_cast<TerminalModel*>(user);
    switch (prop) {
    case VTERM_PROP_ALTSCREEN: self->alternate_ = value->boolean; break;
    case VTERM_PROP_CURSORVISIBLE: self->cursorVisible_ = value->boolean; break;
    case VTERM_PROP_CURSORBLINK: self->cursorBlink_ = value->boolean; break;
    case VTERM_PROP_CURSORSHAPE: self->cursorShape_ = value->number; break;
    case VTERM_PROP_MOUSE: self->mouseMode_ = value->number; break;
    case VTERM_PROP_TITLE:
        if (value->string.initial) self->titleBuffer_.clear();
        self->titleBuffer_.append(value->string.str, value->string.len);
        if (value->string.final) {
            const QString title = sessionTitle(QString::fromUtf8(self->titleBuffer_));
            if (!title.isEmpty()) emit self->titleChanged(title);
        }
        break;
    default: break;
    }
    return 1;
}
int TerminalModel::bellCallback(void* user) { emit static_cast<TerminalModel*>(user)->bell(); return 1; }
int TerminalModel::pushCallback(int cols, const VTermScreenCell* cells, void* user) {
    auto* self = static_cast<TerminalModel*>(user);
    TerminalRow row;
    if (self->pushedInOperation_ < self->beforeContinuation_.size()) row.continuation = self->beforeContinuation_[self->pushedInOperation_];
    ++self->pushedInOperation_;
    for (int column = 0; column < cols; ++column) row.cells.append(self->convert(cells[column]));
    self->history_.push_back(std::move(row));
    while (int(self->history_.size()) > self->historyLimit_) self->history_.pop_front();
    return 1;
}
int TerminalModel::clearCallback(void* user) { static_cast<TerminalModel*>(user)->history_.clear(); return 1; }
int TerminalModel::pushReflowCallback(int cols, const VTermScreenCell* cells, int continuation, void* user) {
    auto* self = static_cast<TerminalModel*>(user);
    pushCallback(cols, cells, user);
    if (!self->history_.empty()) self->history_.back().continuation = continuation;
    return 1;
}
int TerminalModel::oscCallback(int command, VTermStringFragment value, void* user) {
    if (command != 7) return 0;
    auto* self = static_cast<TerminalModel*>(user);
    if (value.initial) self->directoryBuffer_.clear();
    self->directoryBuffer_.append(value.str, value.len);
    if (value.final) { const QUrl url(QString::fromUtf8(self->directoryBuffer_)); if (url.isLocalFile()) emit self->directoryChanged(url.toLocalFile()); }
    return 1;
}
void TerminalModel::outputCallback(const char* bytes, size_t size, void* user) { emit static_cast<TerminalModel*>(user)->responseReady(QByteArray(bytes, qsizetype(size))); }
