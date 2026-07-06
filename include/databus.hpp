/**
 * @file databus.hpp
 * @brief Serial communication with finger controller.
 */

#ifndef FINGER_DATABUS_HPP
#define FINGER_DATABUS_HPP

#include "das_protocol.hpp"
#include "pack.hpp"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <thread>
#include <vector>

namespace das {

class DataBus {
public:
    using TactileCallback = std::function<void(const std::vector<uint8_t>&)>;
    using EncoderCallback = std::function<void(const std::vector<uint8_t>&)>;
    using CameraCalibCallback = std::function<void(const std::vector<uint8_t>&)>;

    static constexpr float MIN_TARGET_DISTANCE = 0.0f;
    static constexpr float MAX_TARGET_DISTANCE = 0.2f;

    DataBus(const std::string& tty_port = "/dev/ttyFingerLeft",
            int baudrate = 921600,
            double timeout = 0.5,
            bool is_calib_cmd = false,
            double encoder_freq = 0,
            double tactile_freq = 0,
            TactileCallback tactile_callback = nullptr,
            EncoderCallback encoder_callback = nullptr,
            CameraCalibCallback camera_calib_callback = nullptr,
            const std::string& yaml_filename = "",
            const std::string& output_dir = "",
            bool quiet_console = false,
            const std::string& calib_cmd_name = "");

    ~DataBus();

    void setTargetDistance(float distance);
    float getTargetDistance();
    void driveMotor(float angle_degree);
    void disableMotor();
    void calibEncoder();
    bool sendCameraCalibCmd(const std::string& camera_cmd);
    bool addCmd(const CmdPack& cmd);
    bool isOpened() const { return open_serial_success_; }

    void registerTactileCallback(TactileCallback callback);
    void registerEncoderCallback(EncoderCallback callback);
    void registerCameraCalibCallback(CameraCalibCallback callback);
    bool waitForCalibResponse(double timeout_sec = 3.0, double poll_interval_sec = 0.05);
    void stop();

private:
    void openSerial();
    void startReading();
    void startParsing();
    void startSending();
    void startEncoderLoop();
    void startTactileLoop();
    void readingLoop();
    void parsingLoop();
    void sendingLoop();
    void encoderLoop();
    void tactileLoop();

    std::string tty_port_;
    int baudrate_;
    double timeout_;
    int serial_fd_;
    std::atomic<bool> is_running_;
    std::atomic<bool> open_serial_success_;
    bool dry_run_serial_;

    std::vector<uint8_t> data_buffer_;
    std::mutex data_buffer_lock_;
    std::mutex serial_lock_;

    std::queue<CmdPack> cmd_queue_;
    std::mutex cmd_queue_lock_;
    std::condition_variable cmd_queue_cv_;

    double encoder_freq_;
    double tactile_freq_;
    float finger_dis_;
    std::mutex distance_lock_;
    std::atomic<bool> is_calib_cmd_;

    TactileCallback tactile_callback_;
    EncoderCallback encoder_callback_;
    CameraCalibCallback camera_calib_callback_;
    std::string yaml_filename_;
    std::string output_dir_;
    bool quiet_console_;
    std::string calib_cmd_name_;

    std::unique_ptr<std::thread> read_thread_;
    std::unique_ptr<std::thread> parse_thread_;
    std::unique_ptr<std::thread> send_thread_;
    std::unique_ptr<std::thread> encoder_thread_;
    std::unique_ptr<std::thread> tactile_thread_;
};

bool checkAndFixPermission(const std::string& port, bool verbose = true);
std::string findConfiguredSerialPort(bool verbose = true);
std::string findSerialPort(const std::string& pattern = "ttyUSB", bool verbose = true);
std::string findFingerSerialBySide(const std::string& side, bool verbose = true);

} // namespace das

#endif // FINGER_DATABUS_HPP
