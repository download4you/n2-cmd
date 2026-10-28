#pragma once
#include "settings_store.h"
#include <QMainWindow>
#include <QPointer>
#include <QVector>
#include <QCursor>

class Backdrop;
class TopBar;
class TerminalStatusBar;
class TerminalView;
class SettingsDialog;
class QStackedWidget;
class QMenu;
class QKeyEvent;
class QTimer;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(SettingsStore* store, QVector<ShellProfile> profiles, QWidget* parent = nullptr, bool autoStart = true);
    ~MainWindow() override;
    int newTab(const QString& profileKey = {});
    bool closeTab(int index);
    bool closeOtherTabs(int index);
    bool renameTab(int index, const QString& name);
    void resetTabName(int index);
    bool saveTabOutput(int index, const QString& path, QString* error = nullptr) const;
    QString outputFilename(int index) const;
    bool runQuickCommand(const QString& key);
    bool runCustomFavorite(int index);
    int tabCount() const;
    int currentIndex() const;
    void setCurrentIndex(int index);
    QString tabTitle(int index) const;
    quint64 tabId(int index) const;
    TerminalView* terminalAt(int index) const;
    QMenu* createQuickMenu(QWidget* parent = nullptr);
    QMenu* createTabMenu(int index, QWidget* parent = nullptr);
    QString lastError() const { return lastError_; }
public slots:
    void applySettings();
    void openSettings();
    void showQuickMenu(QPoint position = {});
    void showProfileMenu(QPoint position = {});
    void showTabMenu(int index, QPoint position);
    void renameTabDialog(int index = -1);
    void saveTabOutputDialog(int index = -1);
    void addFavoriteDialog();
    void cycleTab(int delta);
    void zoom(int delta);
    void toggleMaximized();
    void toggleFullscreen();
    void toggleStayOnTop();
signals:
    void tabsChanged();
    void errorOccurred(const QString& message);
protected:
    bool eventFilter(QObject*, QEvent*) override;
    bool nativeEvent(const QByteArray&, void*, qintptr*) override;
    void closeEvent(QCloseEvent*) override;
    void showEvent(QShowEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void changeEvent(QEvent*) override;
private:
    struct Tab {
        quint64 id = 0;
        QPointer<TerminalView> view;
        QString title;
        QString shellTitle;
        bool customTitle = false;
        int columns = 0, rows = 0;
    };
    int indexForId(quint64 id) const;
    int indexForView(TerminalView* view) const;
    QString uniqueTitle(const QString& title, int excluded = -1) const;
    void refreshTabs();
    void updateStatus();
    void restoreGeometryFromSettings();
    void saveGeometryToSettings();
    void updateWindowMask();
    void beginWindowMove();
    void applySnapFromCursor();
    void showError(const QString& message);
    void recordError(const QString& message);
    void installShortcuts();
    bool handlePhysicalShortcut(QKeyEvent* event, bool execute);
    SettingsStore* store_;
    QVector<ShellProfile> profiles_;
    QVector<Tab> tabs_;
    quint64 nextId_ = 1;
    bool autoStart_, closing_ = false;
    bool maximizedBeforeFullscreen_ = false;
    bool stayOnTop_ = false;
    bool snapArmed_ = false;
    QRect moveStartGeometry_;
    Backdrop* backdrop_ = nullptr;
    TopBar* topbar_ = nullptr;
    TerminalStatusBar* status_ = nullptr;
    QStackedWidget* stack_ = nullptr;
    QPointer<SettingsDialog> settingsDialog_;
    QString lastError_;
    QTimer* geometryTimer_ = nullptr;
    QPointer<QWidget> resizeCursorOwner_;
    QCursor previousCursor_;
};
