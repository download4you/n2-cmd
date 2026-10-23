#pragma once
#include "app_types.h"

class SettingsStore {
public:
    explicit SettingsStore(QString dataDirectory = {});
    Settings settings;
    QVector<QuickCommand> quickCommands;
    bool load(QString* error = nullptr);
    bool save(QString* error = nullptr);
    void sanitize();
    void resetAppearance();
    QString dataDirectory() const;
    QString settingsPath() const;
    QString assetPath(const QString& name) const;
    bool setPinned(const QString& key, bool pinned, QString* error = nullptr);
    bool addFavorite(const QString& name, const QString& command, QString* error = nullptr);
    bool removeFavorite(int index, QString* error = nullptr);
    const QuickCommand* quickCommand(const QString& key) const;
private:
    QString directory_;
    bool loadLua(QString* error);
};

QVector<ShellProfile> detectProfiles();
ShellProfile pickProfile(const QVector<ShellProfile>& profiles, const QString& key);
