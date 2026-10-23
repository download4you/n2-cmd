#pragma once
#include <QString>
#include <QStringList>
#include <QVector>

struct FavoriteCommand { QString name; QString command; };

struct Settings {
    double opacity = 0.94;
    QString background;
    double bg_dim = 0.58;
    int bg_blur = 8;
    double terminal_tint = 0.34;
    QString font_family = QStringLiteral("Vazir Code Hack");
    double font_size = 10.0;
    bool copy_on_select = true;
    QString default_shell = QStringLiteral("native");
    QStringList pinned_commands;
    QVector<FavoriteCommand> favorite_commands;
    QString accent = QStringLiteral("#3ddc97");
    int scrollback = 6000;
    int window_width = 1240;
    int window_height = 780;
    int window_x = -1;
    int window_y = -1;
    bool maximized = false;
};

struct ShellProfile {
    QString key;
    QString label;
    QString executable;
    QStringList arguments;
    QString description;
};

struct QuickCommand {
    QString key, label, category, description;
    QString powershell, cmd, unix_command;
    bool admin = false;
    QString commandFor(const QString& profileKey) const;
};
