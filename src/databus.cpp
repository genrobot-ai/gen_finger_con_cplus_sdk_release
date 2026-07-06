/**
 * @file databus.cpp
 * @brief Serial communication with finger controller.
 */

#include "databus.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

namespace das {

DataBus::DataBus(const std::string& tty_port,
                 int baudrate,
                 double timeout,
                 bool is_calib_cmd,
                 double encoder_freq,
                 double tactile_freq,
                 TactileCallback tactile_callback,
                 EncoderCallback encoder_callback,
                 CameraCalibCallback camera_calib_callback,
                 const std::string& yaml_filename,
                 const std::string& output_dir,
                 bool quiet_console,
                 const std::string& calib_cmd_name)
    : tty_port_(tty_port)
    , baudrate_(baudrate)
    , timeout_(timeout)
    , serial_fd_(-1)
    , is_running_(false)
    , open_serial_success_(false)
    , dry_run_serial_(false)
    , encoder_freq_(encoder_freq)
    , tactile_freq_(tactile_freq)
    , finger_dis_(0.0f)
    , is_calib_cmd_(is_calib_cmd)
    , tactile_callback_(tactile_callback)
    , encoder_callback_(encoder_callback)
    , camera_calib_callback_(camera_calib_callback)
    , yaml_filename_(yaml_filename)
    , output_dir_(output_dir)
    , quiet_console_(quiet_console)
    , calib_cmd_name_(calib_cmd_name) {
    openSerial();
    if (!open_serial_success_) {
        throw std::runtime_error("Cannot open serial port: " + tty_port_);
    }

    is_running_ = true;
    if (!dry_run_serial_) {
        startReading();
        startParsing();
        startSending();
        if (encoder_freq_ > 0) startEncoderLoop();
        if (tactile_freq_ > 0) startTactileLoop();
    }
}

DataBus::~DataBus() {
    stop();
}

void DataBus::openSerial() {
    serial_fd_ = open(tty_port_.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (serial_fd_ < 0) {
        open_serial_success_ = false;
        return;
    }

    termios tty;
    std::memset(&tty, 0, sizeof(tty));
    if (tcgetattr(serial_fd_, &tty) != 0) {
        if (tty_port_ == "/dev/null") {
            dry_run_serial_ = true;
            open_serial_success_ = true;
            return;
        }
        close(serial_fd_);
        serial_fd_ = -1;
        open_serial_success_ = false;
        return;
    }

    speed_t baud = B921600;
    switch (baudrate_) {
        case 9600: baud = B9600; break;
        case 19200: baud = B19200; break;
        case 38400: baud = B38400; break;
        case 57600: baud = B57600; break;
        case 115200: baud = B115200; break;
        case 230400: baud = B230400; break;
        case 460800: baud = B460800; break;
        case 921600: baud = B921600; break;
        default: baud = B921600; break;
    }

    cfsetispeed(&tty, baud);
    cfsetospeed(&tty, baud);
    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;
    tty.c_cflag &= ~CRTSCTS;
    tty.c_cflag |= CREAD | CLOCAL;
    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);
    tty.c_oflag &= ~OPOST;
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = static_cast<cc_t>(timeout_ * 10);

    if (tcsetattr(serial_fd_, TCSANOW, &tty) != 0) {
        close(serial_fd_);
        serial_fd_ = -1;
        open_serial_success_ = false;
        return;
    }

    // Pull BOOT0 low via modem lines (matches finger_controller_sdk databus_single.py).
    int modem_status = 0;
    if (ioctl(serial_fd_, TIOCMGET, &modem_status) == 0) {
        modem_status |= TIOCM_DTR;
        modem_status &= ~TIOCM_RTS;
        ioctl(serial_fd_, TIOCMSET, &modem_status);
    }

    tcflush(serial_fd_, TCIOFLUSH);
    if (!quiet_console_) {
        std::cout << "Serial opened: " << tty_port_ << ", baudrate: " << baudrate_ << std::endl;
    }
    open_serial_success_ = true;
}

void DataBus::setTargetDistance(float distance) {
    if (distance < MIN_TARGET_DISTANCE || distance > MAX_TARGET_DISTANCE) {
        throw std::invalid_argument("Distance must be in [0.0, 0.2], got: " + std::to_string(distance));
    }
    std::lock_guard<std::mutex> lock(distance_lock_);
    finger_dis_ = distance;
}

float DataBus::getTargetDistance() {
    std::lock_guard<std::mutex> lock(distance_lock_);
    return finger_dis_;
}

void DataBus::driveMotor(float angle_degree) {
    addCmd(CmdPack::pack(Opcode::WriteDrive, RecordType::Drive, floatToBigEndianBytes(angle_degree)));
}

void DataBus::disableMotor() {
    addCmd(CmdPack::pack(Opcode::DisableDrive, RecordType::Drive));
}

void DataBus::calibEncoder() {
    addCmd(CmdPack::pack(Opcode::CalibEncoder, RecordType::Drive));
}

bool DataBus::sendCameraCalibCmd(const std::string& camera_cmd) {
    is_calib_cmd_ = true;
    return addCmd(CmdPack::packCalib(std::vector<uint8_t>(camera_cmd.begin(), camera_cmd.end())));
}

bool DataBus::addCmd(const CmdPack& cmd) {
    if (dry_run_serial_) {
        return true;
    }
    std::lock_guard<std::mutex> lock(cmd_queue_lock_);
    if (cmd_queue_.size() >= 1000) {
        return false;
    }
    cmd_queue_.push(cmd);
    cmd_queue_cv_.notify_one();
    return true;
}

void DataBus::registerTactileCallback(TactileCallback callback) {
    tactile_callback_ = callback;
}

void DataBus::registerEncoderCallback(EncoderCallback callback) {
    encoder_callback_ = callback;
}

void DataBus::registerCameraCalibCallback(CameraCalibCallback callback) {
    camera_calib_callback_ = callback;
}

void DataBus::startReading() {
    read_thread_ = std::make_unique<std::thread>(&DataBus::readingLoop, this);
}

void DataBus::startParsing() {
    parse_thread_ = std::make_unique<std::thread>(&DataBus::parsingLoop, this);
}

void DataBus::startSending() {
    send_thread_ = std::make_unique<std::thread>(&DataBus::sendingLoop, this);
}

void DataBus::startEncoderLoop() {
    encoder_thread_ = std::make_unique<std::thread>(&DataBus::encoderLoop, this);
}

void DataBus::startTactileLoop() {
    tactile_thread_ = std::make_unique<std::thread>(&DataBus::tactileLoop, this);
}

void DataBus::readingLoop() {
    uint8_t buffer[16384];
    while (is_running_) {
        {
            std::lock_guard<std::mutex> lock(serial_lock_);
            if (serial_fd_ >= 0) {
                int bytes_available = 0;
                ioctl(serial_fd_, FIONREAD, &bytes_available);
                if (bytes_available > 0) {
                    ssize_t n = read(serial_fd_, buffer, std::min(bytes_available, 16384));
                    if (n > 0) {
                        std::lock_guard<std::mutex> data_lock(data_buffer_lock_);
                        data_buffer_.insert(data_buffer_.end(), buffer, buffer + n);
                    }
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void DataBus::parsingLoop() {
    while (is_running_) {
        std::vector<std::vector<uint8_t>> packets;
        {
            std::lock_guard<std::mutex> lock(data_buffer_lock_);
            if (!data_buffer_.empty()) {
                auto [found, remain] = DASProtocol::findPacket(data_buffer_);
                packets = found;
                data_buffer_ = remain;
            }
        }

        for (const auto& packet : packets) {
            if (is_calib_cmd_) {
                const std::string magic(PackContent::MAGIC,
                                        PackContent::MAGIC + PackContent::MAGIC_LENGTH);
                const bool magic_framed = packet.size() > 2 * magic.size()
                    && std::equal(magic.begin(), magic.end(), packet.begin())
                    && std::equal(magic.rbegin(), magic.rend(), packet.rbegin());

                // MCUID / 1234 / DMZEROSET: print payload between das magics.
                if (yaml_filename_.empty() && magic_framed) {
                    const std::vector<uint8_t> middle(packet.begin() + magic.size(),
                                                        packet.end() - magic.size());
                    std::string text;
                    bool printable = !middle.empty();
                    for (uint8_t byte : middle) {
                        if (byte > 127) {
                            printable = false;
                            break;
                        }
                    }
                    if (printable) {
                        text.assign(middle.begin(), middle.end());
                    } else {
                        std::ostringstream hex;
                        hex << std::hex << std::setfill('0');
                        for (uint8_t byte : middle) {
                            hex << std::setw(2) << static_cast<int>(byte);
                        }
                        text = hex.str();
                    }

                    if (calib_cmd_name_ == "MCUID") {
                        std::cout << "MCUID: " << text << std::endl;
                    } else {
                        std::cout << "Device response (" << calib_cmd_name_ << "): "
                                  << text << std::endl;
                    }
                    is_calib_cmd_ = false;
                    continue;
                }

                if (MessagePack::unpackCameraCalib(packet, yaml_filename_, output_dir_,
                                                   calib_cmd_name_)) {
                    if (camera_calib_callback_) {
                        camera_calib_callback_(packet);
                    }
                    is_calib_cmd_ = false;
                    continue;
                }

                auto pack = MessagePack::unpack(packet);
                if (pack) {
                    for (const auto& record : pack->records) {
                        if (record.record_type != RecordType::Echo) {
                            continue;
                        }

                        std::string text;
                        bool printable = !record.record_data.empty();
                        for (uint8_t byte : record.record_data) {
                            if (byte < 32 || byte > 126) {
                                printable = false;
                                break;
                            }
                        }
                        if (printable) {
                            text.assign(record.record_data.begin(), record.record_data.end());
                        } else {
                            std::ostringstream hex;
                            hex << std::hex << std::setfill('0');
                            for (uint8_t byte : record.record_data) {
                                hex << std::setw(2) << static_cast<int>(byte);
                            }
                            text = hex.str();
                        }
                        std::cout << "Device response (" << calib_cmd_name_ << "): "
                                  << text << std::endl;
                        is_calib_cmd_ = false;
                        break;
                    }
                }
                continue;
            }

            auto pack = MessagePack::unpack(packet);
            if (!pack) continue;
            for (const auto& record : pack->records) {
                if (record.record_type == RecordType::Tactile && tactile_callback_) {
                    tactile_callback_(record.record_data);
                } else if (record.record_type == RecordType::Encoder && encoder_callback_) {
                    encoder_callback_(record.record_data);
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(packets.empty() ? 5 : 1));
    }
}

void DataBus::sendingLoop() {
    while (is_running_) {
        CmdPack cmd;
        bool has_cmd = false;
        {
            std::unique_lock<std::mutex> lock(cmd_queue_lock_);
            cmd_queue_cv_.wait_for(lock, std::chrono::milliseconds(100),
                                   [this] { return !cmd_queue_.empty() || !is_running_; });
            if (!cmd_queue_.empty()) {
                cmd = cmd_queue_.front();
                cmd_queue_.pop();
                has_cmd = true;
            }
        }

        if (has_cmd) {
            std::lock_guard<std::mutex> lock(serial_lock_);
            if (serial_fd_ >= 0) {
                ssize_t written = write(serial_fd_, cmd.data.data(), cmd.data.size());
                if (written < 0 && !quiet_console_) {
                    std::cerr << "Serial write failed: " << strerror(errno) << std::endl;
                }
                tcdrain(serial_fd_);
            }
        }
    }
}

void DataBus::encoderLoop() {
    if (encoder_freq_ <= 0) return;
    const double interval = 1.0 / encoder_freq_;
    while (is_running_) {
        auto start = std::chrono::steady_clock::now();
        float target = getTargetDistance();
        addCmd(CmdPack::pack(Opcode::ReadBatch, RecordType::Encoder, floatToBigEndianBytes(target)));
        auto sleep_time = std::chrono::duration<double>(interval) - (std::chrono::steady_clock::now() - start);
        if (sleep_time.count() > 0) std::this_thread::sleep_for(sleep_time);
    }
}

void DataBus::tactileLoop() {
    if (tactile_freq_ <= 0) return;
    const double interval = 1.0 / tactile_freq_;
    while (is_running_) {
        auto start = std::chrono::steady_clock::now();
        addCmd(CmdPack::pack(Opcode::ReadSingle, RecordType::Tactile, floatToBigEndianBytes(0.0f)));
        auto sleep_time = std::chrono::duration<double>(interval) - (std::chrono::steady_clock::now() - start);
        if (sleep_time.count() > 0) std::this_thread::sleep_for(sleep_time);
    }
}

bool DataBus::waitForCalibResponse(double timeout_sec, double poll_interval_sec) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::duration<double>(timeout_sec);
    while (std::chrono::steady_clock::now() < deadline) {
        if (!is_calib_cmd_) return true;
        std::this_thread::sleep_for(std::chrono::duration<double>(poll_interval_sec));
    }
    return !is_calib_cmd_;
}

void DataBus::stop() {
    is_running_ = false;
    cmd_queue_cv_.notify_all();
    if (read_thread_ && read_thread_->joinable()) read_thread_->join();
    if (send_thread_ && send_thread_->joinable()) send_thread_->join();
    if (parse_thread_ && parse_thread_->joinable()) parse_thread_->join();
    if (encoder_thread_ && encoder_thread_->joinable()) encoder_thread_->join();
    if (tactile_thread_ && tactile_thread_->joinable()) tactile_thread_->join();
    if (serial_fd_ >= 0) {
        close(serial_fd_);
        serial_fd_ = -1;
    }
}

bool checkAndFixPermission(const std::string& port, bool verbose) {
    if (!std::filesystem::exists(port)) return false;
    if (access(port.c_str(), R_OK | W_OK) == 0) return true;
    const std::string cmd = "sudo chmod 666 " + port;
    int result = system(cmd.c_str());
    if (result == 0) return true;
    if (verbose) std::cerr << "No read/write access to " << port << std::endl;
    return false;
}

std::string findConfiguredSerialPort(bool verbose) {
    std::vector<std::string> ports;
    DIR* dir = opendir("/dev");
    if (dir) {
        while (auto* entry = readdir(dir)) {
            std::string name = entry->d_name;
            if (name.find("ttyFinger") == 0) {
                ports.push_back("/dev/" + name);
            }
        }
        closedir(dir);
    }
    std::sort(ports.begin(), ports.end());
    for (const auto& port : ports) {
        if (checkAndFixPermission(port, verbose)) return port;
    }
    return ports.empty() ? "" : ports[0];
}

std::string findSerialPort(const std::string& pattern, bool verbose) {
    (void)pattern;
    std::string port = findConfiguredSerialPort(verbose);
    if (!port.empty()) return port;
    if (verbose) {
        std::cerr << "No configured /dev/ttyFinger* serial device found" << std::endl;
    }
    return "";
}

std::string findFingerSerialBySide(const std::string& side, bool verbose) {
    const std::string dev = side == "right" ? "/dev/ttyFingerRight" : "/dev/ttyFingerLeft";
    if (side != "left" && side != "right") {
        if (verbose) std::cerr << "side must be left or right" << std::endl;
        return "";
    }
    if (!std::filesystem::exists(dev)) {
        if (verbose) std::cerr << "Serial device not found: " << dev << std::endl;
        return "";
    }
    return checkAndFixPermission(dev, verbose) ? dev : "";
}

} // namespace das
