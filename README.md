# Gen Finger Controller SDK (C++)

C++ SDK for the single-camera Gen Finger controller. Each finger uses one CH340 serial device and one USB camera.

## Build

```bash
sudo apt update
sudo apt install -y build-essential cmake pkg-config libopencv-dev v4l-utils

mkdir -p build
cmake -S . -B build
cmake --build build -j$(nproc)
```

## USB Configuration

> First-time users must complete [USB configuration](docs/usb-setup.md) before running the SDK.

Configure udev rules once per USB port before first use. The template is at [config/99-usb-serial.rules](./config/99-usb-serial.rules).

Each finger requires only one serial port and one camera rule.

Steps:

1. Query serial and camera `KERNELS` values with `udevadm` and `v4l2-ctl`
2. Edit `config/99-usb-serial.rules`
3. Copy to `/etc/udev/rules.d/` and reload

Detailed guide:

- [USB 配置指南 (ZH)](docs/usb-setup_CN.md)
- [USB Configuration Guide (EN)](docs/usb-setup.md)

Expected device links:

```bash
/dev/ttyFingerLeft
/dev/finger_camera_left
/dev/ttyFingerRight
/dev/finger_camera_right
```

## Demo

```bash
./start_finger.cpp left
./start_finger.cpp left --distance 0.02
./start_finger.cpp left --sine-wave
./start_finger.cpp left --stream-mode
./start_finger.cpp left --print-tactile-info
```

Distance range is `[0.0, 0.2]` meters.

## Device Commands

```bash
./camera_cmd.sh left MCUID
./camera_cmd.sh left camerarc
```

Finger devices use one camera, so `camerarc` is the normal calibration command.
