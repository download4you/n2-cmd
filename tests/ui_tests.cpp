#include "main_window.h"
#include "settings_dialog.h"
#include "backdrop.h"
#include "chrome.h"
#include "terminal_view.h"
#include <QtTest>
#include <QAction>
#include <QBuffer>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QMenu>
#include <QPushButton>
#include <QSignalSpy>
#include <QSlider>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QToolButton>
#include <memory>

namespace {
QString persian() { return QString::fromUcs4(U"\u0633\u0644\u0627\u0645 \u0641\u0627\u0631\u0633\u06cc"); }
QVector<ShellProfile> profiles() {
    return {{"native", "n2-cmd - Native", "cmd.exe", {}, "Native CMD"},
        {"enhanced", "n2-cmd - Enhanced", "cmd.exe", {}, "Enhanced CMD"},
        {"powershell", "PowerShell", "powershell.exe", {}, "Windows PowerShell"},
        {"wsl", "WSL", "wsl.exe", {}, "Linux"}};
}
QByteArray read(const QString& path) { QFile file(path); if (!file.open(QIODevice::ReadOnly)) return {}; return file.readAll(); }
void write(const QString& path, const QByteArray& bytes) { QFile file(path); if (file.open(QIODevice::WriteOnly)) file.write(bytes); }
QAction* action(QMenu* menu, const QString& name) { return menu->findChild<QAction*>(name); }
}

class NativeUiTests : public QObject {
    Q_OBJECT
private slots:
    void settingsControlsAndReset();
    void imageFormatsAndAtomicFailure();
    void allExifOrientations();
    void tabNamesIdsAndExport();
    void tabMenuTargetsStableIdentity();
    void quickCommandMenusAndPersistence();
    void physicalKeysAndButtons();
    void optionalVisualSnapshot();
};

void NativeUiTests::settingsControlsAndReset() {
    QTemporaryDir directory; QVERIFY(directory.isValid());
    SettingsStore store(directory.path()); QString error; QVERIFY2(store.load(&error), qPrintable(error));
    QVERIFY(store.addFavorite(persian(), "echo test", &error)); QVERIFY(store.setPinned("cwd", true, &error));
    store.settings.default_shell = "cmd";
    SettingsDialog dialog(&store, profiles()); QSignalSpy changed(&dialog, &SettingsDialog::settingsChanged);
    auto* opacity = dialog.findChild<QSlider*>("opacitySlider"); QVERIFY(opacity); QCOMPARE(opacity->minimum(), 30); QCOMPARE(opacity->maximum(), 100);
    auto* dim = dialog.findChild<QSlider*>("dimSlider"); QVERIFY(dim); QCOMPARE(dim->minimum(), 0); QCOMPARE(dim->maximum(), 92);
    auto* blur = dialog.findChild<QSlider*>("blurSlider"); QVERIFY(blur); QCOMPARE(blur->minimum(), 0); QCOMPARE(blur->maximum(), 40);
    auto* tint = dialog.findChild<QSlider*>("tintSlider"); QVERIFY(tint); QCOMPARE(tint->minimum(), 0); QCOMPARE(tint->maximum(), 88);
    auto* fontSize = dialog.findChild<QSlider*>("fontSizeSlider"); QVERIFY(fontSize); QCOMPARE(fontSize->minimum(), 15); QCOMPARE(fontSize->maximum(), 52);
    opacity->setValue(73); QCOMPARE(store.settings.opacity, .73);
    dim->setValue(42); QCOMPARE(store.settings.bg_dim, .42);
    blur->setValue(0); QCOMPARE(store.settings.bg_blur, 0);
    tint->setValue(11); QCOMPARE(store.settings.terminal_tint, .11);
    fontSize->setValue(31); QCOMPARE(store.settings.font_size, 15.5);
    auto* fontCombo = dialog.findChild<QComboBox*>("fontCombo"); QVERIFY(fontCombo);
    for (const QString& family : {QStringLiteral("Cascadia Mono"), QStringLiteral("Cascadia Code"), QStringLiteral("Consolas"),
                                  QStringLiteral("JetBrains Mono"), QStringLiteral("Fira Code"), QStringLiteral("Courier New")})
        QVERIFY2(fontCombo->findText(family) >= 0, qPrintable(family));
    fontCombo->setCurrentText("Consolas"); QCOMPARE(store.settings.font_family, QString("Consolas"));
    auto* shell = dialog.findChild<QComboBox*>("defaultShellCombo"); QCOMPARE(shell->currentData().toString(), QString("native"));
    shell->setCurrentIndex(shell->findData("enhanced")); QCOMPARE(store.settings.default_shell, QString("enhanced"));
    dialog.findChild<QCheckBox*>("copyOnSelectCheck")->setChecked(false); QVERIFY(!store.settings.copy_on_select);
    dialog.findChild<QCheckBox*>("snapWindowsCheck")->setChecked(false); QVERIFY(!store.settings.snap_windows);
    dialog.findChild<QCheckBox*>("opaqueBackgroundCheck")->setChecked(false); QVERIFY(!store.settings.opaque_background);
    dialog.findChild<QSpinBox*>("scrollbackSpin")->setValue(12500); QCOMPARE(store.settings.scrollback, 12500);
    for (const QString& accent : {QStringLiteral("accentCyan"), QStringLiteral("accentViolet"), QStringLiteral("accentMint"),
                                  QStringLiteral("accentAmber"), QStringLiteral("accentRose")})
        QVERIFY2(dialog.findChild<QPushButton*>(accent), qPrintable(accent));
    dialog.findChild<QPushButton*>("accentViolet")->click(); QCOMPARE(store.settings.accent, QString("#9b8cff"));
    SettingsStore reloaded(directory.path()); QVERIFY(reloaded.load(&error)); QCOMPARE(reloaded.settings.opacity, .73); QCOMPARE(reloaded.settings.font_size, 15.5); QVERIFY(!reloaded.settings.snap_windows); QVERIFY(!reloaded.settings.opaque_background);
    QVERIFY(changed.count() >= 10);
    dialog.resetAppearance(); QCOMPARE(store.settings.opacity, .94); QCOMPARE(store.settings.font_family, QString("Vazir Code Hack"));
    QCOMPARE(store.settings.font_size, 10.0); QCOMPARE(store.settings.bg_blur, 8); QCOMPARE(store.settings.accent, QString("#3ddc97"));
    QCOMPARE(store.settings.default_shell, QString("enhanced")); QVERIFY(!store.settings.copy_on_select); QCOMPARE(store.settings.scrollback, 12500);
    QCOMPARE(store.settings.favorite_commands.size(), 1); QCOMPARE(store.settings.pinned_commands, QStringList{"cwd"});
}

void NativeUiTests::imageFormatsAndAtomicFailure() {
    QTemporaryDir directory; QVERIFY(directory.isValid());
    const QString output = directory.filePath(persian() + ".png"); QString error;
    QImage source(13, 7, QImage::Format_RGB32); source.fill(QColor(14, 100, 220));
    for (const QByteArray& format : {QByteArray("png"), QByteArray("jpg"), QByteArray("bmp"), QByteArray("webp")}) {
        const QString input = directory.filePath("source." + QString::fromLatin1(format));
        QVERIFY2(source.save(input, format.constData()), format.constData());
        const QByteArray original = read(input);
        QVERIFY2(Backdrop::importWallpaper(input, output, &error), qPrintable(error));
        QImageReader reader(output); QCOMPARE(reader.format(), QByteArray("png"));
        const QImage decoded = reader.read(); QCOMPARE(decoded.size(), source.size()); QVERIFY(!decoded.hasAlphaChannel());
        QCOMPARE(read(input), original);
    }
    const QString gif = directory.filePath("pixel.gif");
    write(gif, QByteArray::fromBase64("R0lGODlhAQABAIAAAAAAAP///yH5BAEAAAAALAAAAAABAAEAAAIBRAA7"));
    QVERIFY2(Backdrop::importWallpaper(gif, output, &error), qPrintable(error)); QCOMPARE(QImage(output).size(), QSize(1, 1));
    const QByteArray preserved = read(output);
    const QString invalid = directory.filePath("corrupt.jpg"); write(invalid, "not an image");
    QVERIFY(!Backdrop::importWallpaper(invalid, output, &error)); QVERIFY(!error.isEmpty()); QCOMPARE(read(output), preserved);
    QVERIFY(!Backdrop::importWallpaper(directory.filePath("missing.png"), output, &error)); QCOMPARE(read(output), preserved);
    QVERIFY(Backdrop::importWallpaper(output, output, &error)); QCOMPARE(QImage(output).size(), QSize(1, 1));
    SettingsStore store(directory.filePath("settings")); QVERIFY(store.load(&error));
    SettingsDialog dialog(&store, profiles()); QVERIFY(dialog.applyWallpaper(gif, &error));
    const QString savedPath = store.settings.background; const QByteArray savedImage = read(savedPath);
    QVERIFY(!dialog.applyWallpaper(invalid, &error)); QCOMPARE(store.settings.background, savedPath); QCOMPARE(read(savedPath), savedImage);
    const QString stale = QDir(store.dataDirectory()).filePath("wallpaper_custom.jpg"); write(stale, "old wallpaper");
    QVERIFY(dialog.applyWallpaper(gif, &error)); QVERIFY(!QFileInfo::exists(stale));
}

void NativeUiTests::allExifOrientations() {
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QImage image(3, 2, QImage::Format_RGB32);
    for (int y = 0; y < 2; ++y) for (int x = 0; x < 3; ++x) image.setPixelColor(x, y, QColor(40 + x * 70, 40 + y * 150, 20 + (x + y) * 30));
    QByteArray jpeg; QBuffer buffer(&jpeg); QVERIFY(buffer.open(QIODevice::WriteOnly)); QVERIFY(image.save(&buffer, "jpg", 100));
    const QImage baseline = QImage::fromData(jpeg);
    const QVector<QVector<int>> order = {{0,1,2,3,4,5},{2,1,0,5,4,3},{5,4,3,2,1,0},{3,4,5,0,1,2},{0,3,1,4,2,5},{3,0,4,1,5,2},{5,2,4,1,3,0},{2,5,1,4,0,3}};
    for (int orientation = 1; orientation <= 8; ++orientation) {
        QByteArray metadata = QByteArray::fromHex("45786966000049492a000800000001001201030001000000");
        metadata.append(static_cast<char>(orientation)); metadata.append(QByteArray(7, '\0'));
        QByteArray encoded = QByteArray::fromHex("ffd8ffe1");
        const int length = metadata.size() + 2; encoded.append(static_cast<char>(length >> 8)); encoded.append(static_cast<char>(length & 255));
        encoded += metadata; encoded += jpeg.mid(2);
        const QString input = directory.filePath("orientation.jpg"), output = directory.filePath("normalized.png"); write(input, encoded);
        QString error; QVERIFY2(Backdrop::importWallpaper(input, output, &error), qPrintable(error));
        const QImage actual(output); const int width = orientation < 5 ? 3 : 2;
        QCOMPARE(actual.size(), orientation < 5 ? QSize(3,2) : QSize(2,3));
        for (int i = 0; i < 6; ++i) { const int source = order[orientation - 1][i]; QCOMPARE(actual.pixelColor(i % width, i / width), baseline.pixelColor(source % 3, source / 3)); }
    }
}

void NativeUiTests::tabNamesIdsAndExport() {
    QTemporaryDir directory; SettingsStore store(directory.path()); QString error; QVERIFY(store.load(&error));
    MainWindow window(&store, profiles(), nullptr, false); QCOMPARE(window.tabCount(), 1);
    const quint64 first = window.tabId(0); window.newTab("enhanced"); const quint64 second = window.tabId(1);
    QVERIFY(first != second); QVERIFY(window.renameTab(0, "  " + persian() + "\r\nname  "));
    const QString custom = persian() + " name"; QCOMPARE(window.tabTitle(0), custom);
    window.terminalAt(0)->feedForTest("\x1b]0;dynamic title\x07"); QCOMPARE(window.tabTitle(0), custom);
    window.resetTabName(0); QCOMPARE(window.tabTitle(0), QString("dynamic title"));
    QVERIFY(window.renameTab(0, "name")); QVERIFY(window.renameTab(1, "name")); QCOMPARE(window.tabTitle(1), QString("name 2"));
    QVERIFY(!window.renameTab(0, " \n ")); QCOMPARE(window.tabTitle(0), QString("name"));
    QVERIFY(window.renameTab(0, QString(100, 'x'))); QCOMPARE(window.tabTitle(0).size(), 64);
    const QString emoji = QString::fromUcs4(U"\U0001f680");
    QVERIFY(window.renameTab(0, emoji.repeated(70))); QCOMPARE(window.tabTitle(0), emoji.repeated(64));
    auto* terminal = window.terminalAt(1); terminal->feedForTest("\x1b[31mred\x1b[0m\r\n" + persian().toUtf8());
    const QString exportBase = directory.filePath(persian()); QVERIFY2(window.saveTabOutput(1, exportBase, &error), qPrintable(error));
    const QByteArray exported = read(exportBase + ".txt"); QVERIFY(exported.contains(persian().toUtf8())); QVERIFY(!exported.contains("\x1b")); QVERIFY(exported.endsWith('\n')); QVERIFY(!exported.startsWith(QByteArray::fromHex("efbbbf")));
    const QString blocked = directory.filePath("blocked.txt"); QVERIFY(QDir().mkdir(blocked)); QVERIFY(!window.saveTabOutput(1, blocked, &error)); QVERIFY(QFileInfo(blocked).isDir());
    QVERIFY(window.closeTab(0)); QCOMPARE(window.tabId(0), second); QCOMPARE(window.tabCount(), 1);
    window.newTab("native"); window.newTab("powershell"); QVERIFY(window.closeOtherTabs(0)); QCOMPARE(window.tabCount(), 1); QCOMPARE(window.tabId(0), second);
}

void NativeUiTests::tabMenuTargetsStableIdentity() {
    QTemporaryDir directory; SettingsStore store(directory.path()); QString error; QVERIFY(store.load(&error));
    MainWindow window(&store, profiles(), nullptr, false); window.newTab("enhanced"); window.newTab("powershell");
    const quint64 keep = window.tabId(2);
    std::unique_ptr<QMenu> menu(window.createTabMenu(1)); QVERIFY(action(menu.get(), "closeTabAction"));
    QVERIFY(window.closeTab(0)); action(menu.get(), "closeTabAction")->trigger(); QCOMPARE(window.tabCount(), 1); QCOMPARE(window.tabId(0), keep);
    QVERIFY(window.renameTab(0, "custom")); std::unique_ptr<QMenu> renamed(window.createTabMenu(0));
    QVERIFY(action(renamed.get(), "resetTabNameAction")); action(renamed.get(), "resetTabNameAction")->trigger(); QCOMPARE(window.tabTitle(0), QString("PowerShell"));
}

void NativeUiTests::quickCommandMenusAndPersistence() {
    QTemporaryDir directory; SettingsStore store(directory.path()); QString error; QVERIFY(store.load(&error)); QCOMPARE(store.quickCommands.size(), 28);
    MainWindow window(&store, profiles(), nullptr, false); std::unique_ptr<QMenu> menu(window.createQuickMenu());
    QVERIFY(action(menu.get(), "emptyFavoritesHint")); int commands = 0;
    for (auto* item : menu->findChildren<QAction*>()) if (item->objectName().startsWith("quick_")) { ++commands; QVERIFY(item->isEnabled()); }
    QCOMPARE(commands, 28); QVERIFY(action(menu.get(), "quick_flush-dns")->text().contains("(admin)"));
    QAction* pin = action(menu.get(), "pin_cwd"); QVERIFY(pin); pin->trigger(); QVERIFY(store.settings.pinned_commands.contains("cwd"));
    QVERIFY(store.addFavorite(persian(), "echo hello", &error));
    std::unique_ptr<QMenu> favorites(window.createQuickMenu()); QVERIFY(action(favorites.get(), "favorite_builtin_cwd"));
    QCOMPARE(action(favorites.get(), "favorite_custom_0")->text(), persian());
    action(favorites.get(), "removeFavorite_0")->trigger(); QVERIFY(store.settings.favorite_commands.isEmpty());
    SettingsStore reloaded(directory.path()); QVERIFY(reloaded.load(&error)); QVERIFY(reloaded.settings.pinned_commands.contains("cwd"));
    window.newTab("wsl"); std::unique_ptr<QMenu> unixMenu(window.createQuickMenu()); int disabled = 0;
    for (auto* item : unixMenu->findChildren<QAction*>()) if (item->objectName().startsWith("quick_") && !item->isEnabled()) ++disabled;
    QCOMPARE(disabled, 5); QVERIFY(!window.runQuickCommand("wifi-info")); QVERIFY(window.lastError().contains("not available"));
    QVERIFY(!window.runQuickCommand("cwd")); QVERIFY(!window.runCustomFavorite(999));
}

void NativeUiTests::physicalKeysAndButtons() {
    QTemporaryDir directory; SettingsStore store(directory.path()); QString error; QVERIFY(store.load(&error));
    MainWindow window(&store, profiles(), nullptr, false); window.show(); QTest::qWait(30);
    auto* plus = window.findChild<QToolButton*>("newTabButton"); QVERIFY(plus); QTest::mouseClick(plus, Qt::LeftButton); QCOMPARE(window.tabCount(), 2);
    QKeyEvent key(QEvent::KeyPress, 0, Qt::ControlModifier | Qt::ShiftModifier, 0x14, 'T', 0, persian().left(1));
    QApplication::sendEvent(window.terminalAt(window.currentIndex()), &key); QCOMPARE(window.tabCount(), 3);
    QKeyEvent first(QEvent::KeyPress, 0, Qt::ControlModifier, 0x02, '1', 0, QString(QChar(0x06f1)));
    QApplication::sendEvent(window.terminalAt(window.currentIndex()), &first); QCOMPARE(window.currentIndex(), 0);
    auto* settings = window.findChild<QToolButton*>("settingsButton"); QVERIFY(settings); QTest::mouseClick(settings, Qt::LeftButton);
    QTRY_VERIFY(window.findChild<SettingsDialog*>()); auto* dialog = window.findChild<SettingsDialog*>(); dialog->reject(); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(window.findChild<QToolButton*>("quickButton")); QVERIFY(window.findChild<QToolButton*>("minimizeButton")); QVERIFY(window.findChild<QToolButton*>("maximizeButton"));
}

void NativeUiTests::optionalVisualSnapshot() {
    const QString output = qEnvironmentVariable("N2_CMD_TEST_OUTPUT_DIR"); if (output.isEmpty()) QSKIP("Set N2_CMD_TEST_OUTPUT_DIR to collect UI screenshots.");
    QTemporaryDir directory; SettingsStore store(directory.path()); QString error; QVERIFY(store.load(&error));
    MainWindow window(&store, profiles(), nullptr, false); window.resize(1240, 780); window.show();
    window.terminalAt(0)->feedForTest("\x1b[32mn2-cmd\x1b[0m\r\nC:\\Workspace> " + persian().toUtf8() + "\r\nNative C/C++ terminal with Lua configuration\r\n");
    window.newTab("powershell"); window.setCurrentIndex(0); QTest::qWait(250); QVERIFY(QDir().mkpath(output));
    QVERIFY(window.grab().save(QDir(output).filePath("main-window.png")));
    window.openSettings(); QTest::qWait(100); auto* dialog = window.findChild<SettingsDialog*>(); QVERIFY(dialog);
    QVERIFY(dialog->grab().save(QDir(output).filePath("settings-dialog.png"))); dialog->reject();
}

QTEST_MAIN(NativeUiTests)
#include "ui_tests.moc"
