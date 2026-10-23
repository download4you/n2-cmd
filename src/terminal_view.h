#pragma once
#include "app_types.h"
#include "terminal_model.h"
#include <QWidget>
#include <QTimer>
#include <QFont>
#include <QElapsedTimer>
#include <memory>

class SettingsStore;
class ConPtySession;
class QPainter;
class QTextLayout;

class TerminalView : public QWidget {
    Q_OBJECT
public:
    TerminalView(SettingsStore* settings, ShellProfile profile, QString cwd = {}, QWidget* parent = nullptr, bool autoStart = true);
    ~TerminalView() override;
    const ShellProfile& profile() const { return profile_; }
    void applySettings();
    bool isRunning() const;
    QString selectedText() const;
    QString plainText() const { return model_.plainText(); }
    bool runCommand(const QString& command);
    bool copySelection(bool clear = false);
    bool pasteClipboard();
    void selectAll();
    void clearSelection();
    void clearScrollback();
    void restart();
    void shutdown();
    QString currentDirectory() const;
    void feedForTest(const QByteArray& bytes) { model_.feed(bytes); }
    TerminalModel& model() { return model_; }
    QRectF cellRect(int row, int column) const;
    QPoint cellAt(const QPointF& position) const;
signals:
    void titleChanged(const QString& title);
    void gridChanged(int columns, int rows);
    void stateChanged(bool running);
    void zoomRequested(int delta);
    void newTabRequested();
    void closeTabRequested();
    void settingsRequested();
protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void contextMenuEvent(QContextMenuEvent*) override;
    void inputMethodEvent(QInputMethodEvent*) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery) const override;
    void focusInEvent(QFocusEvent*) override;
    void focusOutEvent(QFocusEvent*) override;
private:
    struct RowLayout;
    SettingsStore* settings_;
    ShellProfile profile_;
    QString directory_;
    TerminalModel model_;
    ConPtySession* session_ = nullptr;
    QFont font_;
    qreal cellWidth_ = 10, cellHeight_ = 22;
    int scrollOffset_ = 0;
    QPoint anchor_{-1, -1}, focus_{-1, -1};
    bool dragging_ = false, rectangular_ = false, visualSelection_ = false, running_ = false, shuttingDown_ = false, blink_ = true;
    bool scrollbarDragging_ = false;
    qreal scrollbarGrabOffset_ = 0;
    bool lastWriteOk_ = true;
    QElapsedTimer clickTimer_;
    QPoint lastClickPosition_;
    int clickCount_ = 0;
    QTimer blinkTimer_;
    QElapsedTimer toastClock_;
    QString preedit_, toast_;
    mutable QVector<std::shared_ptr<RowLayout>> layouts_;
    mutable bool layoutDirty_ = true;
    static constexpr qreal leftPadding_ = 17, topPadding_ = 14, rightPadding_ = 20, bottomPadding_ = 13;
    void startSession();
    void updateGrid();
    void invalidateLayouts();
    void ensureLayouts() const;
    int firstVisibleRow() const;
    bool selected(int row, int column) const;
    void selectWord(QPoint cell);
    void selectLine(QPoint cell);
    void scrollBy(int rows);
    void showToast(const QString& text);
    void sendKey(QKeyEvent* event);
    void sendRaw(const QByteArray& bytes);
    void sendWin32Key(int virtualKey, int scanCode, int unicode, int state);
    QRectF scrollBarTrack() const;
    QRectF scrollBarThumb() const;
};
