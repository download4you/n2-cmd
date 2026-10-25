#include "main_window.h"
#include "settings_store.h"
#include "themed_style.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QIcon>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QMessageBox>
#include <QStyleFactory>

#ifdef Q_OS_WIN
#include <windows.h>
namespace {
void setAppUserModelId() {
    using SetAppId = HRESULT(WINAPI*)(PCWSTR);
    HMODULE shell = GetModuleHandleW(L"shell32.dll");
    if (!shell) shell = LoadLibraryW(L"shell32.dll");
    if (!shell) return;
    auto setter = reinterpret_cast<SetAppId>(GetProcAddress(shell, "SetCurrentProcessExplicitAppUserModelID"));
    if (setter) setter(L"n2-cmd.terminal");
}
}
#endif

int main(int argc, char** argv) {
#ifdef Q_OS_WIN
    setAppUserModelId();
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
#endif
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("n2-cmd"));
    QApplication::setApplicationDisplayName(QStringLiteral("n2-cmd"));
    QApplication::setOrganizationName(QStringLiteral("n2-cmd"));
    QApplication::setApplicationVersion(QStringLiteral(N2_CMD_VERSION));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/assets/icon.png")));
    app.setStyle(new ThemedStyle(QStyleFactory::create(QStringLiteral("Fusion"))));
    QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/Vazir-Code-Hack.ttf"));
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("A native C/C++ and Lua terminal for Windows."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({QStringLiteral("data-dir"), QStringLiteral("Use this settings directory."), QStringLiteral("directory")});
    parser.addOption({QStringLiteral("profile"), QStringLiteral("Initial shell profile."), QStringLiteral("name")});
    parser.addOption({QStringLiteral("cwd"), QStringLiteral("Initial working directory."), QStringLiteral("directory")});
    parser.addOption({QStringLiteral("runtime-report"), QStringLiteral("Write runtime capability information and exit."), QStringLiteral("file")});
    parser.addOption({QStringLiteral("clink-directory"), QStringLiteral("Directory containing an optional Clink installation."), QStringLiteral("directory")});
    parser.addOption({QStringLiteral("clink-profile"), QStringLiteral("Use this optional Clink profile directory."), QStringLiteral("directory")});
    parser.process(app);
    if (parser.isSet(QStringLiteral("runtime-report"))) {
        QJsonArray formats;
        for (const QByteArray& format : QImageReader::supportedImageFormats()) formats.append(QString::fromLatin1(format));
        const QJsonObject report{{"application_version", QApplication::applicationVersion()},
                                 {"qt_version", QString::fromLatin1(qVersion())}, {"image_formats", formats}};
        QSaveFile file(parser.value(QStringLiteral("runtime-report")));
        const QByteArray bytes = QJsonDocument(report).toJson();
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) return 1;
        return 0;
    }
    if (parser.isSet(QStringLiteral("cwd"))) QDir::setCurrent(parser.value(QStringLiteral("cwd")));
    if (parser.isSet(QStringLiteral("clink-directory"))) qputenv("N2_CMD_CLINK_DIR", parser.value(QStringLiteral("clink-directory")).toUtf8());
    if (parser.isSet(QStringLiteral("clink-profile"))) qputenv("N2_CMD_CLINK_PROFILE", parser.value(QStringLiteral("clink-profile")).toUtf8());
    SettingsStore settings(parser.value(QStringLiteral("data-dir")));
    QString error;
    settings.load(&error);
    if (!error.isEmpty()) qWarning().noquote() << error;
    if (parser.isSet(QStringLiteral("profile"))) settings.settings.default_shell = parser.value(QStringLiteral("profile"));
    MainWindow window(&settings, detectProfiles());
    window.show();
    if (settings.settings.maximized) window.showMaximized();
    return app.exec();
}
