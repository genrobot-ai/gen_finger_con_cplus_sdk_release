#!/bin/bash
DIR="$(cd "$(dirname "$0")" && pwd)"; BIN=$(mktemp -u).out; ERR=$(mktemp 2>/dev/null || echo /tmp/sf_err); tail -n +4 "$0" | g++ -std=c++17 -w -O2 -o "$BIN" -x c++ - -I"$DIR/include" -L"$DIR/lib" -L"$DIR/build" -lfinger_cpp_sdk_static $(pkg-config --cflags --libs opencv4 2>/dev/null || pkg-config --cflags --libs opencv 2>/dev/null) -lpthread 2>"$ERR"; G=$?; [ $G -ne 0 ] && { cat "$ERR" >&2; rm -f "$ERR" "$BIN"; exit $G; }; rm -f "$ERR"; exec -a "$0" "$BIN" "$@"

#include "finger_system.hpp"
#include "pack.hpp"
#include "tactile_processing.hpp"

#include <chrono>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <map>
#include <opencv2/opencv.hpp>
#include <string>
#include <thread>
#include <vector>

using namespace das;

struct SideConfig {
    std::string serial_port;
    std::vector<std::string> video_devices;
};

std::map<std::string, SideConfig> SIDE_CONFIG = {
    {"left", {"/dev/ttyFingerLeft", {"/dev/finger_camera_left"}}},
    {"right", {"/dev/ttyFingerRight", {"/dev/finger_camera_right"}}},
};

void capture_frames_callback(CameraCapture* camera) {
    if (!camera) return;
    camera->captureFrames();
}

void tactile_callback(const std::vector<uint8_t>& record_data) {
    try {
        auto tactile = convert_tactile_448_to_1000(record_data);
        std::vector<int> all;
        all.reserve(1000);
        all.insert(all.end(), tactile.first.begin(), tactile.first.end());
        all.insert(all.end(), tactile.second.begin(), tactile.second.end());
        submit_tactile_1000_grid_print(all);
    } catch (const std::exception& e) {
        std::cerr << "Tactile data handler error: " << e.what() << std::endl;
    }
}

void encoder_callback(const std::vector<uint8_t>& record_data) {
    if (record_data.size() < 4) return;
    float value = bigEndianBytesToFloat(record_data.data());
    std::cout << "finger distance: " << std::fixed << std::setprecision(3)
              << value << " m" << std::endl;
}

void printUsage(const char* program) {
    std::cout << "Usage: " << program << " <left|right> [options]\n\n"
              << "Options:\n"
              << "  --camera-resolutions <WxH>  Camera resolution, default 1600x1296\n"
              << "  --no-preview                Disable OpenCV preview window\n"
              << "  --camera-fps <n>            Target display FPS, default 60\n"
              << "  --stream-mode               Disable trigger mode for laptop compatibility\n"
              << "  --distance <m>              Fixed distance, range [0.0, 0.2]\n"
              << "  --sine-wave                 Enable sine wave control\n"
              << "  --amplitude <m>             Sine amplitude, default 0.025\n"
              << "  --center <m>                Sine center, default 0.05\n"
              << "  --frequency <Hz>            Sine frequency, default 0.5\n"
              << "  --duration <s>              Sine duration, default 10.0, 0 = forever\n"
              << "  --print-tactile-info        Print tactile grid\n"
              << "  --tactile-print-hz <Hz>     Limit tactile print rate\n"
              << "  -h, --help                  Show this help\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    std::string side = argv[1];
    if (side == "-h" || side == "--help") {
        printUsage(argv[0]);
        return 0;
    }
    if (SIDE_CONFIG.find(side) == SIDE_CONFIG.end()) {
        std::cerr << "side must be left or right" << std::endl;
        printUsage(argv[0]);
        return 1;
    }

    std::string camera_resolutions = "1600x1296";
    bool show_preview = true;
    bool trigger_mode = true;
    bool use_sine_wave = false;
    float distance = -1.0f;
    float amplitude = 0.025f;
    float center = 0.05f;
    float frequency = 0.5f;
    float duration = 10.0f;
    int camera_fps = 60;
    bool print_tactile = false;
    double tactile_print_hz = 0.0;

    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--camera-resolutions" && i + 1 < argc) {
            camera_resolutions = argv[++i];
        } else if (arg == "--no-preview") {
            show_preview = false;
        } else if (arg == "--camera-fps" && i + 1 < argc) {
            camera_fps = std::stoi(argv[++i]);
        } else if (arg == "--stream-mode") {
            trigger_mode = false;
        } else if (arg == "--distance" && i + 1 < argc) {
            distance = std::stof(argv[++i]);
        } else if (arg == "--sine-wave") {
            use_sine_wave = true;
        } else if (arg == "--amplitude" && i + 1 < argc) {
            amplitude = std::stof(argv[++i]);
        } else if (arg == "--center" && i + 1 < argc) {
            center = std::stof(argv[++i]);
        } else if (arg == "--frequency" && i + 1 < argc) {
            frequency = std::stof(argv[++i]);
        } else if (arg == "--duration" && i + 1 < argc) {
            duration = std::stof(argv[++i]);
        } else if (arg == "--print-tactile-info") {
            print_tactile = true;
        } else if (arg == "--tactile-print-hz" && i + 1 < argc) {
            tactile_print_hz = std::stod(argv[++i]);
        } else if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        }
    }

    if (distance > DataBus::MAX_TARGET_DISTANCE) {
        std::cerr << "distance out of range [0.0, 0.2]" << std::endl;
        return 1;
    }

    set_tactile_grid_print_enabled(print_tactile);
    set_tactile_grid_print_max_hz(tactile_print_hz);

    const auto& config = SIDE_CONFIG[side];
    FingerSystem system(
        config.serial_port,
        camera_resolutions,
        show_preview,
        config.video_devices,
        tactile_callback,
        encoder_callback,
        capture_frames_callback,
        camera_fps,
        trigger_mode
    );

    std::thread control_thread([&]() {
        for (int i = 0; i < 100; ++i) {
            if (system.getDataBus()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                FingerController controller(system.getDataBus());
                if (use_sine_wave) {
                    controller.startSineWave(amplitude, center, frequency, duration);
                    while (controller.isSineWaveRunning() && system.isRunning()) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    }
                } else if (distance >= 0.0f) {
                    controller.setFixedDistance(distance);
                } else {
                    controller.setFixedDistance(0.05f);
                }
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        std::cerr << "Warning: system init timed out; control mode not applied" << std::endl;
    });
    control_thread.detach();

    return system.start() ? 0 : 1;
}
