#include "backdrop.h"
#include <QFileInfo>
#include <QGraphicsBlurEffect>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QImageReader>
#include <QImageWriter>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QSaveFile>
#include <QDir>
#include <QDateTime>
#include <algorithm>

Backdrop::Backdrop(SettingsStore* store, QWidget* parent) : QWidget(parent), store_(store) {
    setObjectName(QStringLiteral("terminalBackdrop"));
    setAttribute(Qt::WA_TranslucentBackground);
    debounce_.setSingleShot(true);
    debounce_.setInterval(160);
    connect(&debounce_, &QTimer::timeout, this, &Backdrop::rebuild);
    applySettings();
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
        if (blurred_.isNull()) rebuild();
        else debounce_.start();
    }
    update();
}

void Backdrop::rebuild() {
    appliedBlur_ = store_->settings.bg_blur;
    blurred_ = source_;
    if (!source_.isNull() && appliedBlur_ > 0) {
        QGraphicsScene scene;
        auto* item = scene.addPixmap(QPixmap::fromImage(source_));
        auto* effect = new QGraphicsBlurEffect;
        effect->setBlurRadius(appliedBlur_);
        effect->setBlurHints(QGraphicsBlurEffect::QualityHint);
        item->setGraphicsEffect(effect);
        scene.setSceneRect(QRectF(QPointF(0, 0), source_.size()));
        blurred_ = QImage(source_.size(), QImage::Format_ARGB32_Premultiplied);
        blurred_.fill(Qt::black);
        QPainter painter(&blurred_);
        scene.render(&painter, QRectF(QPointF(0, 0), blurred_.size()), scene.sceneRect());
    }
    update();
    emit imageChanged();
}

void Backdrop::setDesktopCapture(const QImage& image, const QPoint& origin) {
    desktop_ = image;
    desktopOrigin_ = origin;
    desktopBlurred_ = image;
    if (!image.isNull()) {
        QGraphicsScene scene;
        auto* item = scene.addPixmap(QPixmap::fromImage(image));
        auto* effect = new QGraphicsBlurEffect;
        effect->setBlurRadius(30);
        effect->setBlurHints(QGraphicsBlurEffect::QualityHint);
        item->setGraphicsEffect(effect);
        scene.setSceneRect(QRectF(QPointF(0, 0), image.size()));
        desktopBlurred_ = QImage(image.size(), QImage::Format_ARGB32_Premultiplied);
        desktopBlurred_.fill(Qt::black);
        QPainter painter(&desktopBlurred_);
        scene.render(&painter, QRectF(QPointF(0, 0), desktopBlurred_.size()), scene.sceneRect());
        desktopBlurred_.setDevicePixelRatio(image.devicePixelRatio());
    }
    update();
}

void Backdrop::clearDesktopCapture() {
    desktop_ = {};
    desktopBlurred_ = {};
    update();
}

QImage Backdrop::sourceImage() const { return source_; }

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
        painter.fillRect(rect(), QColor(3, 6, 12, qRound(255 * store_->settings.bg_dim)));
    } else {
        painter.fillRect(rect(), QColor(7, 12, 20));
        if (!desktopBlurred_.isNull()) {
            const qreal scale = desktopBlurred_.devicePixelRatio();
            const QPoint delta = mapToGlobal(QPoint(0, 0)) - desktopOrigin_;
            const QRectF source(delta.x() * scale, delta.y() * scale, width() * scale, height() * scale);
            const double opacity = 1.0 - (1.0 - store_->settings.opacity) * (1.0 - store_->settings.background_blur_opacity);
            painter.setOpacity(std::clamp(opacity, 0.0, 1.0));
            painter.drawImage(QRectF(rect()), desktopBlurred_, source);
            painter.setOpacity(1.0);
        }
    }
    painter.setClipping(false);
    painter.setPen(QColor(255, 255, 255, 34));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(outline);
}
