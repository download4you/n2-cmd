#include "desktop_sampler.h"
#include "image_blur.h"
#include <QImage>
#include <windows.h>

namespace {

bool isOwnWindow(HWND handle) {
    if (!handle || !IsWindow(handle)) return false;
    DWORD owner = 0;
    GetWindowThreadProcessId(handle, &owner);
    return owner == GetCurrentProcessId();
}

struct GrabResult {
    QImage image;
    QPoint origin;
    double pixelsPerPixel = 1.0;
};

GrabResult grabDesktop(int scale, quintptr window) {
    GrabResult result;
    HWND handle = reinterpret_cast<HWND>(window);
    RECT client{};
    POINT origin{};
    if (!GetClientRect(handle, &client) || !ClientToScreen(handle, &origin)) return result;
    const int left = origin.x;
    const int top = origin.y;
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
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
    if (!previous || previous == HGDI_ERROR) {
        DeleteObject(bitmap);
        DeleteDC(memory);
        ReleaseDC(nullptr, screen);
        return result;
    }
    SetStretchBltMode(memory, HALFTONE);
    const BOOL copied = StretchBlt(memory, 0, 0, outWidth, outHeight, screen, left, top, width, height, SRCCOPY);
    POINT currentOrigin{};
    const bool unchanged = ClientToScreen(handle, &currentOrigin) && currentOrigin.x == left && currentOrigin.y == top;

    QImage image;
    if (copied && unchanged) {
        image = QImage(outWidth, outHeight, QImage::Format_RGB32);
        if (!image.isNull()) {
            const size_t rowBytes = static_cast<size_t>(outWidth) * 4;
            const auto* source = static_cast<const unsigned char*>(bits);
            for (int y = 0; y < outHeight; ++y)
                memcpy(image.scanLine(y), source + static_cast<size_t>(y) * rowBytes, rowBytes);
        }
    }

    SelectObject(memory, previous);
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
    if (image.isNull()) return result;
    result.image = image;
    result.origin = QPoint(left, top);
    result.pixelsPerPixel = static_cast<double>(width) / outWidth;
    return result;
}

}

DesktopSampler::DesktopSampler(QObject* parent) : QObject(parent) {
    worker_ = std::thread([this] { run(); });
}

DesktopSampler::~DesktopSampler() { shutdown(); }

void DesktopSampler::setWindow(quintptr window) {
    requestedWindow_ = window;
    updateCaptureWindow();
}

void DesktopSampler::updateCaptureWindow() {
    window_.store(0, std::memory_order_relaxed);
    if (affinityWindow_) {
        HWND handle = reinterpret_cast<HWND>(affinityWindow_);
        if (isOwnWindow(handle)) SetWindowDisplayAffinity(handle, WDA_NONE);
        affinityWindow_ = 0;
    }
    if (!active_.load(std::memory_order_relaxed) || !requestedWindow_) return;

    HWND handle = reinterpret_cast<HWND>(requestedWindow_);
    if (!isOwnWindow(handle) || !SetWindowDisplayAffinity(handle, WDA_EXCLUDEFROMCAPTURE)) return;
    affinityWindow_ = requestedWindow_;
    window_.store(requestedWindow_, std::memory_order_relaxed);
}

void DesktopSampler::shutdown() {
    bool stop = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!stopping_) { stopping_ = true; stop = true; }
    }
    if (stop) {
        wake_.notify_all();
        if (worker_.joinable()) worker_.join();
    }
    requestedWindow_ = 0;
    updateCaptureWindow();
}

void DesktopSampler::setActive(bool active) {
    const bool previous = active_.exchange(active, std::memory_order_relaxed);
    if (previous != active) updateCaptureWindow();
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
        std::unique_lock<std::mutex> lock(mutex_);
        wake_.wait_for(lock, std::chrono::milliseconds(120), [this] { return stopping_; });
        if (stopping_) return;
    }
}

void DesktopSampler::capture() {
    const quintptr handle = window_.load(std::memory_order_relaxed);
    if (!handle) return;
    HWND window = reinterpret_cast<HWND>(handle);
    if (!isOwnWindow(window) || !IsWindowVisible(window) || IsIconic(window)) return;

    const int scale = scale_.load(std::memory_order_relaxed);
    GrabResult grabbed = grabDesktop(scale, handle);
    if (grabbed.image.isNull()) return;

    const double ratio = grabbed.pixelsPerPixel > 0.0 ? grabbed.pixelsPerPixel : 1.0;
    const int radius = radius_.load(std::memory_order_relaxed);
    const int sampleRadius = qMax(1, qRound(radius / ratio));
    const QImage image = blurImage(grabbed.image, sampleRadius);
    if (!image.isNull()) emit sampled(image, grabbed.origin, ratio);
}
