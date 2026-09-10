# CameraApp

CameraApp 是一个基于 LVGL 9 / CMake 的 embedded Linux 相机应用，主要面向 M5CardputerZero / CM0 的 APPLaunch 集成使用。应用本身会安装为独立程序，同时提供 APPLaunch 的 `.desktop` 入口、图标、字体和音效资源。

## 目录约定

当前工程推荐保持在完整 workspace 中：

```text
CameraApp/
├── src/
│   ├── app/
│   ├── config/
│   ├── input/
│   ├── services/
│   ├── ui/
│   ├── viewmodels/
│   └── views/
├── cmake/
│   ├── templates/
│   └── toolchains/
├── assets/
└── CMakePresets.json
```

CM0 交叉编译默认使用 `cmake/toolchains/cp0-aarch64-linux-gnu.cmake`，sysroot 默认放在 `.cache/sdk_bsp-src`。

## Debian 环境准备

### 1. 基础工具

```bash
sudo apt update
sudo apt install -y \
  build-essential \
  cmake \
  git \
  pkg-config \
  dpkg-dev
```

`dpkg-dev` 提供 `dpkg-deb`，用于生成 Debian 包。

### 2. Desktop 本地预览依赖

Desktop 模式用于本机 SDL/LVGL 预览，音频统一使用 miniaudio；Linux 上使用 PulseAudio 后端。

```bash
sudo apt install -y \
  libsdl2-dev \
  libfreetype-dev \
  libpng-dev \
  libjpeg-dev \
  zlib1g-dev \
  libminiaudio-dev \
  libpulse0
```

### 3. CM0 / arm64 交叉编译依赖

如果在 Debian x86_64 上交叉编译 arm64，需要先开启 arm64 multiarch：

```bash
sudo dpkg --add-architecture arm64
sudo apt update
```

然后安装交叉编译器和 arm64 依赖：

```bash
sudo apt install -y \
  gcc-aarch64-linux-gnu \
  g++-aarch64-linux-gnu \
  libfreetype-dev:arm64 \
  libpng-dev:arm64 \
  libjpeg-dev:arm64 \
  zlib1g-dev:arm64 \
  libcamera-dev:arm64 \
  libfmt-dev:arm64 \
  libcjson-dev:arm64
```

CameraApp 的 CM0 sysroot 必须提供 `usr/include/miniaudio.h`，目标系统运行时需要
`libpulse0`。除 DRM display backend 外，其它依赖都是必需项；CMake 找不到会直接报错。
fbdev 是默认 display backend。只有需要 DRM/KMS 时才安装 `libdrm-dev:arm64` 并打开
`APP_USE_DRM`：

```bash
-DAPP_USE_DRM=ON
```

## CMake 编译

以下命令不包含 test 目标，适合只上传应用源码、不上传 `test/` 目录的场景。

### Desktop 编译

```bash
cd /path/to/debian_workspace/projects/CameraApp
cmake --preset linux-x86-64
cmake --build --preset linux-x86-64-rel
```

运行 desktop 版本：

```bash
./build/linux-x86-64/Release/camera_app
```

### CM0 / arm64 编译

```bash
cd /path/to/debian_workspace/projects/CameraApp
cmake --preset cp0-cross
cmake --build --preset cp0-cross-rel
```

如果需要显式指定 sysroot：

```bash
cmake -S . -B build/cp0-cross \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/cp0-aarch64-linux-gnu.cmake \
  -DUSE_DESKTOP=OFF \
  -DCM0_SDK_ROOT=/path/to/sysroot \
  -DCMAKE_SYSROOT=/path/to/sysroot
cmake --build build/cp0-cross --config Release --target camera_app --parallel 4
```

如果需要启用 DRM：

```bash
cmake --preset cp0-cross -DAPP_USE_DRM=ON
```

## Debian 打包

可以通过 `CAMERA_APP_VERSION` 覆盖包版本，无需修改 `CMakeLists.txt`：

```bash
./package_deb.sh -DCAMERA_APP_VERSION=0.3.7
```

推送到 `main` 后，`.github/workflows/release.yml` 会读取最新 GitHub Release，将补丁版本号
自动加一，交叉编译 arm64 Debian 包并发布新的 Release。也可以手动运行 workflow，并输入
明确的 `MAJOR.MINOR.PATCH` 版本号。

推荐使用仓库内脚本打包，它会调用 CMake preset 构建 `camera_app`，再用 CPack 生成 `.deb`。

```bash
cd /path/to/debian_workspace/projects/CameraApp
./package_deb.sh
```

默认输出：

```text
dist/Camera_0.3.7_m5stack1_arm64.deb
```

可选参数：

```bash
./package_deb.sh -DAPP_USE_DRM=ON
./package_deb.sh -DCM0_SDK_ROOT=/path/to/sysroot -DCMAKE_SYSROOT=/path/to/sysroot
```

检查包内容：

```bash
dpkg-deb -c dist/Camera_0.3.7_m5stack1_arm64.deb
```

至少应包含这些路径：

```text
/usr/bin/camera_app
/usr/share/APPLaunch/applications/camera_app.desktop
/usr/share/APPLaunch/share/images/camera1.png
/usr/share/camera_app/fonts/...
/usr/share/camera_app/audio/...
/usr/share/CameraApp/assets/fonts/...
/usr/share/CameraApp/assets/audio/...
```

在目标板安装：

```bash
sudo apt install ./dist/Camera_0.3.7_m5stack1_arm64.deb
```

或者复制到目标板后安装：

```bash
scp dist/Camera_0.3.7_m5stack1_arm64.deb pi@pi:~/
ssh pi@pi 'sudo apt install ./Camera_0.3.7_m5stack1_arm64.deb'
```

## 安装后的资源路径

打包安装后，应用使用以下路径：

```text
/usr/bin/camera_app
/usr/share/APPLaunch/applications/camera_app.desktop
/usr/share/APPLaunch/share/images/camera1.png
/usr/share/camera_app/fonts
/usr/share/camera_app/audio
/usr/share/CameraApp/assets/fonts
/usr/share/CameraApp/assets/audio
```

运行时也可以通过环境变量覆盖 asset root：

```bash
CAMERA_APP_ASSET_DIR=/custom/assets /usr/bin/camera_app
```
