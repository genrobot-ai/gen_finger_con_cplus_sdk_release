/**
 * @file finger_system.cpp
 * @brief High-level finger system orchestration.
 */

#include "finger_system.hpp"

#include <algorithm>
#include <cmath>
#include <csignal>
#include <iostream>
#include <sstream>

namespace das {

FingerSystem* FingerSystem::instance_ = nullptr;

SineWaveController::SineWaveController(DataBus* databus,
                                       float amplitude,
                                       float center,
                                       float frequency,
                                       float duration)
    : databus_(databus)
    , amplitude_(amplitude)
    , center_(center)
    , frequency_(frequency)
    , duration_(duration)
    , running_(false)
    , control_rate_(30.0f)
    , control_interval_(1.0f / control_rate_) {}

SineWaveController::~SineWaveController() {
    stop();
}

void SineWaveController::start() {
    if (running_) return;
    if (amplitude_ <= 0 ||
        center_ - amplitude_ < DataBus::MIN_TARGET_DISTANCE ||
        center_ + amplitude_ > DataBus::MAX_TARGET_DISTANCE) {
        std::cerr << "Sine wave parameters out of valid range [0.0, 0.2]" << std::endl;
        return;
    }

    running_ = true;
    start_time_ = std::chrono::steady_clock::now();
    control_thread_ = std::make_unique<std::thread>(&SineWaveController::controlLoop, this);
}

void SineWaveController::stop() {
    if (!running_) return;
    running_ = false;
    if (control_thread_ && control_thread_->joinable()) {
        control_thread_->join();
    }
}

void SineWaveController::controlLoop() {
    while (running_) {
        auto cycle_start = std::chrono::steady_clock::now();
        float current_time = std::chrono::duration<float>(cycle_start - start_time_).count();
        if (duration_ > 0 && current_time >= duration_) {
            running_ = false;
            break;
        }

        float value = center_ + amplitude_ * std::sin(2.0f * static_cast<float>(M_PI) * frequency_ * current_time);
        value = std::max(DataBus::MIN_TARGET_DISTANCE, std::min(DataBus::MAX_TARGET_DISTANCE, value));
        if (databus_) {
            databus_->setTargetDistance(value);
        }

        auto sleep_time = std::chrono::duration<float>(control_interval_) -
                          (std::chrono::steady_clock::now() - cycle_start);
        if (sleep_time.count() > 0) std::this_thread::sleep_for(sleep_time);
    }
}

FingerController::FingerController(DataBus* databus)
    : databus_(databus) {}

FingerController::~FingerController() {
    stopSineWave();
}

void FingerController::setFixedDistance(float distance) {
    if (sine_wave_controller_ && sine_wave_controller_->isRunning()) {
        sine_wave_controller_->stop();
    }
    if (databus_) {
        databus_->setTargetDistance(distance);
    }
}

void FingerController::startSineWave(float amplitude, float center, float frequency, float duration) {
    if (sine_wave_controller_ && sine_wave_controller_->isRunning()) {
        sine_wave_controller_->stop();
    }
    sine_wave_controller_ = std::make_unique<SineWaveController>(
        databus_, amplitude, center, frequency, duration);
    sine_wave_controller_->start();
}

void FingerController::stopSineWave() {
    if (sine_wave_controller_) {
        sine_wave_controller_->stop();
    }
}

bool FingerController::isSineWaveRunning() const {
    return sine_wave_controller_ && sine_wave_controller_->isRunning();
}

FingerSystem::FingerSystem(const std::string& serial_port,
                           const std::string& camera_resolutions,
                           bool show_preview,
                           const std::vector<std::string>& video_devices,
                           TactileCallback tactile_callback,
                           EncoderCallback encoder_callback,
                           FrameCallback capture_frames_callback,
                           int camera_fps,
                           bool trigger_mode)
    : running_(true)
    , serial_port_(serial_port)
    , camera_resolutions_(camera_resolutions)
    , show_preview_(show_preview)
    , video_devices_(video_devices)
    , tactile_callback_(tactile_callback)
    , encoder_callback_(encoder_callback)
    , capture_frames_callback_(capture_frames_callback)
    , camera_fps_(camera_fps)
    , trigger_mode_(trigger_mode) {
    resolutions_ = parseResolutions(camera_resolutions_);
    if (resolutions_.empty()) resolutions_ = {{1600, 1296}};
    instance_ = this;
    std::signal(SIGINT, staticSignalHandler);
    std::signal(SIGTERM, staticSignalHandler);
}

FingerSystem::~FingerSystem() {
    stop();
    instance_ = nullptr;
}

void FingerSystem::staticSignalHandler(int signum) {
    if (instance_) instance_->signalHandler(signum);
}

void FingerSystem::signalHandler(int signum) {
    std::cout << "\nReceived signal (" << signum << "), shutting down..." << std::endl;
    running_ = false;
    if (camera_) camera_->stop();
}

std::vector<std::pair<int, int>> FingerSystem::parseResolutions(const std::string& res_str) {
    std::vector<std::pair<int, int>> resolutions;
    std::stringstream ss(res_str);
    std::string item;
    while (std::getline(ss, item, ',')) {
        item.erase(std::remove_if(item.begin(), item.end(), ::isspace), item.end());
        size_t pos = item.find('x');
        if (pos == std::string::npos) continue;
        try {
            resolutions.push_back({std::stoi(item.substr(0, pos)), std::stoi(item.substr(pos + 1))});
        } catch (...) {
        }
    }
    return resolutions;
}

bool FingerSystem::start() {
    std::cout << "Starting finger system..." << std::endl;
    if (serial_port_.empty()) {
        serial_port_ = findSerialPort("ttyUSB");
        if (serial_port_.empty()) return false;
    }

    try {
        camera_ = std::make_unique<CameraCapture>(
            serial_port_, 1, resolutions_, show_preview_, video_devices_,
            nullptr, camera_fps_, trigger_mode_);
    } catch (const std::exception& e) {
        std::cerr << "Camera init failed: " << e.what() << std::endl;
        stop();
        return false;
    }

    std::this_thread::sleep_for(std::chrono::seconds(1));

    try {
        databus_ = std::make_unique<DataBus>(
            serial_port_, 921600, 0.5, false, 30, 0,
            tactile_callback_, encoder_callback_);
    } catch (const std::exception& e) {
        std::cerr << "Serial init failed: " << e.what() << std::endl;
        stop();
        return false;
    }

    if (capture_frames_callback_) {
        capture_frames_callback_(camera_.get());
    } else if (camera_) {
        camera_->captureFrames();
    }
    stop();
    return true;
}

void FingerSystem::stop() {
    running_ = false;
    if (camera_) camera_->stop();
    if (databus_) databus_->stop();
}

void FingerSystem::setFingerDistance(float distance) {
    if (databus_) {
        databus_->setTargetDistance(distance);
    }
}

} // namespace das
