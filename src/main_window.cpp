#include "main_window.h"
#include "backdrop.h"
#include "chrome.h"
#include "settings_dialog.h"
#include "terminal_view.h"
#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>
#include <QRegularExpression>
#include <QRegion>
#include <QSaveFile>
#include <QScreen>
#include <QShortcut>
#include <QStackedWidget>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <QWindow>
#include <algorithm>
#include <windows.h>

namespace {
QAction* namedAction(QMenu* menu, const QString& label, const QString& name) {
    QAction* action = menu->addAction(label); action->setObjectName(name); return action;
}
QString quickLabel(const QuickCommand& command) { return command.label + (command.admin ? QStringLiteral("  (admin)") : QString()); }
QString limitCharacters(const QString& value, int count) {
    auto characters = value.toUcs4();
    if (characters.size() > count) characters.resize(count);
    return QString::fromUcs4(reinterpret_cast<const char32_t*>(characters.constData()), characters.size());
}
QString menuStyle(const QString& accent) {
    return QString("QMenu{background:#0d1420;color:#e6eefb;border:1px solid #344256;border-radius:8px;padding:6px;font:10pt 'Segoe UI';}"
        "QMenu::item{padding:7px 26px 7px 22px;border-radius:4px;} QMenu::item:selected{background:%1;color:#05121a;}"
        "QMenu::item:disabled{color:#77869b;} QMenu::separator{height:1px;background:#304053;margin:5px 8px;}").arg(accent);
}
}

MainWindow::MainWindow(SettingsStore* store, QVector<ShellProfile> profiles, QWidget* parent, bool autoStart)
    : QMainWindow(parent), store_(store), profiles_(std::move(profiles)), autoStart_(autoStart) {
    setObjectName("mainWindow"); setWindowTitle("n2-cmd");
    setWindowIcon(QIcon(store_->assetPath("icon.png")));
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowSystemMenuHint | Qt::WindowMinMaxButtonsHint);
    setAttribute(Qt::WA_TranslucentBackground); setMinimumSize(860, 540);
    backdrop_ = new Backdrop(store_, this); setCentralWidget(backdrop_);
    auto* layout = new QVBoxLayout(backdrop_); layout->setContentsMargins(1, 1, 1, 1); layout->setSpacing(0);
    topbar_ = new TopBar(backdrop_); stack_ = new QStackedWidget(backdrop_); stack_->setObjectName("terminalStack");
    stack_->setAttribute(Qt::WA_TranslucentBackground); status_ = new TerminalStatusBar(backdrop_);
    layout->addWidget(topbar_); layout->addWidget(stack_, 1); layout->addWidget(status_);
    geometryTimer_ = new QTimer(this); geometryTimer_->setSingleShot(true); geometryTimer_->setInterval(350);
    connect(geometryTimer_, &QTimer::timeout, this, &MainWindow::saveGeometryToSettings);

    connect(topbar_, &TopBar::tabActivated, this, &MainWindow::setCurrentIndex);
    connect(topbar_, &TopBar::tabCloseRequested, this, [this](int i) { closeTab(i); });
    connect(topbar_, &TopBar::tabRenameRequested, this, &MainWindow::renameTabDialog);
    connect(topbar_, &TopBar::tabMenuRequested, this, &MainWindow::showTabMenu);
    connect(topbar_, &TopBar::newTabRequested, this, [this] { newTab(); });
    connect(topbar_, &TopBar::profileMenuRequested, this, &MainWindow::showProfileMenu);
    connect(topbar_, &TopBar::quickMenuRequested, this, &MainWindow::showQuickMenu);
    connect(topbar_, &TopBar::settingsRequested, this, &MainWindow::openSettings);
    connect(topbar_, &TopBar::stayOnTopRequested, this, &MainWindow::toggleStayOnTop);
    connect(topbar_, &TopBar::windowMoveStarted, this, &MainWindow::beginWindowMove);
    connect(topbar_, &TopBar::minimizeRequested, this, &QWidget::showMinimized);
    connect(topbar_, &TopBar::maximizeRequested, this, &MainWindow::toggleMaximized);
    connect(topbar_, &TopBar::closeRequested, this, &QWidget::close);
    connect(topbar_, &TopBar::cycleRequested, this, &MainWindow::cycleTab);
    installShortcuts();
    qApp->installEventFilter(this);
    restoreGeometryFromSettings(); applySettings();
    newTab(store_->settings.default_shell);
    captureBackdrop();
}

MainWindow::~MainWindow() {
    qApp->removeEventFilter(this);
    for (const auto& tab : tabs_) if (tab.view) tab.view->shutdown();
}
int MainWindow::tabCount() const { return tabs_.size(); }
int MainWindow::currentIndex() const { return stack_->currentIndex(); }
TerminalView* MainWindow::terminalAt(int index) const { return index >= 0 && index < tabs_.size() ? tabs_[index].view.data() : nullptr; }
QString MainWindow::tabTitle(int index) const { return index >= 0 && index < tabs_.size() ? tabs_[index].title : QString(); }
quint64 MainWindow::tabId(int index) const { return index >= 0 && index < tabs_.size() ? tabs_[index].id : 0; }
int MainWindow::indexForId(quint64 id) const { for (int i = 0; i < tabs_.size(); ++i) if (tabs_[i].id == id) return i; return -1; }
int MainWindow::indexForView(TerminalView* view) const { for (int i = 0; i < tabs_.size(); ++i) if (tabs_[i].view == view) return i; return -1; }
QString MainWindow::uniqueTitle(const QString& title, int excluded) const {
    const QString base = title.trimmed().isEmpty() ? QStringLiteral("Terminal") : title.trimmed();
    QString value = base; int suffix = 2;
    auto exists = [&](const QString& name) { for (int i = 0; i < tabs_.size(); ++i) if (i != excluded && tabs_[i].title == name) return true; return false; };
    while (exists(value)) value = base + " " + QString::number(suffix++);
    return value;
}

int MainWindow::newTab(const QString& profileKey) {
    if (closing_ || profiles_.isEmpty()) return -1;
    const ShellProfile profile = pickProfile(profiles_, profileKey.isEmpty() ? store_->settings.default_shell : profileKey);
    TerminalView* current = terminalAt(currentIndex());
    QString directory = current ? current->currentDirectory() : QDir::currentPath();
    auto* view = new TerminalView(store_, profile, directory, stack_, autoStart_);
    const quint64 id = nextId_++;
    Tab tab; tab.id = id; tab.view = view; tab.title = uniqueTitle(profile.label); tab.shellTitle = profile.label;
    tab.columns = view->model().columns(); tab.rows = view->model().screenRows();
    tabs_.push_back(tab); stack_->addWidget(view);
    connect(view, &TerminalView::titleChanged, this, [this, id](const QString& title) {
        const int index = indexForId(id); if (index < 0 || title.trimmed().isEmpty()) return;
        tabs_[index].shellTitle = title.trimmed();
        if (!tabs_[index].customTitle) { tabs_[index].title = uniqueTitle(title, index); refreshTabs(); }
    });
    connect(view, &TerminalView::gridChanged, this, [this, id](int columns, int rows) {
        const int index = indexForId(id); if (index < 0) return;
        tabs_[index].columns = columns; tabs_[index].rows = rows; updateStatus();
    });
    connect(view, &TerminalView::stateChanged, this, [this](bool) { updateStatus(); });
    connect(view, &TerminalView::zoomRequested, this, &MainWindow::zoom);
    connect(view, &TerminalView::newTabRequested, this, [this, profile] { newTab(profile.key); });
    connect(view, &TerminalView::closeTabRequested, this, [this, id] { closeTab(indexForId(id)); });
    connect(view, &TerminalView::settingsRequested, this, &MainWindow::openSettings);
    setCurrentIndex(tabs_.size() - 1); refreshTabs(); emit tabsChanged(); return tabs_.size() - 1;
}

void MainWindow::setCurrentIndex(int index) {
    if (index < 0 || index >= tabs_.size()) return;
    stack_->setCurrentIndex(index); topbar_->tabBar()->setCurrentIndex(index);
    if (tabs_[index].view) tabs_[index].view->setFocus(); updateStatus();
}
void MainWindow::refreshTabs() {
    QStringList titles; for (const auto& tab : tabs_) titles.push_back(tab.title);
    topbar_->setTabs(titles, currentIndex()); updateStatus();
}
void MainWindow::updateStatus() {
    const int index = currentIndex(); if (index < 0 || index >= tabs_.size() || !tabs_[index].view) return;
    const auto& tab = tabs_[index]; const double opacity = store_->settings.opaque_background ? 1.0 : store_->settings.opacity;
    status_->setInfo(tab.view->profile().label, tab.view->isRunning(), tab.columns, tab.rows, opacity);
}
bool MainWindow::closeTab(int index) {
    if (closing_ || index < 0 || index >= tabs_.size()) return false;
    const quint64 activeId = tabId(currentIndex());
    TerminalView* view = tabs_[index].view;
    tabs_.removeAt(index); stack_->removeWidget(view); view->shutdown(); view->deleteLater();
    if (tabs_.isEmpty()) { refreshTabs(); emit tabsChanged(); close(); return true; }
    const int previous = indexForId(activeId);
    setCurrentIndex(previous >= 0 ? previous : qMin(index, static_cast<int>(tabs_.size()) - 1));
    refreshTabs(); emit tabsChanged(); return true;
}
bool MainWindow::closeOtherTabs(int index) {
    const quint64 keep = tabId(index); if (!keep) return false;
    QVector<quint64> targets; for (const auto& tab : tabs_) if (tab.id != keep) targets.push_back(tab.id);
    for (quint64 id : targets) closeTab(indexForId(id));
    setCurrentIndex(indexForId(keep)); return true;
}
void MainWindow::cycleTab(int delta) {
    if (tabs_.size() > 1) setCurrentIndex((currentIndex() + delta % tabs_.size() + tabs_.size()) % tabs_.size());
}
bool MainWindow::renameTab(int index, const QString& requested) {
    if (index < 0 || index >= tabs_.size()) return false;
    const QString name = limitCharacters(requested.simplified(), 64); if (name.isEmpty()) return false;
    tabs_[index].title = uniqueTitle(name, index); tabs_[index].customTitle = true;
    refreshTabs(); emit tabsChanged(); return true;
}
void MainWindow::resetTabName(int index) {
    if (index < 0 || index >= tabs_.size()) return;
    tabs_[index].customTitle = false;
    tabs_[index].title = uniqueTitle(tabs_[index].shellTitle.isEmpty() ? tabs_[index].view->profile().label : tabs_[index].shellTitle, index);
    refreshTabs(); emit tabsChanged();
}
void MainWindow::renameTabDialog(int index) {
    if (index < 0) index = currentIndex(); const quint64 id = tabId(index); if (!id) return;
    bool accepted = false;
    const QString name = QInputDialog::getText(this, "Rename tab", "Tab name:", QLineEdit::Normal, tabTitle(index), &accepted);
    if (accepted) renameTab(indexForId(id), name);
}
QString MainWindow::outputFilename(int index) const {
    QString safe;
    for (QChar c : tabTitle(index)) safe += (c.isLetterOrNumber() || QStringLiteral(" -_.").contains(c)) ? c : QChar('_');
    safe.remove(QRegularExpression(QStringLiteral("^[ ._]+|[ ._]+$"))); if (safe.isEmpty()) safe = "terminal";
    return safe + "-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss") + ".txt";
}
bool MainWindow::saveTabOutput(int index, const QString& requested, QString* error) const {
    auto* view = terminalAt(index); if (!view || requested.isEmpty()) { if (error) *error = "No tab or output path selected."; return false; }
    QString path = requested; if (QFileInfo(path).suffix().isEmpty()) path += ".txt";
    QSaveFile file(path); if (!file.open(QIODevice::WriteOnly)) { if (error) *error = file.errorString(); return false; }
    QString text = view->plainText(); if (!text.isEmpty() && !text.endsWith('\n')) text += '\n';
    const QByteArray bytes = text.toUtf8();
    if (file.write(bytes) != bytes.size()) { if (error) *error = file.errorString(); file.cancelWriting(); return false; }
    if (!file.commit()) { if (error) *error = file.errorString(); return false; } return true;
}
void MainWindow::saveTabOutputDialog(int index) {
    if (index < 0) index = currentIndex(); const quint64 id = tabId(index); if (!id) return;
    const QString path = QFileDialog::getSaveFileName(this, "Save tab output", QDir::home().filePath(outputFilename(index)), "Text files (*.txt);;All files (*)");
    if (path.isEmpty()) return;
    QString error; if (!saveTabOutput(indexForId(id), path, &error)) showError("Could not save output: " + error);
}

QMenu* MainWindow::createTabMenu(int index, QWidget* parent) {
    auto* menu = new QMenu(parent ? parent : this); menu->setObjectName("tabContextMenu"); menu->setStyleSheet(menuStyle(store_->settings.accent));
    const quint64 id = tabId(index); if (!id) return menu;
    const QString profile = tabs_[index].view->profile().key;
    connect(namedAction(menu, "New tab", "newTabAction"), &QAction::triggered, this, [this, id, profile] { const int i = indexForId(id); if (i >= 0) { setCurrentIndex(i); newTab(profile); } });
    connect(namedAction(menu, "Duplicate tab", "duplicateTabAction"), &QAction::triggered, this, [this, id, profile] { const int i = indexForId(id); if (i >= 0) { setCurrentIndex(i); newTab(profile); } });
    connect(namedAction(menu, "Rename tab...\tF2", "renameTabAction"), &QAction::triggered, this, [this, id] { const int i = indexForId(id); if (i >= 0) renameTabDialog(i); });
    connect(namedAction(menu, "Save output...\tCtrl+Shift+S", "saveOutputAction"), &QAction::triggered, this, [this, id] { const int i = indexForId(id); if (i >= 0) saveTabOutputDialog(i); });
    if (tabs_[index].customTitle) connect(namedAction(menu, "Reset tab name", "resetTabNameAction"), &QAction::triggered, this, [this, id] { resetTabName(indexForId(id)); });
    menu->addSeparator();
    connect(namedAction(menu, "Close tab\tCtrl+Shift+W", "closeTabAction"), &QAction::triggered, this, [this, id] { closeTab(indexForId(id)); });
    auto* others = namedAction(menu, "Close other tabs", "closeOtherTabsAction"); others->setEnabled(tabs_.size() > 1);
    connect(others, &QAction::triggered, this, [this, id] {
        const int index = indexForId(id); if (index < 0 || tabs_.size() < 2) return;
        if (QMessageBox::question(this, "Close other tabs", QString("Close %1 other tabs and end their running commands?").arg(tabs_.size() - 1), QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) == QMessageBox::Yes)
            closeOtherTabs(indexForId(id));
    });
    return menu;
}
void MainWindow::showTabMenu(int index, QPoint position) { auto* menu = createTabMenu(index); menu->exec(position); delete menu; }
void MainWindow::showProfileMenu(QPoint position) {
    QMenu menu(this); menu.setObjectName("profileMenu"); menu.setStyleSheet(menuStyle(store_->settings.accent));
    for (const auto& profile : profiles_) {
        auto* action = namedAction(&menu, profile.label, "profile_" + profile.key); action->setToolTip(profile.description);
        connect(action, &QAction::triggered, this, [this, profile] { newTab(profile.key); });
    }
    menu.exec(position.isNull() ? topbar_->quickAnchor() : position);
}

QMenu* MainWindow::createQuickMenu(QWidget* parent) {
    auto* menu = new QMenu(parent ? parent : this); menu->setObjectName("quickCommandsMenu"); menu->setToolTipsVisible(true); menu->setStyleSheet(menuStyle(store_->settings.accent));
    QStringList categories; for (const auto& command : store_->quickCommands) if (!categories.contains(command.category)) categories.push_back(command.category);
    bool hasFavorites = false;
    for (const QString& key : store_->settings.pinned_commands) if (store_->quickCommand(key)) hasFavorites = true;
    hasFavorites = hasFavorites || !store_->settings.favorite_commands.isEmpty();
    if (hasFavorites) menu->addSection("Favorites");
    else { auto* hint = namedAction(menu, "No favorites yet", "emptyFavoritesHint"); hint->setEnabled(false); }
    for (const QString& key : store_->settings.pinned_commands) {
        const auto* command = store_->quickCommand(key); if (!command) continue;
        auto* action = namedAction(menu, quickLabel(*command), "favorite_builtin_" + key); action->setToolTip(command->description);
        connect(action, &QAction::triggered, this, [this, key] { if (!runQuickCommand(key)) showError(lastError_); });
    }
    for (int i = 0; i < store_->settings.favorite_commands.size(); ++i) {
        const auto favorite = store_->settings.favorite_commands[i];
        auto* action = namedAction(menu, favorite.name, "favorite_custom_" + QString::number(i)); action->setToolTip(favorite.command);
        connect(action, &QAction::triggered, this, [this, i] { if (!runCustomFavorite(i)) showError(lastError_); });
    }
    connect(namedAction(menu, "Add favorite command...", "addFavoriteAction"), &QAction::triggered, this, &MainWindow::addFavoriteDialog);
    auto* pinMenu = menu->addMenu("Pin built-in commands"); pinMenu->setObjectName("pinCommandsMenu");
    for (const QString& category : categories) {
        auto* submenu = pinMenu->addMenu(category); submenu->setObjectName("pinCategory_" + category);
        for (const auto& command : store_->quickCommands) if (command.category == category) {
            auto* action = namedAction(submenu, quickLabel(command), "pin_" + command.key); action->setCheckable(true); action->setChecked(store_->settings.pinned_commands.contains(command.key)); action->setToolTip(command.description);
            connect(action, &QAction::toggled, this, [this, key = command.key](bool checked) { QString error; if (!store_->setPinned(key, checked, &error)) showError(error); });
        }
    }
    if (!store_->settings.favorite_commands.isEmpty()) {
        auto* remove = menu->addMenu("Remove custom favorite"); remove->setObjectName("removeFavoritesMenu");
        for (int i = 0; i < store_->settings.favorite_commands.size(); ++i) {
            auto* action = namedAction(remove, store_->settings.favorite_commands[i].name, "removeFavorite_" + QString::number(i));
            connect(action, &QAction::triggered, this, [this, i] { QString error; if (!store_->removeFavorite(i, &error)) showError(error); });
        }
    }
    menu->addSeparator(); TerminalView* current = terminalAt(currentIndex());
    for (const QString& category : categories) {
        auto* submenu = menu->addMenu(category); submenu->setObjectName("quickCategory_" + category);
        for (const auto& command : store_->quickCommands) if (command.category == category) {
            auto* action = namedAction(submenu, quickLabel(command), "quick_" + command.key); action->setToolTip(command.description);
            action->setEnabled(current && !command.commandFor(current->profile().key).isEmpty());
            connect(action, &QAction::triggered, this, [this, key = command.key] { if (!runQuickCommand(key)) showError(lastError_); });
        }
    }
    return menu;
}
void MainWindow::showQuickMenu(QPoint position) { auto* menu = createQuickMenu(); menu->exec(position.isNull() ? topbar_->quickAnchor() : position); delete menu; }
bool MainWindow::runQuickCommand(const QString& key) {
    const auto* command = store_->quickCommand(key); auto* view = terminalAt(currentIndex());
    if (!command || !view) { recordError("Choose a tab and a valid quick command."); return false; }
    const QString text = command->commandFor(view->profile().key);
    if (text.isEmpty()) { recordError(command->label + " is not available for " + view->profile().label + "."); return false; }
    if (!view->runCommand(text)) { recordError("The command could not be sent. Restart the ended shell or return to its prompt."); return false; }
    lastError_.clear(); return true;
}
bool MainWindow::runCustomFavorite(int index) {
    auto* view = terminalAt(currentIndex());
    if (!view || index < 0 || index >= store_->settings.favorite_commands.size()) { recordError("Choose a valid favorite and terminal tab."); return false; }
    if (!view->runCommand(store_->settings.favorite_commands[index].command)) { recordError("The favorite could not be sent. Restart the ended shell or return to its prompt."); return false; }
    lastError_.clear(); return true;
}
void MainWindow::addFavoriteDialog() {
    QString suggestion; auto* view = terminalAt(currentIndex());
    if (view) { const QString selected = view->selectedText().trimmed(); if (!selected.contains('\n') && !selected.contains('\r')) suggestion = selected.left(2000); }
    bool accepted = false;
    const QString name = QInputDialog::getText(this, "Favorite command", "Name:", QLineEdit::Normal, {}, &accepted);
    if (!accepted || name.trimmed().isEmpty()) return;
    const QString command = QInputDialog::getText(this, "Favorite command", "Command:", QLineEdit::Normal, suggestion, &accepted);
    if (!accepted || command.trimmed().isEmpty()) return;
    QString error; if (!store_->addFavorite(name.simplified(), command, &error)) showError(error);
}

void MainWindow::applySettings() {
    store_->sanitize(); setWindowOpacity(1.0);
    updateBackdropEffect();
    QPalette palette = qApp->palette();
    palette.setColor(QPalette::Window, QColor("#0b111b")); palette.setColor(QPalette::WindowText, QColor("#e6eefb"));
    palette.setColor(QPalette::Base, QColor("#101827")); palette.setColor(QPalette::AlternateBase, QColor("#182434"));
    palette.setColor(QPalette::Text, QColor("#e6eefb")); palette.setColor(QPalette::Button, QColor("#182434"));
    palette.setColor(QPalette::ButtonText, QColor("#e6eefb")); palette.setColor(QPalette::Highlight, QColor(store_->settings.accent));
    palette.setColor(QPalette::HighlightedText, QColor("#05121a")); palette.setColor(QPalette::ToolTipBase, QColor("#101827"));
    palette.setColor(QPalette::ToolTipText, QColor("#e6eefb")); palette.setColor(QPalette::PlaceholderText, QColor("#7b8aa0"));
    palette.setColor(QPalette::Link, QColor(store_->settings.accent)); palette.setColor(QPalette::Disabled, QPalette::Text, QColor("#8494aa"));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#8494aa")); qApp->setPalette(palette);
    backdrop_->applySettings(); topbar_->setAccent(store_->settings.accent); status_->setAccent(store_->settings.accent);
    for (const auto& tab : tabs_) if (tab.view) tab.view->applySettings(); updateStatus();
}
void MainWindow::openSettings() {
    if (settingsDialog_) { settingsDialog_->raise(); settingsDialog_->activateWindow(); return; }
    settingsDialog_ = new SettingsDialog(store_, profiles_, this); settingsDialog_->setAttribute(Qt::WA_DeleteOnClose);
    connect(settingsDialog_, &SettingsDialog::settingsChanged, this, &MainWindow::applySettings);
    settingsDialog_->open();
}
void MainWindow::zoom(int delta) {
    store_->settings.font_size = delta == 0 ? 10.0 : std::clamp(store_->settings.font_size + .5 * delta, 7.5, 26.0);
    QString error; if (!store_->save(&error)) recordError(error); applySettings();
}
void MainWindow::recordError(const QString& message) { lastError_ = message; emit errorOccurred(message); }
void MainWindow::showError(const QString& message) { recordError(message); QMessageBox::warning(this, "n2-cmd", message); }
void MainWindow::toggleMaximized() { if (isMaximized()) showNormal(); else showMaximized(); }
void MainWindow::toggleStayOnTop() {
    stayOnTop_ = !stayOnTop_;
    SetWindowPos(reinterpret_cast<HWND>(winId()), stayOnTop_ ? HWND_TOPMOST : HWND_NOTOPMOST,
                 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    topbar_->setStayOnTop(stayOnTop_);
}
void MainWindow::toggleFullscreen() {
    if (isFullScreen()) { showNormal(); if (maximizedBeforeFullscreen_) showMaximized(); }
    else { maximizedBeforeFullscreen_ = isMaximized(); showFullScreen(); }
}
void MainWindow::beginWindowMove() {
    snapArmed_ = true;
    moveStartGeometry_ = geometry();
}
bool MainWindow::nativeEvent(const QByteArray& eventType, void* message, qintptr* result) {
    MSG* msg = static_cast<MSG*>(message);
    if (msg->message == WM_EXITSIZEMOVE && snapArmed_) {
        const bool moved = geometry().topLeft() != moveStartGeometry_.topLeft();
        snapArmed_ = false;
        if (moved) QTimer::singleShot(0, this, &MainWindow::applySnapFromCursor);
    }
    return QMainWindow::nativeEvent(eventType, message, result);
}
void MainWindow::applySnapFromCursor() {
    if (!store_->settings.snap_windows || isFullScreen() || isMaximized()) return;
    const QPoint cursor = QCursor::pos();
    QScreen* screen = QGuiApplication::screenAt(cursor);
    if (!screen) screen = QGuiApplication::primaryScreen();
    if (!screen) return;
    const QRect area = screen->availableGeometry();
    const int edge = 12;
    const bool left = cursor.x() <= area.left() + edge;
    const bool right = cursor.x() >= area.left() + area.width() - edge;
    const bool top = cursor.y() <= area.top() + edge;
    const bool bottom = cursor.y() >= area.top() + area.height() - edge;
    if (!left && !right && !top && !bottom) return;
    const int halfWidth = area.width() / 2;
    const int halfHeight = area.height() / 2;
    if ((left || right) && (top || bottom)) {
        const int x = left ? area.left() : area.left() + area.width() - halfWidth;
        const int y = top ? area.top() : area.top() + area.height() - halfHeight;
        showNormal();
        setGeometry(x, y, halfWidth, halfHeight);
    } else if (left || right) {
        showNormal();
        setGeometry(left ? area.left() : area.left() + area.width() - halfWidth,
                    area.top(), halfWidth, area.height());
    } else if (top) {
        showMaximized();
    }
}

void MainWindow::installShortcuts() {
    auto bind = [this](const QString& keys, auto callback) { auto* shortcut = new QShortcut(QKeySequence(keys), this); shortcut->setContext(Qt::WidgetWithChildrenShortcut); connect(shortcut, &QShortcut::activated, this, callback); };
    bind("Ctrl+Tab", [this] { cycleTab(1); }); bind("Ctrl+Shift+Tab", [this] { cycleTab(-1); });
    bind("Ctrl+PageDown", [this] { cycleTab(1); }); bind("Ctrl+PageUp", [this] { cycleTab(-1); });
    bind("F2", [this] { renameTabDialog(); }); bind("F11", [this] { toggleFullscreen(); });
}
bool MainWindow::handlePhysicalShortcut(QKeyEvent* event, bool execute) {
    const auto mods = event->modifiers(); if (!(mods & Qt::ControlModifier) || (mods & Qt::AltModifier)) return false;
    const int native = event->nativeVirtualKey() ? static_cast<int>(event->nativeVirtualKey()) : event->key();
    const bool shift = mods & Qt::ShiftModifier;
    if (shift && (native == 'T' || native == 'W' || native == 'P' || native == 'S')) {
        if (execute) { if (native == 'T') newTab(); else if (native == 'W') closeTab(currentIndex()); else if (native == 'P') showQuickMenu(); else saveTabOutputDialog(); }
        return true;
    }
    if (!shift && native >= '1' && native <= '9') { if (execute) setCurrentIndex(native - '1'); return true; }
    if (native == '0') { if (execute) zoom(0); return true; }
    if (native == 0xbb || native == '+' || native == '=') { if (execute) zoom(1); return true; }
    if (native == 0xbd || native == '-' || native == '_') { if (execute) zoom(-1); return true; }
    if (!shift && (native == 0xbc || native == ',')) { if (execute) openSettings(); return true; }
    return false;
}

bool MainWindow::eventFilter(QObject* object, QEvent* event) {
    auto* widget = qobject_cast<QWidget*>(object);
    if (!widget || widget->window() != this) return QMainWindow::eventFilter(object, event);
    if (event->type() == QEvent::ShortcutOverride && handlePhysicalShortcut(static_cast<QKeyEvent*>(event), false)) { event->accept(); return true; }
    if (event->type() == QEvent::KeyPress && handlePhysicalShortcut(static_cast<QKeyEvent*>(event), true)) { event->accept(); return true; }
    if (!isMaximized() && !isFullScreen() && (event->type() == QEvent::MouseMove || event->type() == QEvent::MouseButtonPress)) {
        auto* mouse = static_cast<QMouseEvent*>(event); const QPoint point = mapFromGlobal(mouse->globalPosition().toPoint());
        Qt::Edges edges;
        if (point.x() >= 0 && point.x() < 6) edges |= Qt::LeftEdge;
        if (point.x() < width() && point.x() >= width() - 6) edges |= Qt::RightEdge;
        if (point.y() >= 0 && point.y() < 6) edges |= Qt::TopEdge;
        if (point.y() < height() && point.y() >= height() - 6) edges |= Qt::BottomEdge;
        if (edges) {
            const bool horizontal = edges.testFlag(Qt::LeftEdge) || edges.testFlag(Qt::RightEdge);
            const bool vertical = edges.testFlag(Qt::TopEdge) || edges.testFlag(Qt::BottomEdge);
            Qt::CursorShape shape = horizontal ? Qt::SizeHorCursor : Qt::SizeVerCursor;
            if (horizontal && vertical) shape = ((edges.testFlag(Qt::LeftEdge) && edges.testFlag(Qt::TopEdge)) || (edges.testFlag(Qt::RightEdge) && edges.testFlag(Qt::BottomEdge))) ? Qt::SizeFDiagCursor : Qt::SizeBDiagCursor;
            if (resizeCursorOwner_ != widget) { if (resizeCursorOwner_) resizeCursorOwner_->setCursor(previousCursor_); resizeCursorOwner_ = widget; previousCursor_ = widget->cursor(); }
            widget->setCursor(shape);
            if (event->type() == QEvent::MouseButtonPress && mouse->button() == Qt::LeftButton && windowHandle()) { windowHandle()->startSystemResize(edges); return true; }
        } else if (resizeCursorOwner_) { resizeCursorOwner_->setCursor(previousCursor_); resizeCursorOwner_.clear(); }
    }
    if (event->type() == QEvent::Move && widget == this) {
        if (geometryTimer_) geometryTimer_->start();
        if (backdrop_ && !store_->settings.opaque_background) backdrop_->update();
    }
    return QMainWindow::eventFilter(object, event);
}

void MainWindow::updateWindowMask() {
    // The rounded corners are painted by Backdrop. A native mask clips the
    // right and bottom edges under fractional display scaling.
    clearMask();
}
void MainWindow::updateBackdropEffect() {
    HWND hwnd = reinterpret_cast<HWND>(winId());
    enum { WCA_ACCENT_POLICY = 19 };
    struct AccentPolicy { int state; int flags; int gradient; int animation; };
    struct CompositionData { int attribute; void* data; SIZE_T size; };
    using SetComposition = BOOL(WINAPI*)(HWND, CompositionData*);
    static SetComposition setter = reinterpret_cast<SetComposition>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetWindowCompositionAttribute"));
    if (!setter) return;
    AccentPolicy policy{};
    const bool blurred = !store_->settings.opaque_background;
    if (blurred) {
        policy.state = 4; // ACCENT_ENABLE_ACRYLICBLURBEHIND
        policy.flags = 2;
        policy.gradient = 0x551b110b;
    }
    CompositionData data{WCA_ACCENT_POLICY, &policy, sizeof(policy)};
    if (!setter(hwnd, &data) && blurred) {
        policy.state = 3; // ACCENT_ENABLE_BLURBEHIND
        setter(hwnd, &data);
    }
}
void MainWindow::captureBackdrop() {
    if (!backdrop_) return;
    const int left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (width <= 0 || height <= 0) return;
    const bool visible = isVisible();
    if (visible) {
        setWindowOpacity(0.0);
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        QThread::msleep(40);
    }
    HDC screen = GetDC(nullptr);
    HDC memory = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateCompatibleBitmap(screen, width, height);
    HGDIOBJ previous = SelectObject(memory, bitmap);
    BitBlt(memory, 0, 0, width, height, screen, left, top, SRCCOPY);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    QImage image(width, height, QImage::Format_RGB32);
    GetDIBits(memory, bitmap, 0, height, image.bits(), &info, DIB_RGB_COLORS);
    SelectObject(memory, previous);
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
    if (visible) setWindowOpacity(1.0);
    if (!image.isNull()) backdrop_->setDesktopCapture(image, QPoint(left, top));
}
void MainWindow::showEvent(QShowEvent* event) {
    QMainWindow::showEvent(event);
    updateBackdropEffect();
}

void MainWindow::restoreGeometryFromSettings() {
    const auto& s = store_->settings; QSize size(s.window_width, s.window_height);
    QScreen* primary = QGuiApplication::primaryScreen();
    QRect area = primary ? primary->availableGeometry() : QRect(0, 0, 1280, 800);
    QRect desired(QPoint(s.window_x, s.window_y), size);
    bool visible = false;
    for (auto* screen : QGuiApplication::screens()) if (desired.intersects(screen->availableGeometry())) visible = true;
    if ((s.window_x == -1 && s.window_y == -1) || !visible)
        desired.moveTopLeft(QPoint(area.x() + qMax(0, (area.width() - size.width()) / 2), area.y() + qMax(0, (area.height() - size.height()) / 2)));
    setGeometry(desired);
}
void MainWindow::saveGeometryToSettings() {
    if (isFullScreen()) return;
    QRect geometry = isMaximized() ? normalGeometry() : this->geometry(); if (!geometry.isValid()) return;
    auto& s = store_->settings; s.window_x = geometry.x(); s.window_y = geometry.y(); s.window_width = geometry.width(); s.window_height = geometry.height(); s.maximized = isMaximized();
    QString error; if (!store_->save(&error)) recordError(error);
}
void MainWindow::closeEvent(QCloseEvent* event) {
    if (!closing_) { saveGeometryToSettings(); closing_ = true; geometryTimer_->stop(); for (const auto& tab : tabs_) if (tab.view) tab.view->shutdown(); }
    event->accept();
}
void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    updateWindowMask();
    if (geometryTimer_) geometryTimer_->start();
}
void MainWindow::changeEvent(QEvent* event) {
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange) {
        if (topbar_) topbar_->setMaximized(isMaximized() || isFullScreen());
        updateWindowMask();
        if (backdrop_) backdrop_->update();
        if (geometryTimer_) geometryTimer_->start();
    } else if (event->type() == QEvent::ActivationChange) {
        for (const auto& tab : tabs_) if (tab.view) tab.view->update();
    }
}


