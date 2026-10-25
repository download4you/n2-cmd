#pragma once
#include <QImage>
#include <QObject>
#include <QPoint>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

// Grabs the virtual desktop so the shell can paint a live blurred backdrop.
//
// The whole desktop is captured at a reduced scale, which keeps the grab cheap
// and lets the shell crop the area under its window from it. The terminal is
// hidden from the grab with WDA_EXCLUDEFROMCAPTURE, so the pixels describe the
// desktop behind the window instead of the window's own content, and nothing
// on screen flickers while the sample is taken.
//
// The grab runs on a plain worker thread so the interface never stalls. Every
// setter is safe to call from the GUI thread.
class DesktopSampler : public QObject {
    Q_OBJECT
public:
    explicit DesktopSampler(QObject* parent = nullptr);
    ~DesktopSampler() override;

    void setWindow(quintptr window) { window_.store(window, std::memory_order_relaxed); }
    void setScale(int scale) { scale_.store(scale < 1 ? 1 : scale, std::memory_order_relaxed); }
    void setBlurRadius(int radius) { radius_.store(radius < 0 ? 0 : radius, std::memory_order_relaxed); }
    void setRefreshInterval(int milliseconds) { interval_.store(milliseconds < 120 ? 120 : milliseconds, std::memory_order_relaxed); }
    void setActive(bool active);
    // Asks for a sample as soon as the worker is ready for one.
    void refresh();
    // Stops the worker and waits for it to finish.
    void shutdown();

signals:
    // The blurred desktop, the virtual screen origin it was taken at and the
    // number of physical pixels covered by one image pixel.
    void sampled(const QImage& image, const QPoint& origin, double pixelsPerPixel);

private:
    void run();
    void capture();

    std::atomic<quintptr> window_{0};
    std::atomic<int> scale_{4};
    std::atomic<int> radius_{8};
    std::atomic<int> interval_{800};
    std::atomic<bool> active_{false};
    std::mutex mutex_;
    std::condition_variable wake_;
    bool stopping_ = false;
    bool pending_ = false;
    std::thread worker_;
};
