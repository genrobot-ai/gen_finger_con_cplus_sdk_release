/**
 * @file finger_system.hpp
 * @brief High-level finger system orchestration.
 */

#ifndef FINGER_SYSTEM_HPP
#define FINGER_SYSTEM_HPP

#include "camera.hpp"
#include "databus.hpp"

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace das {

class SineWaveController {
public:
    SineWaveController(DataBus* databus,
                       float amplitude = 0.05f,
                       float center = 0.05f,
                       float frequency = 0.5f,
                       float duration = 1000.0f);
    ~SineWaveController();

    void start();
    void stop();
    bool isRunning() const { return running_; }

private:
    void controlLoop();

    DataBus* databus_;
    float amplitude_;
    float center_;
    float frequency_;
    float duration_;
    std::atomic<bool> running_;
    std::unique_ptr<std::thread> control_thread_;
    std::chrono::steady_clock::time_point start_time_;
    float control_rate_;
    float control_interval_;
};

class FingerController {
public:
    explicit FingerController(DataBus* databus);
    ~FingerController();

    void setFixedDistance(float distance);
    void startSineWave(float amplitude = 0.025f,
                       float center = 0.05f,
                       float frequency = 0.5f,
                       float duration = 10.0f);
    void stopSineWave();
    bool isSineWaveRunning() const;

private:
    DataBus* databus_;
    std::unique_ptr<SineWaveController> sine_wave_controller_;
};

class FingerSystem {
public:
    using TactileCallback = std::function<void(const std::vector<uint8_t>&)>;
    using EncoderCallback = std::function<void(const std::vector<uint8_t>&)>;
    using FrameCallback = std::function<void(CameraCapture*)>;

    FingerSystem(const std::string& serial_port = "",
                 const std::string& camera_resolutions = "1600x1296",
                 bool show_preview = true,
                 const std::vector<std::string>& video_devices = {},
                 TactileCallback tactile_callback = nullptr,
                 EncoderCallback encoder_callback = nullptr,
                 FrameCallback capture_frames_callback = nullptr,
                 int camera_fps = 30,
                 bool trigger_mode = true);
    ~FingerSystem();

    bool start();
    void stop();
    void setFingerDistance(float distance);
    DataBus* getDataBus() { return databus_.get(); }
    CameraCapture* getCamera() { return camera_.get(); }
    bool isRunning() const { return running_; }

private:
    void signalHandler(int signum);
    static void staticSignalHandler(int signum);
    std::vector<std::pair<int, int>> parseResolutions(const std::string& res_str);

    std::atomic<bool> running_;
    std::string serial_port_;
    std::string camera_resolutions_;
    bool show_preview_;
    std::vector<std::string> video_devices_;
    std::vector<std::pair<int, int>> resolutions_;
    TactileCallback tactile_callback_;
    EncoderCallback encoder_callback_;
    FrameCallback capture_frames_callback_;
    int camera_fps_;
    bool trigger_mode_;
    std::unique_ptr<DataBus> databus_;
    std::unique_ptr<CameraCapture> camera_;

    static FingerSystem* instance_;
};

} // namespace das

#endif // FINGER_SYSTEM_HPP
