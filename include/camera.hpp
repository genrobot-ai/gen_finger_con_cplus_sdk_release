/**
 * @file camera.hpp
 * @brief Single-camera capture for finger controller.
 */

#ifndef FINGER_CAMERA_HPP
#define FINGER_CAMERA_HPP

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <opencv2/opencv.hpp>
#include <string>
#include <thread>
#include <vector>

namespace das {

struct CameraInfo {
    int id = 0;
    cv::VideoCapture cap;
    std::string dev;
    int frame_count = 0;
    int width = 0;
    int height = 0;
    std::string window_name;
    std::unique_ptr<std::mutex> lock = std::make_unique<std::mutex>();
    cv::Mat latest_frame;
    uint64_t latest_ts_ns = 0;
    std::deque<double> cap_fps_ts;
    double cap_fps_val = 0.0;
    std::deque<double> disp_fps_ts;
    double disp_fps_val = 0.0;
};

class CameraCapture {
public:
    using FrameCallback = std::function<void(int camera_id, const cv::Mat& frame, uint64_t timestamp_ns)>;

    CameraCapture(const std::string& serial_port = "",
                  int camera_count = 1,
                  const std::vector<std::pair<int, int>>& resolutions = {{1600, 1296}},
                  bool show_preview = true,
                  const std::vector<std::string>& video_devices = {},
                  FrameCallback frame_callback = nullptr,
                  int target_fps = 30,
                  bool trigger_mode = true);

    ~CameraCapture();

    void captureFrames();
    void stop();
    void startGrabThread();
    void stopGrabThread();
    std::pair<cv::Mat, uint64_t> getLatest(CameraInfo& cam);

    bool isRunning() const { return running_; }
    std::vector<CameraInfo>& getCameras() { return cameras_; }
    void setFrameCallback(FrameCallback callback) { frame_callback_ = callback; }

    std::atomic<bool> running_;
    bool show_preview_;
    FrameCallback frame_callback_;
    std::vector<CameraInfo> cameras_;
    int target_fps_;
    bool trigger_mode_;

private:
    std::vector<std::string> getPhysicalDevices();
    bool tryResetDevice(const std::string& dev_path);
    bool initCamera(const std::string& dev_path, int cam_id);
    void initCameras();
    void syncGrabLoop();
    void displayFrames(const std::vector<std::pair<CameraInfo*, cv::Mat>>& frames_data);
    void releaseResources();

    std::string serial_port_;
    int camera_count_;
    std::vector<std::pair<int, int>> resolutions_;
    std::vector<std::string> video_devices_;
    std::unique_ptr<std::thread> grab_thread_;
};

} // namespace das

#endif // FINGER_CAMERA_HPP
