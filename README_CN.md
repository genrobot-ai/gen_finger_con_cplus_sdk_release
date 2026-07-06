# Gen Finger Controller C++ SDK

> 用于 Gen Finger 单相机设备的 C++ SDK，支持相机图像、触觉传感、编码器反馈及开合距离控制。

[English](README.md)

[GitHub代码](https://github.com/genrobot-ai/gen_finger_con_cplus_sdk_release)

License: [MIT License](LICENSE)

## 1 功能特性

- C++ SDK（`finger_cpp_sdk` / `finger_cpp_sdk_static`），无需 ROS 即可直接集成
- 单相机图像采集，支持实时预览
- 触觉数据回调（左 / 右）
- finger 开合距离编码器反馈
- 通过 `DataBus::setTargetDistance()` 控制 finger 开合
- `start_finger.cpp` 包装脚本 — 按需编译并运行 demo
- CMake 构建，含 `camera_cmd` 标定 CLI 与 `camera_cmd.sh` 辅助脚本
- 单指 / 双指 demo 工作流

## 2 环境要求

| 项目   | 要求                     |
| ---- | ---------------------- |
| 系统   | Ubuntu 20.04（推荐）       |
| C++  | C++17                  |
| CMake | 3.16+                  |
| USB  | USB 3.0 接口             |
| 硬件   | Gen Finger controller 设备 |

## 3 快速开始

> 首次使用请先完成 [USB 配置](docs/usb-setup_CN.md)。

```shell
git clone https://github.com/genrobot-ai/gen_finger_con_cplus_sdk_release.git
cd gen_finger_con_cplus_sdk_release
mkdir -p build && cd build
cmake ..
make
cd ..
./start_finger.cpp left
```

在控制台验证反馈：

- 一个相机预览窗口（`Camera_0_*`）
- 编码器输出：`finger distance: <value> m`
- 可选触觉网格：`--print-tactile-info`

设置固定开合距离：

```shell
./start_finger.cpp left --distance 0.05   # 5 cm，范围 [0.0, 0.2] m
```

## 4 程序接口

通过构造 `FingerSystem` 时注册回调获取传感器数据，通过 `DataBus` 下发控制指令。

### 4.1 传感器回调

| 回调                        | 类型                                  | 说明                |
| ------------------------- | ----------------------------------- | ----------------- |
| `capture_frames_callback` | `void(CameraCapture*)`              | 相机帧采集与预览循环        |
| `tactile_callback`        | `void(const std::vector<uint8_t>&)` | 触觉传感器原始数据（448 字节） |
| `encoder_callback`        | `void(const std::vector<uint8_t>&)` | finger 开合距离反馈（m）   |

示例（来自 `start_finger.cpp`）：

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

### 4.2 Finger 控制

通过 `DataBus` 下发目标开合距离：

```cpp
if (databus_) {
    databus_->setTargetDistance(distance);  // 范围 [0.0, 0.2] m
}
```

或使用更高级的 `FingerController` 辅助类：

```cpp
FingerController controller(system.getDataBus());
controller.setFixedDistance(0.05f);
controller.startSineWave(amplitude, center, frequency, duration);
```

### 4.3 库与头文件

| 目标                    | 说明                               |
| --------------------- | -------------------------------- |
| `finger_cpp_sdk_static` | 静态库（demo 脚本使用）                  |
| `finger_cpp_sdk`      | 动态库                              |
| 头文件                   | `include/`（`finger_system.hpp` 等） |

## 5 安装

### 5.1 安装系统依赖

```shell
sudo apt update
sudo apt install build-essential cmake pkg-config libopencv-dev v4l-utils
```

`v4l-utils` 提供 USB 配置所需的 `v4l2-ctl` 命令。

### 5.2 拉取仓库并编译

```shell
git clone https://github.com/genrobot-ai/gen_finger_con_cplus_sdk_release.git
cd gen_finger_con_cplus_sdk_release
mkdir -p build && cd build
cmake ..
make
cd ..
```

编译产物：

| 产物       | 路径                               |
| -------- | -------------------------------- |
| 静态 SDK 库 | `build/libfinger_cpp_sdk_static.a` |
| 动态 SDK 库 | `build/libfinger_cpp_sdk.so`       |
| 相机标定 CLI | `build/camera_cmd`                 |

`start_finger.cpp` 脚本链接 `libfinger_cpp_sdk_static.a`，每次运行时会按需重新编译。

## 6 USB 配置

首次使用前需为每个 USB 口配置 udev 规则，模板见 [config/99-usb-serial.rules](./config/99-usb-serial.rules)。

每只 finger 只需 1 个串口 + 1 个相机（与三相机的 Gen Controller 夹爪不同）。

简要步骤：

1. 用 `udevadm` 和 `v4l2-ctl` 查询串口与相机的 `KERNELS` 值
2. 编辑 `config/99-usb-serial.rules`
3. 复制到 `/etc/udev/rules.d/` 并 reload

详细图文步骤见：

- [USB 配置指南 (ZH)](docs/usb-setup_CN.md)
- [USB Configuration Guide (EN)](docs/usb-setup.md)

双指配置后的默认串口软链接：`/dev/ttyFingerLeft`、`/dev/ttyFingerRight`。

默认相机软链接：`/dev/finger_camera_left`、`/dev/finger_camera_right`（见 `start_finger.cpp` 中 `SIDE_CONFIG`）。

## 7 使用方法

### 7.1 单指 Demo

```shell
cd gen_finger_con_cplus_sdk_release
./start_finger.cpp left                              # 默认：打开 5 cm
./start_finger.cpp left --distance 0.08              # 固定打开 8 cm
./start_finger.cpp left --sine-wave                  # 正弦波开合 10 s
./start_finger.cpp left --print-tactile-info         # 打印触觉网格
./start_finger.cpp left --no-preview                 # 关闭预览窗口
./start_finger.cpp left --stream-mode                # 关闭 trigger 模式，兼容部分笔记本
./start_finger.cpp left --camera-fps 60              # 设为 60 以获得约 30 fps 图像
```

启动后弹出一个图像窗口：

```
Camera_0_*   # finger 相机
```

控制台输出触觉数据（启用时）及 finger 开合距离。

### 7.2 双指 Demo

在独立终端中分别启动左右 finger：

```shell
# 终端 1 — 左 finger
./start_finger.cpp left

# 终端 2 — 右 finger
./start_finger.cpp right
```

启动后弹出两个图像窗口（每指一个）。

需要更高相机帧率时：

```shell
./start_finger.cpp left --camera-fps 60
./start_finger.cpp right --camera-fps 60
```

### 7.3 设备工具命令

运行以下命令时**不要**同时启动 `start_finger.cpp` 或其他控制程序。

需先完成编译（`cmake .. && make`），再使用 `./camera_cmd.sh`：

**单设备：**

```shell
./camera_cmd.sh MCUID      # 设备 ID
./camera_cmd.sh camerarc   # 相机标定（单相机）
```

**双设备（左 / 右）：**

```shell
./camera_cmd.sh left MCUID
./camera_cmd.sh left camerarc

./camera_cmd.sh right MCUID
./camera_cmd.sh right camerarc
```

finger 新设备只有 1 个相机，常用标定命令为 `camerarc`。标定 YAML 文件保存至 `calib_result/`（如 `cam0_sensor_left.yaml`）。

## 8 常见问题

| 问题                | 解决方法                                                      |
| ----------------- | --------------------------------------------------------- |
| 找不到串口             | 执行 `sudo apt remove brltty`，重新插拔设备                        |
| 相机或串口路径不对         | 检查 udev 规则，见 [docs/usb-setup_CN.md](docs/usb-setup_CN.md) |
| 相机帧率偏低            | 使用 `--camera-fps 60` 到30fps                                       |
| 笔记本无预览            | 尝试 `--stream-mode` 关闭 trigger 模式                           |
| `camera_cmd.sh` 报错 | 先编译：`mkdir -p build && cd build && cmake .. && make`     |
| `start_finger.cpp` 链接失败 | 确认 `make` 完成且存在 `build/libfinger_cpp_sdk_static.a`         |
| 编译找不到 OpenCV      | 安装：`sudo apt install libopencv-dev pkg-config`             |
| 无相机预览             | 检查 udev 相机软链接；用 `v4l2-ctl --list-devices` 验证              |

## 9 文档索引

| 说明           | 链接                                                                               |
| ------------ | -------------------------------------------------------------------------------- |
| USB 配置 (ZH)  | [docs/usb-setup_CN.md](docs/usb-setup_CN.md)                                     |
| USB setup (EN) | [docs/usb-setup.md](docs/usb-setup.md)                                           |
| udev 规则模板    | [config/99-usb-serial.rules](config/99-usb-serial.rules)                         |
| Demo 启动脚本    | [start_finger.cpp](start_finger.cpp)                                             |
| 标定辅助脚本       | [camera_cmd.sh](camera_cmd.sh)                                                   |
| SDK 头文件      | [include/](include/)                                                             |
