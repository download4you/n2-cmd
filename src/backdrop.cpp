#include "backdrop.h"
#include "desktop_sampler.h"
#include "image_blur.h"
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QImageWriter>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QSaveFile>
#include <algorithm>
#include <windows.h>

namespace {
// The desktop is captured at a quarter of its physical size. The sample is
// then blurred and stretched back to the window, which is both cheap and gives
// the soft wash the backdrop is meant to look like.
constexpr int kDesktopScale = 4;
// Blur radius in physical pixels; the sampler converts it to sample pixels.
constexpr int kDesktopBlurRadius = 26;
}

Backdrop::Backdrop(SettingsStore* store, QWidget* parent) : QWidget(parent), store_(store) {
    setObjectName("terminalBackdrop");
    setAttribute(Qt::WA_TranslucentBackground);
    debounce_.setSingleShot(true);
    debounce_.setInterval(160);
    connect(&debounce_, &QTimer::timeout, this, &Backdrop::rebuild);

    sampler_ = new DesktopSampler(this);
    connect(sampler_, &DesktopSampler::sampled, this,
            [this](const QImage& image, const QPoint& origin, double pixelsPerPixel) {
                desktop_ = image;
                desktopOrigin_ = origin;
                desktopPixelsPerPixel_ = pixelsPerPixel > 0.0 ? pixelsPerPixel : 1.0;
                update();
            });
    applySettings();
}

Backdrop::~Backdrop() {
    if (sampler_) sampler_->shutdown();
}

bool Backdrop::importWallpaper(const QString& source, const QString& destination, QString* error) {
    QImage image;
    {
        QImageReader reader(source);
        reader.setAutoTransform(true);
        image = reader.read();
        if (image.isNull()) {
            if (error) *error = QStringLiteral("Could not read that image: %1").arg(reader.errorString());
            return false;
        }
    }
    image = image.convertToFormat(QImage::Format_RGB32);
    if (!QDir().mkpath(QFileInfo(destination).absolutePath())) {
        if (error) *error = QStringLiteral("Could not create the wallpaper folder.");
        return false;
    }
    QSaveFile output(destination);
    if (!output.open(QIODevice::WriteOnly)) {
        if (error) *error = output.errorString();
        return false;
    }
    QImageWriter writer(&output, "png");
    if (!writer.write(image)) {
        if (error) *error = writer.errorString();
        output.cancelWriting();
        return false;
    }
    if (!output.commit()) {
        if (error) *error = output.errorString();
        return false;
    }
    return true;
}

void Backdrop::applySettings() {
    QString path = store_->settings.background;
    if (path.isEmpty() || !QFileInfo::exists(path)) path = store_->assetPath(QStringLiteral("wallpaper.jpg"));
    const QFileInfo info(path);
    const QString stamp = QString::number(info.size()) + ':' + QString::number(info.lastModified().toMSecsSinceEpoch());
    if (path != loadedPath_ || stamp != loadedStamp_ || source_.isNull()) {
        QImageReader reader(path);
        reader.setAutoTransform(true);
        source_ = reader.read();
        if (source_.isNull() && path != store_->assetPath(QStringLiteral("wallpaper.jpg"))) {
            path = store_->assetPath(QStringLiteral("wallpaper.jpg"));
            QImageReader fallback(path);
            fallback.setAutoTransform(true);
            source_ = fallback.read();
        }
        loadedPath_ = path;
        loadedStamp_ = stamp;
        if (source_.width() > 2560 || source_.height() > 1600)
            source_ = source_.scaled(2560, 1600, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        appliedBlur_ = -1;
    }
    if (appliedBlur_ != store_->settings.bg_blur) {
        if (blurred_.isNull()) rebuild(); else debounce_.start();
    }
    if (sampler_) {
        sampler_->setScale(kDesktopScale);
        sampler_->setBlurRadius(kDesktopBlurRadius);
        sampler_->setActive(!store_->settings.opaque_background);
    }
    update();
}

void Backdrop::rebuild() {
    appliedBlur_ = store_->settings.bg_blur;
    blurred_ = blurImage(source_, appliedBlur_);
    update();
    emit imageChanged();
}

void Backdrop::setWindow(quintptr window) {
    window_ = window;
    if (sampler_) {
        sampler_->setWindow(window);
        sampler_->refresh();
    }
}

void Backdrop::setRefreshInterval(int milliseconds) {
    if (sampler_) sampler_->setRefreshInterval(milliseconds);
}

QImage Backdrop::sourceImage() const { return source_; }

void Backdrop::refresh() {
    if (sampler_ && !store_->settings.opaque_background) sampler_->refresh();
}

void Backdrop::paintDesktop(QPainter& painter) {
    if (desktop_.isNull() || desktopPixelsPerPixel_ <= 0.0 || !window_) return;
    // The sample covers the whole virtual desktop, so the area under this window
    // is picked by its position on the screen. The result stays locked to the
    // desktop while the window is dragged instead of sliding along with it.
    HWND handle = reinterpret_cast<HWND>(window_);
    RECT client{};
    POINT origin{client.left, client.top};
    if (!GetClientRect(handle, &client) || !ClientToScreen(handle, &origin)) return;
    const double ratio = desktopPixelsPerPixel_;
    const QRectF source((origin.x - desktopOrigin_.x()) / ratio, (origin.y - desktopOrigin_.y()) / ratio,
                        client.right / ratio, client.bottom / ratio);
    const QRectF available(QPointF(0, 0), QSizeF(desktop_.size()));
    const QRectF visible = source.intersected(available);
    if (visible.isEmpty()) return;
    painter.drawImage(QRectF(rect()), desktop_, visible);
}

void Backdrop::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    QPainterPath outline;
    const bool square = window()->isMaximized() || window()->isFullScreen();
    outline.addRoundedRect(QRectF(rect()).adjusted(.5, .5, -.5, -.5), square ? 0 : 13, square ? 0 : 13);
    painter.setClipPath(outline);
    if (store_->settings.opaque_background) {
        QLinearGradient gradient(0, 0, width(), height());
        gradient.setColorAt(0, QColor("#0b2230"));
        gradient.setColorAt(.55, QColor("#0a101d"));
        gradient.setColorAt(1, QColor("#1a1030"));
        painter.fillRect(rect(), gradient);
        if (!blurred_.isNull()) {
            QSizeF size = blurred_.size();
            size.scale(this->size(), Qt::KeepAspectRatioByExpanding);
            painter.drawImage(QRectF((width() - size.width()) / 2, (height() - size.height()) / 2, size.width(), size.height()), blurred_);
        }
    } else {
        painter.fillRect(rect(), QColor(7, 12, 20));
        const double opacity = 1.0 - (1.0 - store_->settings.opacity) * (1.0 - store_->settings.background_blur_opacity);
        painter.save();
        painter.setOpacity(std::clamp(opacity, 0.0, 1.0));
        paintDesktop(painter);
        painter.restore();
    }
    painter.fillRect(rect(), QColor(3, 6, 12, qRound(255 * store_->settings.bg_dim)));
    painter.setClipping(false);
    painter.setPen(QColor(255, 255, 255, 34));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(outline);
}
