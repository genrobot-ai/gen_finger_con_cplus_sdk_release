# Gen Finger Controller C++ SDK

> C++ SDK for Gen Finger controller — single-camera streaming, tactile sensing, encoder feedback, and distance control.

[中文](README_CN.md)

[GitHub repository](https://github.com/genrobot-ai/gen_finger_con_cplus_sdk_release)

License: [MIT License](LICENSE)

## 1 Features

- C++ SDK (`finger_cpp_sdk` / `finger_cpp_sdk_static`) for direct integration without ROS
- Single-camera image capture with optional live preview
- Tactile sensor data via callback (left / right)
- Encoder feedback for finger opening distance
- Finger distance control via `DataBus::setTargetDistance()`
- `start_finger.cpp` wrapper script — compile-on-run demo launcher
- CMake build with `camera_cmd` calibration CLI and `camera_cmd.sh` helper
- Single-finger and dual-finger demo workflows

## 2 Requirements

| Item     | Requirement                   |
| -------- | ----------------------------- |
| OS       | Ubuntu 20.04 (recommended)    |
| C++      | C++17                         |
| CMake    | 3.16+                         |
| USB      | USB 3.0 port                  |
| Hardware | Gen Finger controller device  |

## 3 Quick Start

> First-time users must complete [USB configuration](docs/usb-setup.md) before running the SDK.

```shell
git clone https://github.com/genrobot-ai/gen_finger_con_cplus_sdk_release.git
cd gen_finger_con_cplus_sdk_release
mkdir -p build && cd build
cmake ..
make
cd ..
./start_finger.cpp left
```

Verify feedback in the console:

- One camera preview window (`Camera_0_*`)
- Encoder output: `finger distance: <value> m`
- Optional tactile grid with `--print-tactile-info`

Set a fixed opening distance:

```shell
./start_finger.cpp left --distance 0.05   # 5 cm, range [0.0, 0.2] m
```

## 4 Program Interface

Integrate the SDK by registering callbacks when constructing `FingerSystem` and sending control commands through `DataBus`.

### 4.1 Sensor Callbacks

| Callback                  | Type                                | Description                           |
| ------------------------- | ----------------------------------- | ------------------------------------- |
| `capture_frames_callback` | `void(CameraCapture*)`              | Camera frame capture and preview loop |
| `tactile_callback`        | `void(const std::vector<uint8_t>&)` | Tactile sensor raw data (448 bytes)   |
| `encoder_callback`        | `void(const std::vector<uint8_t>&)` | Finger opening distance feedback (m)  |

Example (from `start_finger.cpp`):

```cpp
FingerSystem system(
    serial_port,
    camera_resolutions,
    show_preview,
    video_devices,
    tactile_callback,
    encoder_callback,
    capture_frames_callback,
    camera_fps,
    trigger_mode
);
```

### 4.2 Finger Control

Send target opening distance through `DataBus`:

```cpp
if (databus_) {
    databus_->setTargetDistance(distance);  // range [0.0, 0.2] m
}
```

Or use the higher-level `FingerController` helper:

```cpp
FingerController controller(system.getDataBus());
controller.setFixedDistance(0.05f);
controller.startSineWave(amplitude, center, frequency, duration);
```

### 4.3 Libraries and Headers

| Target                 | Description                             |
| ---------------------- | --------------------------------------- |
| `finger_cpp_sdk_static` | Static library (used by demo script)   |
| `finger_cpp_sdk`       | Shared library                          |
| Headers                | `include/` (`finger_system.hpp`, etc.)  |

## 5 Installation

### 5.1 Install system dependencies

```shell
sudo apt update
sudo apt install build-essential cmake pkg-config libopencv-dev v4l-utils
```

`v4l-utils` provides `v4l2-ctl`, required for USB configuration.

### 5.2 Clone and build

```shell
git clone https://github.com/genrobot-ai/gen_finger_con_cplus_sdk_release.git
cd gen_finger_con_cplus_sdk_release
mkdir -p build && cd build
cmake ..
make
cd ..
```

Build outputs:

| Artifact               | Path                               |
| ---------------------- | ---------------------------------- |
| Static SDK library     | `build/libfinger_cpp_sdk_static.a` |
| Shared SDK library     | `build/libfinger_cpp_sdk.so`       |
| Camera calibration CLI | `build/camera_cmd`                 |

The `start_finger.cpp` script links against `libfinger_cpp_sdk_static.a` and recompiles on each run.

## 6 USB Configuration

Configure udev rules once per USB port before first use. The template is at [config/99-usb-serial.rules](./config/99-usb-serial.rules).

Each finger requires only one serial port and one camera rule (unlike the Gen Controller finger with three cameras).

Summary:

1. Query serial and camera `KERNELS` values with `udevadm` and `v4l2-ctl`
2. Edit `config/99-usb-serial.rules`
3. Copy to `/etc/udev/rules.d/` and reload rules

For step-by-step instructions with screenshots, see:

- [USB 配置指南 (ZH)](docs/usb-setup_CN.md)
- [USB Configuration Guide (EN)](docs/usb-setup.md)

Default serial symlinks after dual-finger setup: `/dev/ttyFingerLeft`, `/dev/ttyFingerRight`.

Default camera symlinks: `/dev/finger_camera_left`, `/dev/finger_camera_right` (see `start_finger.cpp` `SIDE_CONFIG`).

## 7 Usage

### 7.1 Single Finger Demo

```shell
cd gen_finger_con_cplus_sdk_release
./start_finger.cpp left                              # Default: open to 5 cm
./start_finger.cpp left --distance 0.08              # Fixed 8 cm
./start_finger.cpp left --sine-wave                  # Sine wave for 10 s
./start_finger.cpp left --print-tactile-info         # Print tactile grid
./start_finger.cpp left --no-preview                 # Disable preview window
./start_finger.cpp left --stream-mode                # Disable trigger mode for laptop compatibility
./start_finger.cpp left --camera-fps 60              # Set 60 for ~30 fps images
```

After startup, one image window appears:

```
Camera_0_*   # Finger camera
```

Console output includes tactile data (when enabled) and finger distance.

### 7.2 Dual Finger Demo

Run one instance per finger in separate terminals:

```shell
# Terminal 1 — left finger
./start_finger.cpp left

# Terminal 2 — right finger
./start_finger.cpp right
```

After startup, two image windows appear (one per finger).

When higher camera frame rate is needed:

```shell
./start_finger.cpp left --camera-fps 60
./start_finger.cpp right --camera-fps 60
```

### 7.3 Device Utilities

Do **not** run `start_finger.cpp` or other control programs while using these commands.

Build the project first (`cmake .. && make`), then use `./camera_cmd.sh`:

**Single device:**

```shell
./camera_cmd.sh MCUID      # Device ID
./camera_cmd.sh camerarc   # Camera calibration (single camera)
```

**Dual device (left / right):**

```shell
./camera_cmd.sh left MCUID
./camera_cmd.sh left camerarc

./camera_cmd.sh right MCUID
./camera_cmd.sh right camerarc
```

Finger devices use one camera, so `camerarc` is the normal calibration command. Calibration YAML files are saved to `calib_result/` (e.g. `cam0_sensor_left.yaml`).

## 8 Troubleshooting

| Problem                         | Solution                                                          |
| ------------------------------- | ----------------------------------------------------------------- |
| Serial port not found           | Run `sudo apt remove brltty`, then replug the device              |
| Camera or serial has wrong path | Re-check udev rules; see [docs/usb-setup.md](docs/usb-setup.md)   |
| Low camera frame rate           | Run with `--camera-fps 60` for 30 fps                            |
| No preview on laptop            | Try `--stream-mode` to disable trigger mode                       |
| `camera_cmd.sh` error           | Build first: `mkdir -p build && cd build && cmake .. && make`     |
| `start_finger.cpp` link error   | Ensure `make` completed and `build/libfinger_cpp_sdk_static.a` exists |
| OpenCV not found during build   | Install: `sudo apt install libopencv-dev pkg-config`              |
| No camera preview               | Check udev camera symlinks; verify with `v4l2-ctl --list-devices` |

## 9 Documentation Index

| Description         | Link                                                     |
| ------------------- | -------------------------------------------------------- |
| USB 配置 (ZH)         | [docs/usb-setup_CN.md](docs/usb-setup_CN.md)             |
| USB setup (EN)      | [docs/usb-setup.md](docs/usb-setup.md)                   |
| udev rules template | [config/99-usb-serial.rules](config/99-usb-serial.rules) |
| Demo launcher       | [start_finger.cpp](start_finger.cpp)                     |
| Calibration helper  | [camera_cmd.sh](camera_cmd.sh)                           |
| SDK headers         | [include/](include/)                                     |
