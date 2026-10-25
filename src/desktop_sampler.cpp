#include "desktop_sampler.h"
#include "image_blur.h"
#include <QImage>
#include <QMetaObject>
#include <windows.h>

namespace {

struct GrabResult {
    QImage image;
    QPoint origin;
    double pixelsPerPixel = 1.0;
};

// Grabs the whole virtual desktop at the requested scale. The sampled window is
// excluded from the grab, so the pixels describe the desktop behind it rather
// than the terminal itself, and the window never flickers on screen.
GrabResult grabDesktop(int scale, quintptr window) {
    GrabResult result;
    const int left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (width <= 0 || height <= 0) return result;

    const int outWidth = qMax(1, width / qMax(1, scale));
    const int outHeight = qMax(1, height / qMax(1, scale));

    HDC screen = GetDC(nullptr);
    if (!screen) return result;

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = outWidth;
    info.bmiHeader.biHeight = -outHeight;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    HDC memory = CreateCompatibleDC(screen);
    void* bits = nullptr;
    HBITMAP bitmap = memory ? CreateDIBSection(memory, &info, DIB_RGB_COLORS, &bits, nullptr, 0) : nullptr;
    if (!memory || !bitmap || !bits) {
        if (bitmap) DeleteObject(bitmap);
        if (memory) DeleteDC(memory);
        ReleaseDC(nullptr, screen);
        return result;
    }
    const HGDIOBJ previous = SelectObject(memory, bitmap);
    SetStretchBltMode(memory, HALFTONE);

    if (window) {
        HWND handle = reinterpret_cast<HWND>(window);
        if (IsWindow(handle)) SetWindowDisplayAffinity(handle, WDA_EXCLUDEFROMCAPTURE);
    }
    const BOOL copied = StretchBlt(memory, 0, 0, outWidth, outHeight, screen, left, top, width, height, SRCCOPY);
    if (window) {
        HWND handle = reinterpret_cast<HWND>(window);
        if (IsWindow(handle)) SetWindowDisplayAffinity(handle, WDA_NONE);
    }

    QImage image(outWidth, outHeight, QImage::Format_RGB32);
    if (copied) memcpy(image.bits(), bits, static_cast<size_t>(outWidth) * outHeight * 4);

    SelectObject(memory, previous);
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
    result.image = image;
    result.origin = QPoint(left, top);
    result.pixelsPerPixel = static_cast<double>(width) / outWidth;
    return result;
}

}

DesktopSampler::DesktopSampler(QObject* parent) : QObject(parent) {
    worker_ = std::thread([this] { run(); });
}

DesktopSampler::~DesktopSampler() {
    shutdown();
    const quintptr window = window_.load(std::memory_order_relaxed);
    if (window) {
        HWND handle = reinterpret_cast<HWND>(window);
        if (IsWindow(handle)) SetWindowDisplayAffinity(handle, WDA_NONE);
    }
}

void DesktopSampler::shutdown() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) return;
        stopping_ = true;
    }
    wake_.notify_all();
    if (worker_.joinable()) worker_.join();
}

void DesktopSampler::setActive(bool active) {
    active_.store(active, std::memory_order_relaxed);
    if (active) refresh();
}

void DesktopSampler::refresh() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) return;
        pending_ = true;
    }
    wake_.notify_all();
}

void DesktopSampler::run() {
    for (;;) {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait_for(lock, std::chrono::milliseconds(interval_.load(std::memory_order_relaxed)),
                            [this] { return stopping_ || pending_; });
            if (stopping_) return;
            pending_ = false;
        }
        if (!active_.load(std::memory_order_relaxed)) continue;
        capture();
        // A capture costs a few milliseconds. Wait a moment before taking the
        // next one so a burst of requests cannot keep the worker busy.
        std::unique_lock<std::mutex> lock(mutex_);
        wake_.wait_for(lock, std::chrono::milliseconds(120), [this] { return stopping_; });
        if (stopping_) return;
    }
}

void DesktopSampler::capture() {
    const quintptr handle = window_.load(std::memory_order_relaxed);
    if (!handle) return;
    HWND window = reinterpret_cast<HWND>(handle);
    if (!IsWindow(window) || !IsWindowVisible(window) || IsIconic(window)) return;

    const int scale = scale_.load(std::memory_order_relaxed);
    GrabResult grabbed = grabDesktop(scale, handle);
    if (grabbed.image.isNull()) return;

    // The blur radius is given in physical pixels; the sample is smaller, so it
    // is scaled down to match the resolution the blur is applied at.
    const double ratio = grabbed.pixelsPerPixel > 0.0 ? grabbed.pixelsPerPixel : 1.0;
    const int radius = radius_.load(std::memory_order_relaxed);
    const int sampleRadius = qMax(1, qRound(radius / ratio));
    const QImage image = blurImage(grabbed.image, sampleRadius);

    // The capture happens on the worker thread, so the result is handed to the
    // thread that owns this object before the signal is emitted.
    QMetaObject::invokeMethod(
        this,
        [this, image, origin = grabbed.origin, ratio] { emit sampled(image, origin, ratio); },
        Qt::QueuedConnection);
}
