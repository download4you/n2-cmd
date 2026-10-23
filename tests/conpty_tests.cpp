#include "conpty_session.h"
#include <QSignalSpy>
#include <QTest>
#include <QDir>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QFile>
#include <QFileInfo>

class ConPtyTests : public QObject {
    Q_OBJECT
    ShellProfile cmd() const {
        return {QStringLiteral("native"), QStringLiteral("Command Prompt"),
                QDir::toNativeSeparators(qEnvironmentVariable("SystemRoot", "C:/Windows") + "/System32/cmd.exe"),
                {"/d", "/q", "/k", "chcp 65001>nul"}, {}};
    }
    void observe(ConPtySession& session, QByteArray& output) {
        connect(&session, &ConPtySession::outputReady, this, [&session, &output](const QByteArray& bytes) {
            output += bytes;
            if (bytes.contains("\x1b[6n")) session.write("\x1b[1;1R");
        });
    }
private slots:
    void unicodeCommandResizeAndExit() {
        ConPtySession session;
        QByteArray output;
        observe(session, output);
        QSignalSpy exited(&session, &ConPtySession::exited);
        QString error;
        QVERIFY2(session.start(cmd(), QDir::tempPath(), 80, 24, &error), qPrintable(error));
        QVERIFY(session.isRunning());
        QVERIFY(session.write("echo N2_READY\r"));
        QTRY_VERIFY_WITH_TIMEOUT(output.contains("N2_READY"), 10000);
        const QString sample = QStringLiteral("N2_UTF8_سلام_می‌روم");
        QVERIFY(session.write(("echo " + sample + '\r').toUtf8()));
        QTRY_VERIFY_WITH_TIMEOUT(output.contains(sample.toUtf8()), 10000);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = QDir::toNativeSeparators(directory.filePath(QStringLiteral("Persian سلام output.txt")));
        QVERIFY(session.write(("echo " + sample + ">\"" + path + "\"\r").toUtf8()));
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(path), 10000);
        QFile saved(path);
        QVERIFY(saved.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(saved.readAll()), sample + "\r\n");
        QVERIFY(session.resize(40, 18));
        QVERIFY(session.resize(120, 30));
        QVERIFY(session.write("exit 7\r"));
        QTRY_COMPARE_WITH_TIMEOUT(exited.size(), 1, 10000);
        QCOMPARE(exited.at(0).at(0).toInt(), 7);
        QVERIFY(!session.isRunning());
        QVERIFY(!session.write("ignored"));
    }
    void restartAndOwnedShutdown() {
        ConPtySession session;
        QByteArray output;
        observe(session, output);
        QString error;
        for (int pass=0; pass<2; ++pass) {
            output.clear();
            QVERIFY2(session.start(cmd(), QDir::tempPath(), 80, 24, &error), qPrintable(error));
            const QByteArray marker = QByteArray("N2_PASS_") + QByteArray::number(pass);
            QVERIFY(session.write("echo " + marker + "\r"));
            QTRY_VERIFY_WITH_TIMEOUT(output.contains(marker), 10000);
            QElapsedTimer timer;
            timer.start();
            session.stop();
            QVERIFY2(timer.elapsed() < 5000, "Owned shutdown took too long");
            QVERIFY(!session.isRunning());
        }
    }
    void invalidShellAndDirectory() {
        ConPtySession session;
        QString error;
        ShellProfile profile = cmd();
        profile.executable = "C:/n2-cmd-NOT-PRESENT/missing.exe";
        QVERIFY(!session.start(profile, QDir::tempPath(), 80, 24, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!session.isRunning());
        error.clear();
        QVERIFY(!session.start(cmd(), "C:/n2-cmd-NOT-PRESENT", 80, 24, &error));
        QVERIFY(!error.isEmpty());
    }
};
QTEST_GUILESS_MAIN(ConPtyTests)
#include "conpty_tests.moc"
