#pragma once
#include "settings_store.h"
#include <QDialog>

class QLabel;
class QComboBox;
class QCheckBox;
class QSlider;
class QSpinBox;
class QButtonGroup;
class QVBoxLayout;

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(SettingsStore* store, const QVector<ShellProfile>& profiles, QWidget* parent = nullptr);
    bool applyWallpaper(const QString& path, QString* error = nullptr);
    void resetAppearance();
signals:
    void settingsChanged();
private:
    QSlider* addSlider(QVBoxLayout* layout, const QString& name, const QString& title, int low, int high, double scale, const QString& suffix);
    void loadControls();
    void updateControlStates();
    void commit();
    void updatePreview();
    void updateStyle();
    void chooseWallpaper();
    SettingsStore* store_;
    bool loading_ = false;
    QLabel *preview_, *backgroundName_, *error_;
    QComboBox *font_, *shell_;
    QCheckBox *copy_, *snap_, *live_;
    QSpinBox* scrollback_;
    QButtonGroup* accents_;
    QSlider *opacity_, *blurOpacity_, *dim_, *blur_, *liveBlur_, *refresh_, *tint_, *fontSize_;
};
