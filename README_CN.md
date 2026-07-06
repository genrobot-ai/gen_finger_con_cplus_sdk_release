# Gen Finger Controller SDK (C++)

新一代单相机 finger 设备 C++ SDK。每只 finger 包含 1 个 CH340 串口和 1 个 USB 相机。

## 1. 环境准备

```bash
sudo apt update
sudo apt install -y build-essential cmake pkg-config libopencv-dev v4l-utils
```

USB 接口建议使用 USB 3.0。

## 2. USB 配置

> 首次使用请先完成 [USB 配置](docs/usb-setup_CN.md)。

首次使用前需为每个 USB 口配置 udev 规则，模板见 [config/99-usb-serial.rules](./config/99-usb-serial.rules)。

简要步骤：

1. 用 `udevadm` 和 `v4l2-ctl` 查询串口与相机的 `KERNELS` 值
2. 编辑 `config/99-usb-serial.rules`（每只 finger 只需 1 个串口 + 1 个相机）
3. 复制到 `/etc/udev/rules.d/` 并 reload

详细图文步骤见：

- [USB 配置指南 (ZH)](docs/usb-setup_CN.md)
- [USB Configuration Guide (EN)](docs/usb-setup.md)

单指配置后的默认软链接：

| 设备 | 软链接 |
|------|--------|
| 串口 | `/dev/ttyFingerLeft` |
| 相机 | `/dev/finger_camera_left` |

双指配置后的默认软链接：

| 设备 | 软链接 |
|------|--------|
| 左 finger 串口 | `/dev/ttyFingerLeft` |
| 左 finger 相机 | `/dev/finger_camera_left` |
| 右 finger 串口 | `/dev/ttyFingerRight` |
| 右 finger 相机 | `/dev/finger_camera_right` |

验证：

```bash
ls -l /dev/ttyFingerLeft /dev/finger_camera_left
ls -l /dev/ttyFingerRight /dev/finger_camera_right
```

## 3. 编译

```bash
mkdir -p build
cmake -S . -B build
cmake --build build -j$(nproc)
```

## 4. 运行 demo

```bash
./start_finger.cpp left
./start_finger.cpp left --distance 0.02
./start_finger.cpp left --sine-wave
./start_finger.cpp left --camera-fps 60
./start_finger.cpp left --stream-mode
./start_finger.cpp left --print-tactile-info --tactile-print-hz 2
```

距离范围为 `[0.0, 0.2]` 米。启动后默认弹出 1 个相机窗口，并打印 `finger distance`。

双 finger 可在两个终端分别运行：

```bash
./start_finger.cpp left
./start_finger.cpp right
```

## 5. 编程接口

主要回调：

```cpp
capture_frames_callback(CameraCapture* camera)
tactile_callback(const std::vector<uint8_t>& record_data)
encoder_callback(const std::vector<uint8_t>& record_data)
```

控制 finger 开合：

```cpp
if (system.getDataBus()) {
    system.getDataBus()->setTargetDistance(0.05f);
}
```

## 6. 设备参数获取

```bash
./camera_cmd.sh left MCUID
./camera_cmd.sh left camerarc
./camera_cmd.sh right MCUID
./camera_cmd.sh right camerarc
```

finger 新设备只有 1 个相机，常用标定命令为 `camerarc`，生成文件位于 `calib_result/`。

## 7. 常见问题

| 问题 | 解决方法 |
|------|----------|
| 找不到串口 | 执行 `sudo apt remove brltty`，重新插拔设备 |
| 相机或串口路径不对 | 检查 udev 规则，见 [docs/usb-setup_CN.md](docs/usb-setup_CN.md) |
