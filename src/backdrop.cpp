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
constexpr int kDesktopScale = 2;
constexpr int kFadeSteps = 3;
}

Backdrop::Backdrop(SettingsStore* store, QWidget* parent) : QWidget(parent), store_(store) {
    setObjectName("terminalBackdrop");
    setAttribute(Qt::WA_TranslucentBackground);
    setAutoFillBackground(false);
    debounce_.setSingleShot(true);
    debounce_.setInterval(160);
    connect(&debounce_, &QTimer::timeout, this, &Backdrop::rebuild);

    sampler_ = new DesktopSampler(this);
    connect(sampler_, &DesktopSampler::sampled, this,
            [this](const QImage& image, const QPoint& origin, double pixelsPerPixel) {
                fading_ = desktop_;
                desktop_ = image;
                desktopOrigin_ = origin;
                desktopPixelsPerPixel_ = pixelsPerPixel > 0.0 ? pixelsPerPixel : 1.0;
                fadeStep_ = 0;
                fadeTimer_.start();
                update();
            }, Qt::QueuedConnection);

    fadeTimer_.setInterval(25);
    connect(&fadeTimer_, &QTimer::timeout, this, [this] {
        ++fadeStep_;
        if (fadeStep_ >= kFadeSteps || fading_.isNull()) { fadeTimer_.stop(); fading_ = {}; }
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
        sampler_->setBlurRadius(store_->settings.backdrop_blur_radius);
        sampler_->setRefreshInterval(store_->settings.backdrop_refresh_ms);
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

void Backdrop::stopSampling() {
    if (!sampler_) return;
    sampler_->setWindow(0);
    sampler_->shutdown();
}

void Backdrop::paintDesktop(QPainter& painter) {
    if (desktop_.isNull() || desktopPixelsPerPixel_ <= 0.0 || !window_) return;
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
    const QRectF target(rect());
    auto targetFor = [&](const QRectF& visibleArea) {
        return QRectF(target.left() + (visibleArea.left() - source.left()) * target.width() / source.width(),
                      target.top() + (visibleArea.top() - source.top()) * target.height() / source.height(),
                      visibleArea.width() * target.width() / source.width(),
                      visibleArea.height() * target.height() / source.height());
    };
    if (!fading_.isNull()) {
        const QRectF fadingAvailable(QPointF(0, 0), QSizeF(fading_.size()));
        const QRectF fadingVisible = source.intersected(fadingAvailable);
        if (!fadingVisible.isEmpty()) painter.drawImage(targetFor(fadingVisible), fading_, fadingVisible);
        const qreal progress = qBound(0.0, qreal(fadeStep_) / qreal(kFadeSteps), 1.0);
        painter.setOpacity(painter.opacity() * progress);
    }
    painter.drawImage(targetFor(visible), desktop_, visible);
    painter.setOpacity(1.0);
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
