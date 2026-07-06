/**
 * @file camera.cpp
 * @brief Single-camera capture implementation.
 */

#include "camera.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace das {

CameraCapture::CameraCapture(const std::string& serial_port,
                             int camera_count,
                             const std::vector<std::pair<int, int>>& resolutions,
                             bool show_preview,
                             const std::vector<std::string>& video_devices,
                             FrameCallback frame_callback,
                             int target_fps,
                             bool trigger_mode)
    : running_(true)
    , show_preview_(show_preview)
    , frame_callback_(frame_callback)
    , target_fps_(target_fps)
    , trigger_mode_(trigger_mode)
    , serial_port_(serial_port)
    , camera_count_(camera_count <= 0 ? 1 : camera_count)
    , resolutions_(resolutions.empty() ? std::vector<std::pair<int, int>>{{1600, 1296}} : resolutions)
    , video_devices_(video_devices) {
    initCameras();
}

CameraCapture::~CameraCapture() {
    stop();
}

std::vector<std::string> CameraCapture::getPhysicalDevices() {
    std::vector<std::string> devices;
    FILE* pipe = popen("v4l2-ctl --list-devices 2>/dev/null", "r");
    if (pipe) {
        char buffer[256];
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            std::string line(buffer);
            size_t pos = line.find("/dev/video");
            if (pos != std::string::npos) {
                std::string dev = line.substr(pos);
                dev.erase(std::remove(dev.begin(), dev.end(), '\n'), dev.end());
                dev.erase(std::remove(dev.begin(), dev.end(), '\r'), dev.end());
                dev.erase(dev.find_last_not_of(" \t") + 1);
                if (std::filesystem::exists(dev)) devices.push_back(dev);
            }
        }
        pclose(pipe);
    }
    if (devices.empty()) {
        for (int i = 0; i < 20; ++i) {
            std::string dev = "/dev/video" + std::to_string(i);
            if (std::filesystem::exists(dev)) devices.push_back(dev);
        }
    }
    std::sort(devices.begin(), devices.end());
    devices.erase(std::unique(devices.begin(), devices.end()), devices.end());
    return devices;
}

bool CameraCapture::tryResetDevice(const std::string& dev_path) {
    std::string cmd = "udevadm info -q path -n " + dev_path + " 2>/dev/null";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return false;
    char buffer[256];
    bool ok = false;
    if (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        std::string udev_info(buffer);
        udev_info.erase(std::remove(udev_info.begin(), udev_info.end(), '\n'), udev_info.end());
        std::string reset_path = "/sys" + udev_info + "/../reset";
        if (std::filesystem::exists(reset_path)) {
            std::ofstream reset(reset_path);
            if (reset.is_open()) {
                reset << "1";
                ok = true;
                std::this_thread::sleep_for(std::chrono::seconds(2));
            }
        }
    }
    pclose(pipe);
    return ok;
}

bool CameraCapture::initCamera(const std::string& dev_path, int cam_id) {
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (!std::filesystem::exists(dev_path)) {
            continue;
        }
        if (attempt > 0) {
            tryResetDevice(dev_path);
            int chmod_result = system(("sudo chmod 666 " + dev_path + " 2>/dev/null").c_str());
            int fuser_result = system(("sudo fuser -k " + dev_path + " 2>/dev/null").c_str());
            (void)chmod_result;
            (void)fuser_result;
        }

        cv::VideoCapture cap(dev_path, cv::CAP_V4L2);
        if (!cap.isOpened()) {
            continue;
        }

        cap.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));
        cap.set(cv::CAP_PROP_FPS, static_cast<double>(target_fps_));
        if (!trigger_mode_) {
            cap.set(cv::CAP_PROP_FOCUS, 0);
        }

        int actual_width = 0;
        int actual_height = 0;
        for (const auto& res : resolutions_) {
            cap.set(cv::CAP_PROP_FRAME_WIDTH, res.first);
            cap.set(cv::CAP_PROP_FRAME_HEIGHT, res.second);
            actual_width = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_WIDTH));
            actual_height = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_HEIGHT));
            if (actual_width == res.first && actual_height == res.second) {
                break;
            }
        }

        for (int i = 0; i < 5; ++i) {
            if (!cap.grab()) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        actual_width = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_WIDTH));
        actual_height = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_HEIGHT));

        CameraInfo info;
        info.id = cam_id;
        info.cap = std::move(cap);
        info.dev = dev_path;
        info.width = actual_width;
        info.height = actual_height;
        info.window_name = "Camera_" + std::to_string(cam_id) + "_" +
                           std::to_string(actual_width) + "x" + std::to_string(actual_height);
        cameras_.push_back(std::move(info));
        return true;
    }
    return false;
}

void CameraCapture::initCameras() {
    std::vector<std::string> devices = video_devices_;
    if (devices.empty()) {
        devices = getPhysicalDevices();
    }

    if (devices.size() > static_cast<size_t>(camera_count_)) {
        devices.resize(static_cast<size_t>(camera_count_));
    }

    for (size_t i = 0; i < devices.size(); ++i) {
        initCamera(devices[i], static_cast<int>(i));
    }

    if (cameras_.empty()) {
        running_ = false;
    }
}

void CameraCapture::syncGrabLoop() {
    while (running_) {
        auto now = std::chrono::steady_clock::now();
        double now_sec = std::chrono::duration<double>(now.time_since_epoch()).count();
        uint64_t ts_ns = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());

        for (auto& cam : cameras_) {
            if (!cam.cap.grab()) continue;
            cv::Mat frame;
            if (!cam.cap.retrieve(frame) || frame.empty()) continue;

            if (frame_callback_) frame_callback_(cam.id, frame, ts_ns);
            cam.frame_count++;
            {
                std::lock_guard<std::mutex> lock(*cam.lock);
                cam.latest_frame = frame;
                cam.latest_ts_ns = ts_ns;
            }
            cam.cap_fps_ts.push_back(now_sec);
            if (cam.cap_fps_ts.size() > 30) cam.cap_fps_ts.pop_front();
            if (cam.cap_fps_ts.size() >= 2) {
                double dt = cam.cap_fps_ts.back() - cam.cap_fps_ts.front();
                if (dt > 0) cam.cap_fps_val = (static_cast<double>(cam.cap_fps_ts.size()) - 1.0) / dt;
            }
        }
    }
}

void CameraCapture::startGrabThread() {
    if (!running_ || grab_thread_) return;
    grab_thread_ = std::make_unique<std::thread>(&CameraCapture::syncGrabLoop, this);
}

void CameraCapture::stopGrabThread() {
    running_ = false;
    if (grab_thread_ && grab_thread_->joinable()) {
        grab_thread_->join();
    }
    grab_thread_.reset();
}

std::pair<cv::Mat, uint64_t> CameraCapture::getLatest(CameraInfo& cam) {
    std::lock_guard<std::mutex> lock(*cam.lock);
    cv::Mat frame = cam.latest_frame;
    uint64_t ts = cam.latest_ts_ns;
    cam.latest_frame = cv::Mat();
    return {frame, ts};
}

void CameraCapture::displayFrames(const std::vector<std::pair<CameraInfo*, cv::Mat>>& frames_data) {
    for (const auto& [cam, frame] : frames_data) {
        if (frame.empty()) continue;
        auto time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::tm tm = *std::localtime(&time);
        std::ostringstream stamp;
        stamp << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");

        cv::Mat display = frame.clone();
        cv::putText(display,
                    "Camera_" + std::to_string(cam->id) + " | " + stamp.str() +
                        " | Frames: " + std::to_string(cam->frame_count),
                    cv::Point(10, 30), cv::FONT_HERSHEY_SIMPLEX, 0.7,
                    cv::Scalar(0, 255, 0), 2);
        cv::imshow(cam->window_name, display);
    }
    if (cv::waitKey(1) == 27) running_ = false;
}

void CameraCapture::captureFrames() {
    if (show_preview_) {
        for (auto& cam : cameras_) {
            cv::namedWindow(cam.window_name, cv::WINDOW_NORMAL);
            cv::resizeWindow(cam.window_name, 640, 480);
        }
    }

    startGrabThread();
    const double frame_interval = 1.0 / static_cast<double>(target_fps_ > 0 ? target_fps_ : 30);
    while (running_) {
        auto start = std::chrono::steady_clock::now();
        std::vector<std::pair<CameraInfo*, cv::Mat>> frames;
        for (auto& cam : cameras_) {
            auto [frame, ts] = getLatest(cam);
            (void)ts;
            frames.push_back({&cam, frame});
        }
        if (show_preview_) displayFrames(frames);
        auto sleep_time = std::chrono::duration<double>(frame_interval) -
                          (std::chrono::steady_clock::now() - start);
        if (sleep_time.count() > 0) std::this_thread::sleep_for(sleep_time);
    }
    releaseResources();
}

void CameraCapture::releaseResources() {
    stopGrabThread();
    for (auto& cam : cameras_) {
        if (cam.cap.isOpened()) cam.cap.release();
    }
    if (show_preview_) {
        for (auto& cam : cameras_) {
            cv::destroyWindow(cam.window_name);
        }
    }
}

void CameraCapture::stop() {
    running_ = false;
    stopGrabThread();
}

} // namespace das
