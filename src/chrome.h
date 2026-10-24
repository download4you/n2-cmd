#pragma once
#include <QTabBar>
#include <QWidget>

class QLabel;
class QToolButton;

class TerminalTabBar : public QTabBar {
    Q_OBJECT
public:
    explicit TerminalTabBar(QWidget* parent = nullptr);
signals:
    void renameRequested(int index);
    void menuRequested(int index, QPoint globalPosition);
    void cycleRequested(int delta);
protected:
    void contextMenuEvent(QContextMenuEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
};

class TopBar : public QWidget {
    Q_OBJECT
public:
    explicit TopBar(QWidget* parent = nullptr);
    void setTabs(const QStringList& titles, int activeIndex);
    void setAccent(const QString& accent);
    void setMaximized(bool maximized);
    void setStayOnTop(bool enabled);
    QPoint quickAnchor() const;
    TerminalTabBar* tabBar() const { return tabs_; }
signals:
    void tabActivated(int index);
    void tabCloseRequested(int index);
    void tabRenameRequested(int index);
    void tabMenuRequested(int index, QPoint position);
    void newTabRequested();
    void profileMenuRequested(QPoint position);
    void quickMenuRequested(QPoint position);
    void settingsRequested();
    void stayOnTopRequested();
    void windowMoveStarted();
    void minimizeRequested();
    void maximizeRequested();
    void closeRequested();
    void cycleRequested(int delta);
protected:
    void mousePressEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
private:
    TerminalTabBar* tabs_;
    QLabel* brand_;
    QToolButton* quick_;
    QToolButton* pin_;
    QToolButton* maximize_;
    QString accent_ = QStringLiteral("#3ddc97");
};

class TerminalStatusBar : public QWidget {
    Q_OBJECT
public:
    explicit TerminalStatusBar(QWidget* parent = nullptr);
    void setAccent(const QString& accent);
    void setInfo(const QString& shell, bool running, int columns, int rows, double opacity);
private:
    QLabel *state_, *grid_, *help_;
    QString accent_ = QStringLiteral("#3ddc97");
};
