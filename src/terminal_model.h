#pragma once
#include <QObject>
#include <QColor>
#include <QPoint>
#include <QVector>
#include <deque>
#include <vterm.h>

struct TerminalCell {
    QString text;
    int width = 1;
    QColor foreground, background;
    bool defaultBackground = true;
    bool bold = false, italic = false, underline = false, strike = false, blink = false, conceal = false;
};

struct TerminalRow {
    QVector<TerminalCell> cells;
    bool continuation = false;
};

class TerminalModel : public QObject {
    Q_OBJECT
public:
    explicit TerminalModel(int columns = 100, int rows = 30, int history = 6000, QObject* parent = nullptr);
    ~TerminalModel() override;
    void feed(const QByteArray& bytes);
    void resize(int columns, int rows);
    void reset();
    void clearHistory();
    void setHistoryLimit(int limit);
    int columns() const;
    int screenRows() const;
    int historyRows() const;
    int rowCount() const;
    TerminalRow row(int index) const;
    QString plainText() const;
    QString textRange(QPoint start, QPoint end) const;
    QPoint cursor() const;
    bool cursorVisible() const { return cursorVisible_; }
    bool cursorBlink() const { return cursorBlink_; }
    int cursorShape() const { return cursorShape_; }
    bool alternateScreen() const { return alternate_; }
    bool mouseReporting() const { return mouseMode_ != 0; }
    void key(VTermKey key, VTermModifier modifiers = VTERM_MOD_NONE);
    void text(const QString& text, VTermModifier modifiers = VTERM_MOD_NONE);
    void paste(const QString& text);
    void mouse(int row, int column, int button, bool pressed, VTermModifier modifiers);
    void mouseMove(int row, int column, VTermModifier modifiers);
    void focus(bool focused);
signals:
    void changed();
    void titleChanged(const QString& title);
    void directoryChanged(const QString& directory);
    void responseReady(const QByteArray& bytes);
    void bell();
private:
    VTerm* term_;
    VTermState* state_;
    VTermScreen* screen_;
    std::deque<TerminalRow> history_;
    QVector<bool> beforeContinuation_;
    int pushedInOperation_ = 0;
    int historyLimit_;
    bool alternate_ = false, cursorVisible_ = true, cursorBlink_ = true;
    int cursorShape_ = VTERM_PROP_CURSORSHAPE_BLOCK, mouseMode_ = 0;
    QByteArray titleBuffer_, directoryBuffer_;
    TerminalCell convert(const VTermScreenCell& cell) const;
    void rememberLines();
    void reflowHistory(int columns);
    static int damageCallback(VTermRect, void*);
    static int cursorCallback(VTermPos, VTermPos, int, void*);
    static int propertyCallback(VTermProp, VTermValue*, void*);
    static int bellCallback(void*);
    static int pushCallback(int, const VTermScreenCell*, void*);
    static int pushReflowCallback(int, const VTermScreenCell*, int, void*);
    static int clearCallback(void*);
    static int oscCallback(int, VTermStringFragment, void*);
    static void outputCallback(const char*, size_t, void*);
};
