#pragma once
#include "settings_store.h"
#include <QImage>
#include <QTimer>
#include <QWidget>

class Backdrop : public QWidget {
    Q_OBJECT
public:
    explicit Backdrop(SettingsStore* store, QWidget* parent = nullptr);
    void applySettings();
    void setDesktopCapture(const QImage& image);
    void clearDesktopCapture();
    QImage sourceImage() const;
    static bool importWallpaper(const QString& source, const QString& destination, QString* error = nullptr);
signals:
    void imageChanged();
protected:
    void paintEvent(QPaintEvent*) override;
private:
    void rebuild();
    SettingsStore* store_;
    QString loadedPath_;
    QString loadedStamp_;
    QImage source_, blurred_, desktop_, desktopBlurred_;
    int appliedBlur_ = -1;
    QTimer debounce_;
};
