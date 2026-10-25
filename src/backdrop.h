#pragma once
#include "settings_store.h"
#include <QImage>
#include <QPoint>
#include <QRect>
#include <QTimer>
#include <QWidget>

class DesktopSampler;
class QPainter;
class QPaintEvent;

// Paints the terminal backdrop: either the wallpaper or a live, blurred copy of
// the desktop that sits behind the window. The live copy is refreshed on a
// timer, so content behind the terminal keeps showing through instead of
// freezing a single screenshot taken at startup.
class Backdrop : public QWidget {
    Q_OBJECT
public:
    explicit Backdrop(SettingsStore* store, QWidget* parent = nullptr);
    ~Backdrop() override;

    void applySettings();
    void setWindow(quintptr window);
    void setRefreshInterval(int milliseconds);
    void refresh();
    QImage sourceImage() const;
    static bool importWallpaper(const QString& source, const QString& destination, QString* error = nullptr);
signals:
    void imageChanged();
protected:
    void paintEvent(QPaintEvent* event) override;
private:
    void rebuild();
    void paintDesktop(QPainter& painter);
    SettingsStore* store_;
    DesktopSampler* sampler_ = nullptr;
    quintptr window_ = 0;
    QString loadedPath_;
    QString loadedStamp_;
    QImage source_, blurred_, desktop_;
    QPoint desktopOrigin_;
    double desktopPixelsPerPixel_ = 1.0;
    int appliedBlur_ = -1;
    QTimer debounce_;
};
