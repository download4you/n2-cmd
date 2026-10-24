#include "settings_store.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QStringDecoder>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <memory>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

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
constexpr int kMaxEntries = 60;
constexpr int kMaxName = 64;
constexpr int kMaxCommand = 2000;
constexpr qint64 kMaxConfigBytes = 2 * 1024 * 1024;
constexpr size_t kLuaMemoryLimit = 8 * 1024 * 1024;

void fail(QString* error, const QString& message) {
    if (error) *error = message;
}

double bounded(double value, double low, double high, double fallback) {
    return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
}

QString limitedText(const QString& value, int count) {
    const auto points = value.toUcs4();
    return points.size() <= count ? value : QString::fromUcs4(reinterpret_cast<const char32_t*>(points.constData()), count);
}

int characterCount(const QString& value) {
    return static_cast<int>(value.toUcs4().size());
}

bool validText(const QString& text) {
    return !text.contains(QChar::Null);
}

void overlaySettings(Settings& settings, const QJsonObject& values) {
    const auto number = [&values](const char* key, double& destination) {
        if (values.value(QLatin1String(key)).isDouble())
            destination = values.value(QLatin1String(key)).toDouble();
    };
    const auto integer = [&values](const char* key, int& destination) {
        const QJsonValue value = values.value(QLatin1String(key));
        if (value.isDouble() && std::isfinite(value.toDouble())) {
            destination = static_cast<int>(std::clamp(value.toDouble(),
                static_cast<double>(std::numeric_limits<int>::min()),
                static_cast<double>(std::numeric_limits<int>::max())));
        }
    };
    const auto string = [&values](const char* key, QString& destination) {
        if (values.value(QLatin1String(key)).isString())
            destination = values.value(QLatin1String(key)).toString();
    };
    const auto boolean = [&values](const char* key, bool& destination) {
        if (values.value(QLatin1String(key)).isBool())
            destination = values.value(QLatin1String(key)).toBool();
    };
    number("opacity", settings.opacity);
    number("bg_dim", settings.bg_dim);
    number("terminal_tint", settings.terminal_tint);
    number("font_size", settings.font_size);
    integer("bg_blur", settings.bg_blur);
    integer("scrollback", settings.scrollback);
    integer("window_width", settings.window_width);
    integer("window_height", settings.window_height);
    integer("window_x", settings.window_x);
    integer("window_y", settings.window_y);
    string("background", settings.background);
    string("font_family", settings.font_family);
    string("default_shell", settings.default_shell);
    string("accent", settings.accent);
    boolean("copy_on_select", settings.copy_on_select);
    boolean("snap_windows", settings.snap_windows);
    boolean("opaque_background", settings.opaque_background);
    boolean("maximized", settings.maximized);
    if (values.value(QStringLiteral("pinned_commands")).isArray()) {
        settings.pinned_commands.clear();
        for (const auto& value : values.value(QStringLiteral("pinned_commands")).toArray()) {
            if (value.isString()) settings.pinned_commands.append(value.toString());
        }
    }
    if (values.value(QStringLiteral("favorite_commands")).isArray()) {
        settings.favorite_commands.clear();
        for (const auto& value : values.value(QStringLiteral("favorite_commands")).toArray()) {
            if (!value.isObject()) continue;
            const QJsonObject item = value.toObject();
            if (item.value(QStringLiteral("name")).isString()
                && item.value(QStringLiteral("command")).isString()) {
                settings.favorite_commands.append({item.value(QStringLiteral("name")).toString(),
                                                   item.value(QStringLiteral("command")).toString()});
            }
        }
    }
}

QJsonObject settingsObject(const Settings& settings) {
    QJsonArray pins;
    for (const auto& key : settings.pinned_commands) pins.append(key);
    QJsonArray favorites;
    for (const auto& entry : settings.favorite_commands)
        favorites.append(QJsonObject{{QStringLiteral("name"), entry.name},
                                     {QStringLiteral("command"), entry.command}});
    return {
        {QStringLiteral("schema_version"), 1},
        {QStringLiteral("opacity"), settings.opacity},
        {QStringLiteral("background"), settings.background},
        {QStringLiteral("bg_dim"), settings.bg_dim},
        {QStringLiteral("bg_blur"), settings.bg_blur},
        {QStringLiteral("terminal_tint"), settings.terminal_tint},
        {QStringLiteral("font_family"), settings.font_family},
        {QStringLiteral("font_size"), settings.font_size},
        {QStringLiteral("copy_on_select"), settings.copy_on_select},
        {QStringLiteral("snap_windows"), settings.snap_windows},
        {QStringLiteral("opaque_background"), settings.opaque_background},
        {QStringLiteral("default_shell"), settings.default_shell},
        {QStringLiteral("pinned_commands"), pins},
        {QStringLiteral("favorite_commands"), favorites},
        {QStringLiteral("accent"), settings.accent},
        {QStringLiteral("scrollback"), settings.scrollback},
        {QStringLiteral("window_width"), settings.window_width},
        {QStringLiteral("window_height"), settings.window_height},
        {QStringLiteral("window_x"), settings.window_x},
        {QStringLiteral("window_y"), settings.window_y},
        {QStringLiteral("maximized"), settings.maximized},
    };
}

QString luaString(lua_State* state, int index) {
    if (lua_type(state, index) != LUA_TSTRING) return {};
    size_t length = 0;
    const char* text = lua_tolstring(state, index, &length);
    return QString::fromUtf8(text, static_cast<qsizetype>(length));
}

QString luaField(lua_State* state, const char* name) {
    lua_getfield(state, -1, name);
    const QString value = luaString(state, -1);
    lua_pop(state, 1);
    return value;
}

QJsonObject luaSettingsObject(lua_State* state) {
    QJsonObject values;
    const int table = lua_absindex(state, -1);
    constexpr const char* fields[] = {
        "opacity", "background", "bg_dim", "bg_blur", "terminal_tint", "font_family",
        "font_size", "copy_on_select", "snap_windows", "opaque_background", "default_shell", "accent", "scrollback",
        "window_width", "window_height", "window_x", "window_y", "maximized",
    };
    for (const char* field : fields) {
        lua_getfield(state, table, field);
        if (lua_type(state, -1) == LUA_TSTRING)
            values.insert(QLatin1String(field), luaString(state, -1));
        else if (lua_type(state, -1) == LUA_TNUMBER)
            values.insert(QLatin1String(field), lua_tonumber(state, -1));
        else if (lua_type(state, -1) == LUA_TBOOLEAN)
            values.insert(QLatin1String(field), lua_toboolean(state, -1) != 0);
        lua_pop(state, 1);
    }
    lua_getfield(state, table, "pinned_commands");
    if (lua_istable(state, -1)) {
        QJsonArray pins;
        const auto count = std::min<lua_Unsigned>(lua_rawlen(state, -1), kMaxEntries + 1);
        for (lua_Unsigned i = 1; i <= count; ++i) {
            lua_rawgeti(state, -1, i);
            if (lua_type(state, -1) == LUA_TSTRING) pins.append(luaString(state, -1));
            lua_pop(state, 1);
        }
        values.insert(QStringLiteral("pinned_commands"), pins);
    }
    lua_pop(state, 1);
    lua_getfield(state, table, "favorite_commands");
    if (lua_istable(state, -1)) {
        QJsonArray favorites;
        const auto count = std::min<lua_Unsigned>(lua_rawlen(state, -1), kMaxEntries + 1);
        for (lua_Unsigned i = 1; i <= count; ++i) {
            lua_rawgeti(state, -1, i);
            if (lua_istable(state, -1))
                favorites.append(QJsonObject{{QStringLiteral("name"), luaField(state, "name")},
                                             {QStringLiteral("command"), luaField(state, "command")}});
            lua_pop(state, 1);
        }
        values.insert(QStringLiteral("favorite_commands"), favorites);
    }
    lua_pop(state, 1);
    return values;
}

struct LuaMemory { size_t used = 0; };

void* luaAllocator(void* context, void* pointer, size_t oldSize, size_t newSize) {
    auto& memory = *static_cast<LuaMemory*>(context);
    if (!pointer) oldSize = 0;
    if (newSize == 0) {
        memory.used -= oldSize;
        std::free(pointer);
        return nullptr;
    }
    if (newSize > kLuaMemoryLimit - (memory.used - oldSize)) return nullptr;
    void* result = std::realloc(pointer, newSize);
    if (result) memory.used = memory.used - oldSize + newSize;
    return result;
}

void instructionLimit(lua_State* state, lua_Debug*) {
    luaL_error(state, "Lua configuration exceeded 100000 instructions");
}

bool readFile(const QString& path, QByteArray& bytes, QString* error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        fail(error, QStringLiteral("Cannot read %1: %2").arg(path, file.errorString()));
        return false;
    }
    if (file.size() > kMaxConfigBytes) {
        fail(error, QStringLiteral("Configuration exceeds the 2 MiB limit: %1").arg(path));
        return false;
    }
    bytes = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        fail(error, QStringLiteral("Cannot read %1: %2").arg(path, file.errorString()));
        return false;
    }
    if (bytes.startsWith("\xEF\xBB\xBF")) bytes.remove(0, 3);
    QStringDecoder decoder(QStringDecoder::Utf8);
    const QString decoded = decoder(bytes);
    Q_UNUSED(decoded);
    if (decoder.hasError()) {
        fail(error, QStringLiteral("Configuration must be valid UTF-8: %1").arg(path));
        return false;
    }
    return true;
}

template<typename Reader>
bool withLuaTable(const QString& path, Reader reader, QString* error) {
    QByteArray bytes;
    if (!readFile(path, bytes, error)) return false;
    LuaMemory memory;
    std::unique_ptr<lua_State, decltype(&lua_close)> state(lua_newstate(luaAllocator, &memory), lua_close);
    if (!state) {
        fail(error, QStringLiteral("Cannot allocate Lua configuration state."));
        return false;
    }
    // Configuration is declarative: no I/O, process, package, or debug libraries.
    lua_sethook(state.get(), instructionLimit, LUA_MASKCOUNT, 100000);
    const QByteArray name = path.toUtf8();
    if (luaL_loadbufferx(state.get(), bytes.constData(), static_cast<size_t>(bytes.size()),
                        name.constData(), "t") != LUA_OK
        || lua_pcall(state.get(), 0, 1, 0) != LUA_OK) {
        fail(error, QStringLiteral("Invalid Lua configuration %1: %2").arg(path, luaString(state.get(), -1)));
        return false;
    }
    if (!lua_istable(state.get(), -1)) {
        fail(error, QStringLiteral("Lua configuration must return a table: %1").arg(path));
        return false;
    }
    return reader(state.get(), error);
}

bool validateCollections(const Settings& settings, QString* error) {
    if (settings.pinned_commands.size() > kMaxEntries) {
        fail(error, QStringLiteral("You can pin at most 60 commands. Unpin one before adding another."));
        return false;
    }
    if (settings.favorite_commands.size() > kMaxEntries) {
        fail(error, QStringLiteral("You can save at most 60 favorites. Remove one before adding another."));
        return false;
    }
    for (const auto& favorite : settings.favorite_commands) {
        if (characterCount(favorite.name.trimmed()) > kMaxName
            || characterCount(favorite.command.trimmed()) > kMaxCommand) {
            fail(error, QStringLiteral("Favorite names are limited to 64 characters and commands to 2000 characters."));
            return false;
        }
    }
    return true;
}

QString existingExecutable(const QStringList& candidates) {
    for (const auto& candidate : candidates) {
        const QString found = QFileInfo(candidate).isAbsolute()
            ? (QFileInfo(candidate).isFile() ? candidate : QString())
            : QStandardPaths::findExecutable(candidate);
        if (!found.isEmpty()) return QDir::toNativeSeparators(QFileInfo(found).absoluteFilePath());
    }
    return {};
}

QString decodeWslOutput(QByteArray bytes) {
    const bool utf16 = bytes.startsWith("\xFF\xFE") || bytes.contains('\0');
    QStringDecoder decoder(utf16 ? QStringDecoder::Utf16LE : QStringDecoder::Utf8);
    QString text = decoder(bytes);
    text.remove(QChar::ByteOrderMark);
    text.remove(QChar::Null);
    return text;
}
} // namespace

QString QuickCommand::commandFor(const QString& profileKey) const {
    if (profileKey == QStringLiteral("native") || profileKey == QStringLiteral("enhanced")
        || profileKey == QStringLiteral("cmd")) return cmd;
    if (profileKey == QStringLiteral("wsl") || profileKey.startsWith(QStringLiteral("wsl:"))
        || profileKey == QStringLiteral("bash")) return unix_command;
    return powershell;
}

SettingsStore::SettingsStore(QString dataDirectory) {
    if (dataDirectory.isEmpty()) dataDirectory = qEnvironmentVariable("N2_CMD_DATA_DIR");
    if (dataDirectory.isEmpty()) dataDirectory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (dataDirectory.isEmpty()) dataDirectory = QDir::home().filePath(QStringLiteral(".n2-cmd"));
    directory_ = QDir::cleanPath(QFileInfo(dataDirectory).absoluteFilePath());
}

QString SettingsStore::dataDirectory() const { return directory_; }
QString SettingsStore::settingsPath() const { return QDir(directory_).filePath(QStringLiteral("settings.json")); }

QString SettingsStore::assetPath(const QString& name) const {
    if (name.isEmpty() || QFileInfo(name).fileName() != name) return {};
    const QString installed = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("assets/") + name);
    if (QFileInfo(installed).isFile()) return installed;
    return QStringLiteral(":/assets/") + name;
}

bool SettingsStore::loadLua(QString* error) {
    QStringList errors;
    QString problem;
    if (!withLuaTable(QStringLiteral(":/config/defaults.lua"), [this](lua_State* state, QString*) {
            overlaySettings(settings, luaSettingsObject(state));
            return true;
        }, &problem)) errors.append(problem);

    if (!withLuaTable(QStringLiteral(":/config/quick_commands.lua"), [this](lua_State* state, QString* reason) {
            QVector<QuickCommand> commands;
            QSet<QString> keys;
            const lua_Unsigned count = lua_rawlen(state, -1);
            if (count == 0 || count > 256) {
                fail(reason, QStringLiteral("The quick-command catalog must contain 1 to 256 commands."));
                return false;
            }
            for (lua_Unsigned i = 1; i <= count; ++i) {
                lua_rawgeti(state, -1, i);
                if (!lua_istable(state, -1)) {
                    fail(reason, QStringLiteral("Invalid quick-command catalog entry %1.").arg(i));
                    return false;
                }
                QuickCommand command;
                command.key = luaField(state, "key");
                command.label = luaField(state, "label");
                command.category = luaField(state, "category");
                command.description = luaField(state, "description");
                command.powershell = luaField(state, "powershell");
                command.cmd = luaField(state, "cmd");
                command.unix_command = luaField(state, "unix");
                lua_getfield(state, -1, "admin");
                command.admin = lua_toboolean(state, -1) != 0;
                lua_pop(state, 2);
                if (command.key.isEmpty() || command.label.isEmpty() || command.category.isEmpty()
                    || command.description.isEmpty() || command.powershell.isEmpty() || keys.contains(command.key)) {
                    fail(reason, QStringLiteral("Quick-command catalog contains an incomplete or duplicate entry."));
                    return false;
                }
                keys.insert(command.key);
                commands.append(command);
            }
            quickCommands = std::move(commands);
            return true;
        }, &problem)) errors.append(problem);

    const QString preferences = QDir(directory_).filePath(QStringLiteral("preferences.lua"));
    if (QFileInfo::exists(preferences)
        && !withLuaTable(preferences, [this](lua_State* state, QString*) {
            overlaySettings(settings, luaSettingsObject(state));
            return true;
        }, &problem)) errors.append(problem);
    fail(error, errors.join(QLatin1Char('\n')));
    return errors.isEmpty();
}

bool SettingsStore::load(QString* error) {
    settings = Settings{};
    quickCommands.clear();
    QStringList errors;
    QString problem;
    if (!loadLua(&problem)) errors.append(problem);
    if (QFileInfo::exists(settingsPath())) {
        QByteArray bytes;
        if (!readFile(settingsPath(), bytes, &problem)) errors.append(problem);
        else {
            QJsonParseError parseError;
            const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
            if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
                errors.append(QStringLiteral("Invalid settings JSON in %1: %2. Defaults remain available; the file was not changed.")
                    .arg(settingsPath(), parseError.error == QJsonParseError::NoError
                        ? QStringLiteral("expected an object") : parseError.errorString()));
            } else overlaySettings(settings, document.object());
        }
    }
    if (!validateCollections(settings, &problem)) errors.append(problem);
    sanitize();
    fail(error, errors.join(QLatin1Char('\n')));
    return errors.isEmpty();
}

void SettingsStore::sanitize() {
    const Settings defaults;
    settings.opacity = bounded(settings.opacity, 0.30, 1.0, defaults.opacity);
    settings.bg_dim = bounded(settings.bg_dim, 0.0, 0.92, defaults.bg_dim);
    settings.bg_blur = std::clamp(settings.bg_blur, 0, 40);
    settings.terminal_tint = bounded(settings.terminal_tint, 0.0, 0.88, defaults.terminal_tint);
    settings.font_size = bounded(settings.font_size, 7.5, 26.0, defaults.font_size);
    settings.scrollback = std::clamp(settings.scrollback, 500, 50000);
    settings.window_width = std::clamp(settings.window_width, 640, 10000);
    settings.window_height = std::clamp(settings.window_height, 400, 10000);
    settings.window_x = std::clamp(settings.window_x, -100000, 100000);
    settings.window_y = std::clamp(settings.window_y, -100000, 100000);
    if (!validText(settings.background)) settings.background.clear();
    settings.font_family = settings.font_family.trimmed();
    if (settings.font_family.isEmpty() || !validText(settings.font_family)) settings.font_family = defaults.font_family;
    settings.default_shell = settings.default_shell.trimmed();
    if (settings.default_shell.isEmpty() || !validText(settings.default_shell)) settings.default_shell = defaults.default_shell;
    const QStringList accents{QStringLiteral("#4cc9f0"), QStringLiteral("#9b8cff"), QStringLiteral("#3ddc97"),
                              QStringLiteral("#ffb454"), QStringLiteral("#ff6f91")};
    settings.accent = settings.accent.toLower();
    if (!accents.contains(settings.accent)) settings.accent = defaults.accent;
    QStringList pins;
    QSet<QString> uniquePins;
    for (const auto& item : settings.pinned_commands) {
        const QString key = item.trimmed();
        if (!key.isEmpty() && validText(key) && !uniquePins.contains(key)) {
            pins.append(key);
            uniquePins.insert(key);
            if (pins.size() == kMaxEntries) break;
        }
    }
    settings.pinned_commands = pins;
    QVector<FavoriteCommand> favorites;
    for (const auto& item : settings.favorite_commands) {
        const QString name = limitedText(item.name.trimmed(), kMaxName);
        const QString command = limitedText(item.command.trimmed(), kMaxCommand);
        if (!name.isEmpty() && !command.isEmpty() && validText(name) && validText(command)) {
            favorites.append({name, command});
            if (favorites.size() == kMaxEntries) break;
        }
    }
    settings.favorite_commands = favorites;
}

bool SettingsStore::save(QString* error) {
    fail(error, {});
    if (!validateCollections(settings, error)) return false;
    sanitize();
    if (!QDir().mkpath(directory_)) {
        fail(error, QStringLiteral("Cannot create settings directory: %1").arg(directory_));
        return false;
    }
    QSaveFile file(settingsPath());
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) {
        fail(error, QStringLiteral("Cannot write settings: %1").arg(file.errorString()));
        return false;
    }
    const QByteArray bytes = QJsonDocument(settingsObject(settings)).toJson(QJsonDocument::Indented);
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        fail(error, QStringLiteral("Cannot commit settings: %1").arg(file.errorString()));
        return false;
    }
    return true;
}

void SettingsStore::resetAppearance() {
    const Settings defaults;
    settings.opacity = defaults.opacity;
    settings.opaque_background = defaults.opaque_background;
    settings.background = defaults.background;
    settings.bg_dim = defaults.bg_dim;
    settings.bg_blur = defaults.bg_blur;
    settings.terminal_tint = defaults.terminal_tint;
    settings.font_family = defaults.font_family;
    settings.font_size = defaults.font_size;
    settings.accent = defaults.accent;
}

const QuickCommand* SettingsStore::quickCommand(const QString& key) const {
    for (const auto& command : quickCommands) if (command.key == key) return &command;
    return nullptr;
}

bool SettingsStore::setPinned(const QString& key, bool pinned, QString* error) {
    fail(error, {});
    const QString normalized = key.trimmed();
    if (pinned && !quickCommand(normalized)) {
        fail(error, QStringLiteral("Unknown built-in command: %1").arg(normalized));
        return false;
    }
    const bool present = settings.pinned_commands.contains(normalized);
    if (pinned == present) return true;
    if (pinned && settings.pinned_commands.size() >= kMaxEntries) {
        fail(error, QStringLiteral("You can pin at most 60 commands. Unpin one before adding another."));
        return false;
    }
    const QStringList previous = settings.pinned_commands;
    if (pinned) settings.pinned_commands.append(normalized);
    else settings.pinned_commands.removeAll(normalized);
    if (save(error)) return true;
    settings.pinned_commands = previous;
    return false;
}

bool SettingsStore::addFavorite(const QString& name, const QString& command, QString* error) {
    fail(error, {});
    const FavoriteCommand entry{name.trimmed(), command.trimmed()};
    if (entry.name.isEmpty() || entry.command.isEmpty() || !validText(entry.name) || !validText(entry.command)) {
        fail(error, QStringLiteral("Enter a non-empty favorite name and command without NUL characters."));
        return false;
    }
    if (characterCount(entry.name) > kMaxName || characterCount(entry.command) > kMaxCommand) {
        fail(error, QStringLiteral("Favorite names are limited to 64 characters and commands to 2000 characters."));
        return false;
    }
    if (settings.favorite_commands.size() >= kMaxEntries) {
        fail(error, QStringLiteral("You can save at most 60 favorites. Remove one before adding another."));
        return false;
    }
    const auto previous = settings.favorite_commands;
    settings.favorite_commands.append(entry);
    if (save(error)) return true;
    settings.favorite_commands = previous;
    return false;
}

bool SettingsStore::removeFavorite(int index, QString* error) {
    fail(error, {});
    if (index < 0 || index >= settings.favorite_commands.size()) {
        fail(error, QStringLiteral("That favorite no longer exists."));
        return false;
    }
    const auto previous = settings.favorite_commands;
    settings.favorite_commands.removeAt(index);
    if (save(error)) return true;
    settings.favorite_commands = previous;
    return false;
}

QVector<ShellProfile> detectProfiles() {
    QVector<ShellProfile> profiles;
    const QString root = qEnvironmentVariable("SystemRoot", QStringLiteral("C:/Windows"));
    const QString cmd = existingExecutable({qEnvironmentVariable("ComSpec"), root + QStringLiteral("/System32/cmd.exe"),
                                            QStringLiteral("cmd.exe")});
    if (!cmd.isEmpty()) {
        profiles.append({QStringLiteral("native"), QStringLiteral("Command Prompt"), cmd,
                         {QStringLiteral("/D")}, QStringLiteral("Native Windows cmd.exe")});
        const QString app = QCoreApplication::applicationDirPath();
        const QString clinkDirectory = qEnvironmentVariable("N2_CMD_CLINK_DIR");
        const QString clink = existingExecutable({clinkDirectory.isEmpty() ? QString{} : clinkDirectory + QStringLiteral("/clink_x64.exe"),
            app + QStringLiteral("/runtime/clink/clink_x64.exe"),
            app + QStringLiteral("/clink/clink_x64.exe"), app + QStringLiteral("/runtime/clink/clink.bat"),
            QStringLiteral("clink_x64.exe"), QStringLiteral("clink.exe")});
        if (!clink.isEmpty()) {
            QString inject = QStringLiteral("chcp 65001>nul & \"%1\" inject --quiet --nolog").arg(QDir::toNativeSeparators(clink));
            const QString clinkProfile = qEnvironmentVariable("N2_CMD_CLINK_PROFILE");
            if (!clinkProfile.isEmpty()) inject += QStringLiteral(" --profile \"%1\"").arg(QDir::toNativeSeparators(clinkProfile));
            profiles.append({QStringLiteral("enhanced"), QStringLiteral("Enhanced CMD (Clink)"), cmd,
                             {QStringLiteral("/D"), QStringLiteral("/K"), inject},
                             QStringLiteral("Command Prompt with optional Clink completion and history")});
        }
    }
    const QString pwsh = existingExecutable({QStringLiteral("pwsh.exe"), QStringLiteral("pwsh"),
        qEnvironmentVariable("ProgramFiles") + QStringLiteral("/PowerShell/7/pwsh.exe")});
    if (!pwsh.isEmpty()) profiles.append({QStringLiteral("pwsh"), QStringLiteral("PowerShell 7"), pwsh,
                                         {QStringLiteral("-NoLogo")}, QStringLiteral("Modern PowerShell")});
    const QString powershell = existingExecutable({root + QStringLiteral("/System32/WindowsPowerShell/v1.0/powershell.exe"),
                                                   QStringLiteral("powershell.exe")});
    if (!powershell.isEmpty()) profiles.append({QStringLiteral("powershell"), QStringLiteral("Windows PowerShell"), powershell,
                                               {QStringLiteral("-NoLogo")}, QStringLiteral("Windows PowerShell 5.1")});

    QString bash = existingExecutable({qEnvironmentVariable("ProgramFiles") + QStringLiteral("/Git/bin/bash.exe"),
        qEnvironmentVariable("ProgramFiles(x86)") + QStringLiteral("/Git/bin/bash.exe"),
        qEnvironmentVariable("LOCALAPPDATA") + QStringLiteral("/Programs/Git/bin/bash.exe"), QStringLiteral("bash.exe")});
    if (QDir::fromNativeSeparators(bash).contains(QStringLiteral("/Windows/System32/"), Qt::CaseInsensitive)) bash.clear();
    if (!bash.isEmpty()) profiles.append({QStringLiteral("bash"), QStringLiteral("Git Bash"), bash,
                                         {QStringLiteral("--login"), QStringLiteral("-i")}, QStringLiteral("Git for Windows Bash")});

    const QString wsl = existingExecutable({root + QStringLiteral("/System32/wsl.exe"), QStringLiteral("wsl.exe")});
    if (!wsl.isEmpty()) {
        QProcess process;
#ifdef Q_OS_WIN
        process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) {
            args->flags |= CREATE_NO_WINDOW;
        });
#endif
        process.start(wsl, {QStringLiteral("--list"), QStringLiteral("--quiet")});
        if (process.waitForStarted(1000) && process.waitForFinished(4000)
            && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0) {
            const QStringList names = decodeWslOutput(process.readAllStandardOutput()).split(
                QRegularExpression(QStringLiteral("[\r\n]+")), Qt::SkipEmptyParts);
            QSet<QString> seen;
            for (const auto& raw : names) {
                const QString name = raw.trimmed();
                if (name.isEmpty() || seen.contains(name) || !validText(name)) continue;
                seen.insert(name);
                if (seen.size() == 1) profiles.append({QStringLiteral("wsl"), QStringLiteral("WSL"), wsl, {},
                                                       QStringLiteral("Default installed WSL distribution")});
                profiles.append({QStringLiteral("wsl:") + name, QStringLiteral("WSL: ") + name, wsl,
                                 {QStringLiteral("--distribution"), name}, QStringLiteral("Windows Subsystem for Linux")});
            }
        } else {
            process.kill();
            process.waitForFinished(1000);
        }
    }
    if (profiles.isEmpty()) profiles.append({QStringLiteral("powershell"), QStringLiteral("PowerShell"),
                                              QStringLiteral("powershell.exe"), {QStringLiteral("-NoLogo")},
                                              QStringLiteral("Windows PowerShell fallback")});
    return profiles;
}

ShellProfile pickProfile(const QVector<ShellProfile>& profiles, const QString& key) {
    const QString normalized = key == QStringLiteral("cmd") ? QStringLiteral("native") : key;
    for (const auto& profile : profiles) if (profile.key == normalized) return profile;
    for (const auto& profile : profiles) if (profile.key == QStringLiteral("native")) return profile;
    return profiles.isEmpty() ? ShellProfile{} : profiles.first();
}
