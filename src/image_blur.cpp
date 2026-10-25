#include "image_blur.h"
#include <QColor>
#include <algorithm>
#include <vector>

namespace {

// Blurs one line of pixels with a box filter of the given radius. Prefix sums
// keep the cost linear in the line length regardless of the radius, and the
// window is clamped to the line ends so the result never darkens.
void blurLine(const quint32* source, quint32* target, int count, int stride, int radius,
              std::vector<int>& red, std::vector<int>& green, std::vector<int>& blue, std::vector<int>& alpha) {
    if (count < 1) return;
    red[0] = green[0] = blue[0] = alpha[0] = 0;
    for (int i = 0; i < count; ++i) {
        const quint32 pixel = source[static_cast<size_t>(i) * stride];
        red[i + 1] = red[i] + qRed(pixel);
        green[i + 1] = green[i] + qGreen(pixel);
        blue[i + 1] = blue[i] + qBlue(pixel);
        alpha[i + 1] = alpha[i] + qAlpha(pixel);
    }
    for (int i = 0; i < count; ++i) {
        const int low = std::max(0, i - radius);
        const int high = std::min(count - 1, i + radius);
        const int samples = high - low + 1;
        target[static_cast<size_t>(i) * stride] =
            qRgba((red[high + 1] - red[low]) / samples, (green[high + 1] - green[low]) / samples,
                  (blue[high + 1] - blue[low]) / samples, (alpha[high + 1] - alpha[low]) / samples);
    }
}

void blurPass(QImage& image, int radius, bool horizontal) {
    const int width = image.width();
    const int height = image.height();
    if (width < 1 || height < 1 || radius < 1) return;
    const int lines = horizontal ? height : width;
    const int count = horizontal ? width : height;
    std::vector<int> red(count + 1), green(count + 1), blue(count + 1), alpha(count + 1);
    if (horizontal) {
        for (int y = 0; y < lines; ++y) {
            quint32* row = reinterpret_cast<quint32*>(image.scanLine(y));
            blurLine(row, row, count, 1, radius, red, green, blue, alpha);
        }
    } else {
        const int stride = image.bytesPerLine() / 4;
        quint32* base = reinterpret_cast<quint32*>(image.scanLine(0));
        for (int x = 0; x < lines; ++x)
            blurLine(base + x, base + x, count, stride, radius, red, green, blue, alpha);
    }
}

}

QImage blurImage(const QImage& source, int radius) {
    if (source.isNull() || radius < 1) return source;
    QImage image = source.convertToFormat(QImage::Format_ARGB32);
    const int limit = std::max(1, std::min(image.width(), image.height()) / 2);
    const int effective = std::min(radius, limit);
    for (int pass = 0; pass < 3; ++pass) {
        blurPass(image, effective, true);
        blurPass(image, effective, false);
    }
    return image;
}
