#include "settings_store.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QSet>

#include <iostream>
#include <limits>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace {
int checks = 0;
int failures = 0;

void check(bool passed, const char* expression, int line) {
    ++checks;
    if (!passed) {
        ++failures;
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
    }
}
#define CHECK(expression) check(static_cast<bool>(expression), #expression, __LINE__)

bool writeFixture(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

QByteArray readFixture(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}

void verifyCatalog() {
    QTemporaryDir temporary;
    CHECK(temporary.isValid());
    SettingsStore store(temporary.path());
    QString error;
    CHECK(store.load(&error));
    CHECK(error.isEmpty());
    CHECK(store.quickCommands.size() == 28);
    // Expected digests for this release; each covers key, label, category,
    // description, command mappings and the administrator flag.
    const struct { const char* key; const char* digest; } expected[] = {
        {"flush-dns", "1f18120ad782fa40db6941207ae54d0fb703ba95cc9dfa301f8c70e96175bb3e"},
        {"local-ip", "e007c5279e8a31e5049a8fa97496f52eecd58cbea0a7e3d2bfd07eb1c7fe58cb"},
        {"public-ip", "a114ccfcb53e98c5d793b30f6ed9db43e5776bcc8a329fcbfe2e03f53e928397"},
        {"dns-servers", "f0161a540e2143987b7014e72c3d81f915068bebd2a4508489b602d78614c558"},
        {"ping", "a4b3bc2ab315065485eea7225a3b7e24cfa65452572901110d201b2402a65e5e"},
        {"trace", "aaa91af4ec0dd41d9f7d33f9edb75eba73e6a53ec3663962b3743e4ab4ecbde3"},
        {"lookup", "c46ebd30dd280680b954a52f8885c8b0896b03d3590055f37d44f32bd327b339"},
        {"connections", "ca06db32b416fa2e8fc770329c0e3cc6be62a470c5e5c68d4fb6ef17410b2ca1"},
        {"listening", "010a42bb13641aeb062996141cc6f2f3f454a353866f4e34341ccfdd2eb6a4b4"},
        {"ipconfig-all", "151abc5c94d52bbc62b82c97a9add41a109f43975d1b073dccda72af81702645"},
        {"wifi-profiles", "82fdae9ae410428c295df994f5c93dd7489073c48e76e08f291541aeb9f129dc"},
        {"wifi-info", "179b23596c0320cd9fa42a8ff0a80b72ccc0d81aba21882cfad6530eba391871"},
        {"sysinfo", "8e36e1c2dba8258647af6e62912e950cb9dc7985210bf49433e4aa63e9bc86b7"},
        {"uptime", "9e82f918c1845276585bea41385e2a6dacff068f7f6260500ab804c5e40505f2"},
        {"disk", "1c00abfbb5761bc5bc08f35a4d1f8a49ee6de6c25aa986efe6ffe5734ccccdca"},
        {"processes", "0df89a44c1af78be71e59183ddbd63362a1c2e62e9484e6f1b1263f7e94a0ce3"},
        {"startup", "b7a22f5e9d05987e4c6f9df5d151659e676df750bc1fa9a6120b0a6f7bc8a5b9"},
        {"whoami", "bd64f82955cb4e50139491b093c7dc9f5eaecdaf80a3b8580a49a6b236d4cd29"},
        {"cwd", "e8a3f3b2b64fd13c6318ac4587f55da5d0d9aa092075703ed134413ecc0a7d2b"},
        {"open-folder", "e220d3267bdab3354db914370ce9502aa516574ef2fb06a634f4e894b18db9cb"},
        {"list-files", "9fb468048917951b941ccfda1e04eda6b1514b030797d95c168700b87601f930"},
        {"largest", "a50f8bf86a56249ee6096aecf7ee26f41ff06b21aef4d75a5f9da042cef9a44b"},
        {"downloads", "5ac09a7dda74681d36a519a3aedcbb7c4b4f0eb3d9a80b3cb520f1697190ddf3"},
        {"git-status", "93ea8e9e0685ee3926ef105d6455ccdced8c377d6db58d76201feb72e9a006f9"},
        {"git-log", "a5c4217913337d2b5b94443b8001e838bb9d332ad588a2599aaf192addd307f3"},
        {"docker-ps", "e2a2052202c192d3f1da6caa5c4b948deab8d74ccb16e1cc35b67d822b6a866e"},
        {"python-version", "9830d7f7dd4cae16e0ab6a1befc93a58be42a6e971acb3a6ef18eafd72b464a6"},
        {"node-version", "9b7b76a51a4fd8e20f50f829bb39aec57eb69bac294c60c17a85d4c795197968"},
    };
    for (int i = 0; i < 28; ++i) {
        const auto* item = store.quickCommand(QLatin1String(expected[i].key));
        CHECK(item != nullptr);
        if (!item) continue;
        CHECK(store.quickCommands[i].key == QLatin1String(expected[i].key));
        const QStringList fields{item->key, item->label, item->category, item->description,
            item->powershell, item->cmd, item->unix_command, item->admin ? QStringLiteral("1") : QStringLiteral("0")};
        const QByteArray digest = QCryptographicHash::hash(fields.join(QChar(31)).toUtf8(), QCryptographicHash::Sha256).toHex();
        CHECK(digest == expected[i].digest);
        for (const auto& profile : {"cmd", "native", "enhanced"})
            CHECK(item->commandFor(QLatin1String(profile)) == item->cmd);
        for (const auto& profile : {"powershell", "pwsh", "unknown"})
            CHECK(item->commandFor(QLatin1String(profile)) == item->powershell);
        for (const auto& profile : {"wsl", "wsl:Ubuntu", "bash"})
            CHECK(item->commandFor(QLatin1String(profile)) == item->unix_command);
    }
    CHECK(store.quickCommand(QStringLiteral("missing")) == nullptr);
    CHECK(store.quickCommand(QStringLiteral("wifi-info"))->commandFor(QStringLiteral("bash")).isEmpty());
}

void verifyPersistenceAndMutations() {
    QTemporaryDir temporary;
    CHECK(temporary.isValid());
    const QString persian = QStringLiteral("\u0633\u0644\u0627\u0645 \u062F\u0646\u06CC\u0627");
    const QString path = QDir(temporary.path()).filePath(persian);
    SettingsStore store(path);
    QString error;
    CHECK(store.load(&error));
    CHECK(store.settings.font_family == QStringLiteral("Vazir Code Hack"));
    CHECK(store.settings.font_size == 10.0);
    CHECK(store.settings.accent == QStringLiteral("#3ddc97"));
    CHECK(store.settings.default_shell == QStringLiteral("native"));
    store.settings.font_family = persian;
    store.settings.window_x = -1920;
    store.settings.window_y = -480;
    store.settings.copy_on_select = false;
    store.settings.snap_windows = false;
    CHECK(store.setPinned(QStringLiteral("git-status"), true, &error));
    CHECK(store.setPinned(QStringLiteral("git-status"), true, &error));
    CHECK(store.settings.pinned_commands.size() == 1);
    CHECK(!store.setPinned(QStringLiteral("missing"), true, &error));
    CHECK(!error.isEmpty());
    CHECK(store.addFavorite(QStringLiteral(" ") + persian + QStringLiteral(" "), QStringLiteral(" echo ") + persian, &error));
    const QByteArray saved = readFixture(store.settingsPath());
    CHECK(saved.contains(persian.toUtf8()));
    CHECK(!saved.startsWith("\xEF\xBB\xBF"));
    SettingsStore restored(path);
    CHECK(restored.load(&error));
    CHECK(restored.settings.font_family == persian);
    CHECK(restored.settings.window_x == -1920 && restored.settings.window_y == -480);
    CHECK(!restored.settings.copy_on_select);
    CHECK(!restored.settings.snap_windows);
    CHECK(restored.settings.favorite_commands.size() == 1);
    CHECK(restored.settings.favorite_commands.first().name == persian);
    CHECK(restored.settings.favorite_commands.first().command == QStringLiteral("echo ") + persian);
    CHECK(restored.settings.pinned_commands == QStringList{QStringLiteral("git-status")});
#ifdef Q_OS_WIN
    const HANDLE lockedFile = CreateFileW(reinterpret_cast<LPCWSTR>(restored.settingsPath().utf16()),
        GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    CHECK(lockedFile != INVALID_HANDLE_VALUE);
    if (lockedFile != INVALID_HANDLE_VALUE) {
        CHECK(!restored.addFavorite(QStringLiteral("locked"), QStringLiteral("echo safe"), &error));
        CHECK(!error.isEmpty());
        CHECK(restored.settings.favorite_commands.size() == 1);
        CHECK(!restored.removeFavorite(0, &error));
        CHECK(restored.settings.favorite_commands.size() == 1);
        CHECK(readFixture(restored.settingsPath()) == saved);
        CloseHandle(lockedFile);
    }
#endif
    CHECK(restored.setPinned(QStringLiteral("git-status"), false, &error));
    CHECK(restored.settings.pinned_commands.isEmpty());
    CHECK(!restored.addFavorite(QString(), QStringLiteral("echo ok"), &error));
    CHECK(!restored.addFavorite(QStringLiteral("empty"), QStringLiteral("  "), &error));
    CHECK(!restored.addFavorite(QString(65, QLatin1Char('x')), QStringLiteral("echo ok"), &error));
    CHECK(!restored.addFavorite(QStringLiteral("long"), QString(2001, QLatin1Char('x')), &error));
    CHECK(!restored.addFavorite(QStringLiteral("nul"), QStringLiteral("a") + QChar::Null + QStringLiteral("b"), &error));
    CHECK(restored.settings.favorite_commands.size() == 1);
    CHECK(restored.addFavorite(QString(64, QLatin1Char('x')), QString(2000, QLatin1Char('y')), &error));
    CHECK(!restored.removeFavorite(-1, &error));
    CHECK(!restored.removeFavorite(2, &error));
    CHECK(restored.removeFavorite(0, &error));
    CHECK(restored.settings.favorite_commands.size() == 1);
    CHECK(restored.settings.favorite_commands.first().name.size() == 64);
    for (int i = 1; i < 60; ++i) CHECK(restored.addFavorite(QString::number(i), QStringLiteral("echo safe"), &error));
    CHECK(!restored.addFavorite(QStringLiteral("overflow"), QStringLiteral("echo safe"), &error));
    CHECK(error.contains(QStringLiteral("60")));
    CHECK(restored.settings.favorite_commands.size() == 60);
    CHECK(restored.removeFavorite(59, &error));
    CHECK(restored.addFavorite(QStringLiteral("replacement"), QStringLiteral("echo safe"), &error));
    SettingsStore again(path);
    CHECK(again.load(&error));
    CHECK(again.settings.favorite_commands.size() == 60);
    CHECK(again.settings.favorite_commands.last().name == QStringLiteral("replacement"));

    for (int i = 0; i < 60; ++i) restored.settings.pinned_commands.append(QStringLiteral("pinned-%1").arg(i));
    CHECK(!restored.setPinned(QStringLiteral("git-status"), true, &error));
    CHECK(error.contains(QStringLiteral("60")));
    CHECK(restored.settings.pinned_commands.size() == 60);
    CHECK(restored.setPinned(QStringLiteral("pinned-0"), false, &error));
    CHECK(restored.setPinned(QStringLiteral("git-status"), true, &error));
    CHECK(restored.settings.pinned_commands.size() == 60);

    const QString blocked = QDir(temporary.path()).filePath(QStringLiteral("file-instead-of-directory"));
    CHECK(writeFixture(blocked, "unchanged"));
    SettingsStore blockedStore(blocked);
    CHECK(blockedStore.load(&error));
    CHECK(!blockedStore.addFavorite(QStringLiteral("test"), QStringLiteral("echo safe"), &error));
    CHECK(!error.isEmpty());
    CHECK(blockedStore.settings.favorite_commands.isEmpty());
    CHECK(!blockedStore.setPinned(QStringLiteral("git-status"), true, &error));
    CHECK(blockedStore.settings.pinned_commands.isEmpty());
    CHECK(readFixture(blocked) == "unchanged");

    SettingsStore unicodeLimits(QDir(temporary.path()).filePath(QStringLiteral("unicode-limits")));
    CHECK(unicodeLimits.load(&error));
    const QString emoji = QString::fromUcs4(U"\U0001F680");
    CHECK(unicodeLimits.addFavorite(emoji.repeated(64), emoji.repeated(2000), &error));
    CHECK(!unicodeLimits.addFavorite(emoji.repeated(65), QStringLiteral("echo safe"), &error));
    CHECK(!unicodeLimits.addFavorite(QStringLiteral("name"), emoji.repeated(2001), &error));
    CHECK(unicodeLimits.settings.favorite_commands.first().name.toUcs4().size() == 64);
}

void verifyRecoveryAndLua() {
    QTemporaryDir temporary;
    CHECK(temporary.isValid());
    SettingsStore store(temporary.path());
    QString error;
    CHECK(writeFixture(store.settingsPath(), "{ broken JSON"));
    CHECK(!store.load(&error));
    CHECK(!error.isEmpty());
    CHECK(store.quickCommands.size() == 28);
    CHECK(store.settings.font_size == 10.0);
    CHECK(readFixture(store.settingsPath()) == "{ broken JSON");
    CHECK(writeFixture(store.settingsPath(), "[]"));
    CHECK(!store.load(&error));
    CHECK(writeFixture(store.settingsPath(), QByteArray("{\"font_family\":\"") + QByteArray::fromHex("ff") + "\"}"));
    CHECK(!store.load(&error));
    CHECK(error.contains(QStringLiteral("UTF-8")));
    CHECK(writeFixture(store.settingsPath(), "{\"font_size\":12.5,\"copy_on_select\":\"false\",\"maximized\":null}"));
    const QString preferences = QDir(temporary.path()).filePath(QStringLiteral("preferences.lua"));
    const QString persian = QStringLiteral("\u0633\u0644\u0627\u0645");
    CHECK(writeFixture(preferences, (QStringLiteral("return { font_family = '") + persian +
        QStringLiteral("', font_size = 14, accent = '#ff6f91', copy_on_select = false, pinned_commands = {'cwd'}, favorite_commands = {{name = '") +
        persian + QStringLiteral("', command = 'echo hello'}} }")).toUtf8()));
    CHECK(store.load(&error));
    CHECK(store.settings.font_family == persian);
    CHECK(store.settings.font_size == 12.5);
    CHECK(store.settings.accent == QStringLiteral("#ff6f91"));
    CHECK(!store.settings.copy_on_select);
    CHECK(!store.settings.maximized);
    CHECK(store.settings.pinned_commands == QStringList{QStringLiteral("cwd")});
    CHECK(store.settings.favorite_commands.first().name == persian);
    CHECK(writeFixture(preferences, "return {font_size ="));
    CHECK(!store.load(&error));
    CHECK(error.contains(QStringLiteral("Lua")));
    CHECK(store.settings.font_size == 12.5);
    CHECK(writeFixture(preferences, "return 3"));
    CHECK(!store.load(&error));
    CHECK(error.contains(QStringLiteral("table")));
    CHECK(writeFixture(preferences, "while true do end"));
    CHECK(!store.load(&error));
    CHECK(error.contains(QStringLiteral("instructions")));
    CHECK(writeFixture(preferences, "local s='x'; for i=1,25 do s=s..s end; return {font_family=s}"));
    CHECK(!store.load(&error));
    CHECK(error.contains(QStringLiteral("memory")));
    CHECK(writeFixture(preferences, "return os.execute('echo this must not execute')"));
    CHECK(!store.load(&error));
    CHECK(error.contains(QStringLiteral("Lua")));
    CHECK(writeFixture(preferences, "local t = {}; for i=1,10000 do t[i] = i end; return { font_size = 13 }"));
    CHECK(store.load(&error));
    CHECK(writeFixture(preferences, QByteArray("\xEF\xBB\xBF") + "return { font_family = 'Unicode BOM' }"));
    CHECK(store.load(&error));
    CHECK(store.settings.font_family == QStringLiteral("Unicode BOM"));

    QJsonArray tooMany;
    for (int i = 0; i < 61; ++i) tooMany.append(QJsonObject{{QStringLiteral("name"), QString::number(i)},
        {QStringLiteral("command"), QStringLiteral("echo safe")}});
    CHECK(writeFixture(store.settingsPath(), QJsonDocument(QJsonObject{{QStringLiteral("favorite_commands"), tooMany}}).toJson()));
    CHECK(!store.load(&error));
    CHECK(error.contains(QStringLiteral("60")));
    CHECK(store.settings.favorite_commands.size() == 60);
    CHECK(QJsonDocument::fromJson(readFixture(store.settingsPath())).object()
        .value(QStringLiteral("favorite_commands")).toArray().size() == 61);
}

void verifyRangesResetAndProfiles() {
    QTemporaryDir temporary;
    SettingsStore store(temporary.path());
    QString error;
    CHECK(store.load(&error));
    store.settings.opacity = -10;
    store.settings.bg_dim = 9;
    store.settings.bg_blur = 500;
    store.settings.terminal_tint = -1;
    store.settings.font_size = 100;
    store.settings.scrollback = 1;
    store.settings.window_width = 1;
    store.settings.window_height = 1;
    store.settings.window_x = -120;
    store.settings.window_y = -42;
    store.settings.accent = QStringLiteral("invalid");
    store.settings.font_family = QStringLiteral("  ");
    store.settings.default_shell.clear();
    store.settings.pinned_commands = {QStringLiteral("cwd"), QStringLiteral(" cwd "), QString()};
    store.sanitize();
    CHECK(store.settings.opacity == 0.30);
    CHECK(store.settings.bg_dim == 0.92);
    CHECK(store.settings.bg_blur == 40);
    CHECK(store.settings.terminal_tint == 0.0);
    CHECK(store.settings.font_size == 26.0);
    CHECK(store.settings.scrollback == 500);
    CHECK(store.settings.window_width == 640 && store.settings.window_height == 400);
    CHECK(store.settings.window_x == -120 && store.settings.window_y == -42);
    CHECK(store.settings.accent == QStringLiteral("#3ddc97"));
    CHECK(store.settings.font_family == QStringLiteral("Vazir Code Hack"));
    CHECK(store.settings.default_shell == QStringLiteral("native"));
    CHECK(store.settings.pinned_commands == QStringList{QStringLiteral("cwd")});
    store.settings.opacity = std::numeric_limits<double>::infinity();
    store.settings.bg_dim = std::numeric_limits<double>::quiet_NaN();
    store.settings.font_size = -std::numeric_limits<double>::infinity();
    store.sanitize();
    CHECK(store.settings.opacity == 0.94 && store.settings.bg_dim == 0.58 && store.settings.font_size == 10.0);
    store.settings.default_shell = QStringLiteral("pwsh");
    store.settings.copy_on_select = false;
    store.settings.favorite_commands = {{QStringLiteral("name"), QStringLiteral("echo safe")}};
    store.settings.font_size = 25;
    store.settings.background = QStringLiteral("example.png");
    store.resetAppearance();
    CHECK(store.settings.font_size == 10 && store.settings.background.isEmpty());
    CHECK(store.settings.default_shell == QStringLiteral("pwsh") && !store.settings.copy_on_select);
    CHECK(store.settings.favorite_commands.size() == 1 && store.settings.pinned_commands.size() == 1);
    CHECK(store.settings.window_x == -120 && store.settings.scrollback == 500);
    CHECK(store.assetPath(QStringLiteral("../unsafe")).isEmpty());
    const auto native = ShellProfile{QStringLiteral("native"), QStringLiteral("CMD"), QStringLiteral("cmd.exe"), {}, {}};
    const auto pwsh = ShellProfile{QStringLiteral("pwsh"), QStringLiteral("PS7"), QStringLiteral("pwsh.exe"), {}, {}};
    CHECK(pickProfile({pwsh, native}, QStringLiteral("cmd")).key == QStringLiteral("native"));
    CHECK(pickProfile({pwsh, native}, QStringLiteral("missing")).key == QStringLiteral("native"));
    CHECK(pickProfile({pwsh, native}, QStringLiteral("pwsh")).key == QStringLiteral("pwsh"));
    CHECK(pickProfile({}, QStringLiteral("missing")).executable.isEmpty());

    const QByteArray previous = qgetenv("N2_CMD_DATA_DIR");
    qputenv("N2_CMD_DATA_DIR", temporary.path().toUtf8());
    CHECK(SettingsStore().dataDirectory() == temporary.path());
    CHECK(SettingsStore(QDir(temporary.path()).filePath(QStringLiteral("explicit"))).dataDirectory().endsWith(QStringLiteral("explicit")));
    if (previous.isNull()) qunsetenv("N2_CMD_DATA_DIR");
    else qputenv("N2_CMD_DATA_DIR", previous);
}
} // namespace

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("n2-cmd-tests"));
    QCoreApplication::setApplicationName(QStringLiteral("settings-tests"));
    if (application.arguments().contains(QStringLiteral("--profiles"))) {
        QJsonArray result;
        QSet<QString> keys;
        const auto profiles = detectProfiles();
        CHECK(!profiles.isEmpty());
        for (const auto& profile : profiles) {
            CHECK(!keys.contains(profile.key));
            CHECK(QFileInfo(profile.executable).isFile());
            keys.insert(profile.key);
            QJsonArray arguments;
            for (const auto& argument : profile.arguments) arguments.append(argument);
            result.append(QJsonObject{{QStringLiteral("key"), profile.key},
                {QStringLiteral("label"), profile.label}, {QStringLiteral("executable"), profile.executable},
                {QStringLiteral("arguments"), arguments}});
        }
        std::cout << QJsonDocument(result).toJson(QJsonDocument::Indented).constData();
        return failures == 0 ? 0 : 1;
    }
    verifyCatalog();
    verifyPersistenceAndMutations();
    verifyRecoveryAndLua();
    verifyRangesResetAndProfiles();
    std::cout << "settings_tests: " << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
