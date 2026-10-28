#pragma once
#include <QImage>
#include <QObject>
#include <QPoint>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

// Captures and blurs the virtual desktop on a worker thread. Capture affinity
// is owned by the GUI thread so the terminal cannot recursively capture itself.
class DesktopSampler : public QObject {
    Q_OBJECT
public:
    explicit DesktopSampler(QObject* parent = nullptr);
    ~DesktopSampler() override;

    void setWindow(quintptr window);
    void setScale(int scale) { scale_.store(scale < 1 ? 1 : scale, std::memory_order_relaxed); }
    void setBlurRadius(int radius) { radius_.store(radius < 0 ? 0 : radius, std::memory_order_relaxed); }
    void setRefreshInterval(int milliseconds) { interval_.store(milliseconds < 120 ? 120 : milliseconds, std::memory_order_relaxed); }
    void setActive(bool active);
    quintptr captureWindow() const { return window_.load(std::memory_order_relaxed); }
    bool isActive() const { return active_.load(std::memory_order_relaxed); }
    quintptr affinityError() const { return affinityError_; }
    void refresh();
    void shutdown();

signals:
    void sampled(const QImage& image, const QPoint& origin, double pixelsPerPixel);

private:
    void run();
    void capture();
    void updateCaptureWindow();

    std::atomic<quintptr> window_{0};
    std::atomic<int> scale_{4};
    std::atomic<int> radius_{8};
    std::atomic<int> interval_{800};
    std::atomic<bool> active_{false};
    quintptr requestedWindow_ = 0;
    quintptr affinityWindow_ = 0;
    quintptr affinityError_ = 0;
    std::mutex mutex_;
    std::condition_variable wake_;
    bool stopping_ = false;
    bool pending_ = false;
    std::thread worker_;
};
