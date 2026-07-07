/**
 * @file camera_cmd.cpp
 * @brief Send finger camera calibration and MCUID commands.
 */

#include "databus.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

void printUsage(const char* program) {
    std::cout << "Usage:\n"
              << "  " << program << " {camerarc|MCUID|1234}\n"
              << "  " << program << " {left|right} {camerarc|MCUID|1234}\n";
}

bool isCommand(const std::string& value) {
    return value == "camerarc" || value == "MCUID" || value == "1234";
}

bool isCameraCalibCommand(const std::string& command) {
    return command.rfind("camera", 0) == 0;
}

void cameraCalibCallback(const std::vector<uint8_t>&) {
    std::cout << "Camera calibration data received" << std::endl;
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc < 2 || std::string(argv[1]) == "-h" || std::string(argv[1]) == "--help") {
        printUsage(argv[0]);
        return argc < 2 ? 1 : 0;
    }

    std::string side = "single";
    std::string command;
    if (argc == 2) {
        command = argv[1];
    } else {
        side = argv[1];
        command = argv[2];
        if (side != "left" && side != "right") {
            std::cerr << "First argument must be left or right when two arguments are provided"
                      << std::endl;
            printUsage(argv[0]);
            return 1;
        }
    }

    if (!isCommand(command)) {
        std::cerr << "Unsupported command: " << command << std::endl;
        printUsage(argv[0]);
        return 1;
    }

    std::string yaml_filename;
    if (command == "camerarc") yaml_filename = "cam0_sensor_" + side + ".yaml";

    const std::string result_dir = "calib_result";
    if (!yaml_filename.empty()) {
        std::cout << "Will write YAML: " << yaml_filename << std::endl;
        std::cout << "Save path: " << result_dir << "/" << yaml_filename << std::endl;
    }

    std::string serial_port;
    const char* env_serial = std::getenv("SERIAL_PORT");
    if (env_serial && *env_serial) {
        serial_port = env_serial;
    } else if (side == "left" || side == "right") {
        serial_port = das::findFingerSerialBySide(side);
    } else {
        serial_port = das::findSerialPort();
    }

    if (serial_port.empty()) {
        std::cerr << "No configured serial port found" << std::endl;
        return 1;
    }

    std::cout << "Side: " << side << std::endl;
    std::cout << "Serial port: " << serial_port << std::endl;
    std::cout << "Sending camera calibration command: " << command << std::endl;

    try {
        das::DataBus bus(
            serial_port, 921600, 0.5, true, 0, 0,
            nullptr, nullptr, cameraCalibCallback,
            yaml_filename, result_dir, false, command);
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        bus.sendCameraCalibCmd(command);

        if (command == "MCUID") {
            bus.waitForCalibResponse(3.0);
        } else if (isCameraCalibCommand(command)) {
            bus.waitForCalibResponse(2.0);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }

        bus.stop();

        if (command == "1234") {
            std::cout << "Calibration OK !" << std::endl;
        } else if (command == "MCUID") {
            std::cout << "MCUID query executed" << std::endl;
        } else {
            std::cout << "Finished sending command: " << command << std::endl;
            if (!yaml_filename.empty()) {
                const std::string yaml_path = result_dir + "/" + yaml_filename;
                if (std::filesystem::exists(yaml_path)) {
                    std::cout << "YAML file created: " << yaml_path << std::endl;
                } else {
                    std::cout << " YAML file not found; check device response" << std::endl;
                    return 1;
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << " Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
