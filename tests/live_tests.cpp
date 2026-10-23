#include "main_window.h"
#include "terminal_view.h"
#include "settings_store.h"
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QMimeData>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

namespace {
QByteArray readFile(const QString& path) { QFile file(path); if (!file.open(QIODevice::ReadOnly)) return {}; return file.readAll(); }
QVector<ShellProfile> nativeProfiles() {
    return {{"native", "Command Prompt", QDir::toNativeSeparators(qEnvironmentVariable("SystemRoot", "C:/Windows") + "/System32/cmd.exe"), {"/d", "/q"}, {}}};
}
QString nativePath(const QString& path) { return QDir::toNativeSeparators(path); }
}

class LiveTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { QVERIFY(QFontDatabase::addApplicationFont(":/fonts/Vazir-Code-Hack.ttf") >= 0); }
    void commandsTabsClipboardAndRestart() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        SettingsStore store(directory.path()); QString error; QVERIFY2(store.load(&error), qPrintable(error));
        MainWindow window(&store, nativeProfiles()); window.show();
        auto* first = window.terminalAt(0);
        QTRY_VERIFY_WITH_TIMEOUT(first->isRunning(), 10000);
        const QString sample = QStringLiteral("N2_LIVE_سلام_می‌روم_😀");
        const QString savedPath = directory.filePath(QStringLiteral("خروجی shell.txt"));
        QVERIFY(first->runCommand("echo " + sample + ">\"" + nativePath(savedPath) + '"'));
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(savedPath), 10000);
        QCOMPARE(QString::fromUtf8(readFile(savedPath)), sample + "\r\n");
        QVERIFY(first->runCommand("echo " + sample));
        QTRY_VERIFY_WITH_TIMEOUT(first->plainText().contains(sample), 10000);

        const QString favoritePath = directory.filePath("favorite-proof.txt");
        QVERIFY(store.addFavorite("Proof", "echo FAVORITE>\"" + nativePath(favoritePath) + '"', &error));
        QVERIFY(window.runCustomFavorite(0));
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(favoritePath), 10000);
        QCOMPARE(readFile(favoritePath), QByteArray("FAVORITE\r\n"));
        QVERIFY(window.runQuickCommand("cwd"));

        const quint64 firstId = window.tabId(0);
        const QString customName = QStringLiteral("ترمینال اصلی");
        QVERIFY(window.renameTab(0, customName));
        QSignalSpy changedTitle(first, &TerminalView::titleChanged);
        first->feedForTest("\x1b]0;N2_DYNAMIC_TITLE\x07");
        QTRY_VERIFY_WITH_TIMEOUT(changedTitle.size() > 0, 10000);
        QCOMPARE(window.tabTitle(0), customName);
        window.resetTabName(0);
        QCOMPARE(window.tabTitle(0), QString("N2_DYNAMIC_TITLE"));

        const QString exportPath = directory.filePath(QStringLiteral("متن ترمینال.txt"));
        QVERIFY2(window.saveTabOutput(0, exportPath, &error), qPrintable(error));
        QVERIFY(QString::fromUtf8(readFile(exportPath)).contains(sample));
        auto* previousClipboard = new QMimeData;
        const auto* clipboard = QApplication::clipboard()->mimeData();
        if (clipboard) for (const QString& format : clipboard->formats()) previousClipboard->setData(format, clipboard->data(format));
        first->selectAll();
        const bool copied = first->copySelection();
        const QString actualClipboard = QApplication::clipboard()->text();
        QApplication::clipboard()->setMimeData(previousClipboard);
        QVERIFY(copied); QVERIFY(actualClipboard.contains(sample));
        first->clearSelection();

        QCOMPARE(window.newTab("native"), 1);
        auto* second = window.terminalAt(1);
        QTRY_VERIFY_WITH_TIMEOUT(second->isRunning(), 10000);
        QVERIFY(second->runCommand("echo N2_SECOND_TAB"));
        QTRY_VERIFY_WITH_TIMEOUT(second->plainText().contains("N2_SECOND_TAB"), 10000);
        QVERIFY(!first->plainText().contains("N2_SECOND_TAB"));
        QVERIFY(second->runCommand("exit 3"));
        QTRY_VERIFY_WITH_TIMEOUT(!second->isRunning(), 10000);
        const quint64 secondId = window.tabId(1);
        QTest::keyClick(second, Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(second->isRunning(), 10000);
        QCOMPARE(window.tabId(1), secondId);
        QVERIFY(second->runCommand("echo N2_RESTARTED"));
        QTRY_VERIFY_WITH_TIMEOUT(second->plainText().contains("N2_RESTARTED"), 10000);
        QVERIFY(window.closeOtherTabs(0)); QCOMPARE(window.tabCount(), 1); QCOMPARE(window.tabId(0), firstId); QVERIFY(first->isRunning());
    }
    void mixedLongTextReflowAndWindowStates() {
        QTemporaryDir directory; SettingsStore store(directory.path()); QString error; QVERIFY(store.load(&error));
        MainWindow window(&store, nativeProfiles()); window.resize(1200, 780); window.show();
        auto* view = window.terminalAt(0); QTRY_VERIFY_WITH_TIMEOUT(view->isRunning(), 10000);
        const QString text = QStringLiteral("سلام0123456789_").repeated(36);
        QVERIFY(view->runCommand("echo " + text));
        QTRY_VERIFY_WITH_TIMEOUT(view->plainText().contains(text), 10000);
        for (double size : {20.0, 26.0, 10.0}) {
            store.settings.font_size = size; window.applySettings(); QTest::qWait(200);
            QVERIFY2(view->plainText().contains(text), qPrintable(QString("Text lost at font size %1").arg(size)));
        }
        window.toggleMaximized(); QTRY_VERIFY(window.isMaximized());
        window.toggleMaximized(); QTRY_VERIFY(!window.isMaximized());
        window.toggleFullscreen(); QTRY_VERIFY(window.isFullScreen());
        window.toggleFullscreen(); QTRY_VERIFY(!window.isFullScreen());
        QVERIFY(view->plainText().contains(text));
        const QString output = qEnvironmentVariable("N2_CMD_TEST_OUTPUT_DIR");
        if (!output.isEmpty()) { QDir().mkpath(output); QVERIFY(window.grab().save(QDir(output).filePath("live-terminal.png"))); }
    }
};
QTEST_MAIN(LiveTests)
#include "live_tests.moc"
