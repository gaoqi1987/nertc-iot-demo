# NERtc ESP32 集成指南

本文档面向在 **xiaozhi-esp32** 项目基础上集成网易云信 NERtc IoT SDK 的开发者，说明如何将 NERtc 能力引入现有工程。

> **SDK 版本**：1.2.4
> **ESP-IDF 版本**：release-v5.4 / release-v5.5（兼容，见注意事项）
> **Demo 工程路径**：`demo/esp32/`

---

## 目录

1. [依赖库说明](#1-依赖库说明)
2. [资源文件](#2-资源文件)
3. [分区配置](#3-分区配置)
4. [代码集成](#4-代码集成)
5. [Kconfig 配置项说明](#5-kconfig-配置项说明)
6. [本地配置文件（config.bin）](#6-本地配置文件configbin)
7. [构建与烧录](#7-构建与烧录)
8. [进阶功能](#8-进阶功能)

---

## 1. 依赖库说明

### 1.1 组件库一览

| 组件名 | 说明 | 是否必须 |
|--------|------|----------|
| `nertc_sdk` | 网易云信 IoT SDK 核心库，提供音频流传输和 AI 对话能力 | **必须** |
| `nertc_wake_up` | 云信自定义唤醒词 SDK | 使用自定义唤醒词时必须 |
| `esp-ml307` | 4G 模组（ML307/EC801E/NT26K）的 AT 命令网络库，位于 `components/esp-ml307`，由本项目独立维护 | 使用 4G 模组时按需参考 |
| `esp-hosted`（仆从芯片固件） | ESP32-P4 无内置 WiFi，需外挂一颗 ESP32（如 ESP32-C6）作为仆从芯片提供网络，仆从芯片运行 esp-hosted slave 固件。参考仓库：https://github.com/espressif/esp-hosted | 使用 P4 + 仆从芯片方案时必须 |

### 1.2 CMakeLists.txt 配置

上述组件需在 `main/CMakeLists.txt` 的 `idf_component_register` 的 `PRIV_REQUIRES` 中声明，否则链接阶段会报符号缺失错误。

```cmake
idf_component_register(SRCS ${SOURCES}
    ...
    PRIV_REQUIRES
        ...
        nertc_sdk
        nertc_wake_up       # 使用自定义唤醒词时
        ...
)
```

> **IDF 版本注意**：
> - **IDF 5.4**：组件会自动被 CMake 检测到，无需额外处理。
> - **IDF 5.5+**：`touch_element` 组件在 IDF 5.5.2 中被移除，已通过 `idf_component.yml` 条件依赖处理。若遇到编译错误，请确认 `main/idf_component.yml` 中相关版本约束与你的 IDF 版本匹配。

---

## 2. 资源文件

### 2.1 本地化语音文件

路径：`main/assets/locales/zh-CN/`

此目录存放设备本地播放的提示音（如连接中、等待、错误提示等）。可参考示例工程中的音频文件，按需加载到自己的工程中。若不需要中文提示音，可跳过。

### 2.2 本地配置文件（config.bin）

路径：`create_local_config/`

该目录提供生成配置文件 `config.bin` 所需的模板和工具脚本。`config.bin` 以 SPIFFS 格式烧入设备的 `custom` 分区，主要用于配置 `appkey` 等运行参数。

> 如果 appkey 直接硬编码在固件中，可不使用此配置文件。详细说明见第 [6 节](#6-本地配置文件configbin)。

### 2.3 蓝牙配网二进制文件

路径：`third_party/blufi_app/`

用于蓝牙配网（BluFi）和小程序操作的独立可执行文件，烧入设备的 `blufi` 分区。

> **注意**：部分特殊版本或带 display 的硬件可能无法正常使用，遇到兼容问题请联系技术支持。

---

## 3. 分区配置

示例工程使用 `partitions/v2/` 下的分区表，针对不同芯片型号分别提供。

### 3.1 ESP32-S3（16MB Flash）

文件：`partitions/v2/16m.csv`

| 分区名 | 类型 | 用途 | 偏移 | 大小 |
|--------|------|------|------|------|
| `nvs` | data/nvs | 系统 NVS 存储 | 0x9000 | 16KB |
| `otadata` | data/ota | OTA 状态数据 | 0xD000 | 8KB |
| `phy_init` | data/phy | PHY 初始化数据 | 0xF000 | 4KB |
| `custom` | data/spiffs | **存放 config.bin**（本地配置） | 0x10000 | 128KB |
| `ota_0` | app/ota_0 | 主应用程序 | 0x30000 | 4928KB |
| `blufi` | app/ota_1 | **存放 blufi_app.bin** | 0x520000 | 2944KB |
| `assets` | data/spiffs | 资源文件（音频、字体等） | 0x810000 | 8128KB |

### 3.2 ESP32-C3（16MB Flash）

文件：`partitions/v2/16m_c3.csv`

| 分区名 | 类型 | 用途 | 偏移 | 大小 |
|--------|------|------|------|------|
| `nvs` | data/nvs | 系统 NVS 存储 | 0x9000 | 16KB |
| `otadata` | data/ota | OTA 状态数据 | 0xD000 | 8KB |
| `phy_init` | data/phy | PHY 初始化数据 | 0xF000 | 4KB |
| `custom` | data/spiffs | **存放 config.bin** | 0x10000 | 64KB |
| `ota_0` | app/ota_0 | 主应用程序 | 0x20000 | 4000KB |
| `ota_1` | app/ota_1 | 备用 OTA | （接续） | 4000KB |
| `assets` | data/spiffs | 资源文件 | 0x800000 | 4000KB |

---

## 4. 代码集成

### 4.1 闹钟模块（`main/alarm/`）

| 文件 | 说明 |
|------|------|
| `alarm_manager.cc/h` | 闹钟调度与管理核心逻辑 |
| `ccronexpr.c/h` | Cron 表达式解析库（第三方，用于定时触发） |

当 `CONFIG_CONNECTION_TYPE_NERTC` 开启时，CMake 自动编译上述文件。如不需要闹钟功能，可从 CMakeLists.txt 中移除对应条目，并屏蔽 `application.cc` / `application_nertc.cc` 中相关调用。

### 4.2 Board 公共模块（`main/boards/common/`）

#### 4.2.1 `board.cc`

云信扩展了以下接口：

- **`Board::GetSystemInfoJson()`**：追加 OTA 鉴权流程，上报设备信息（型号、固件版本）及云音乐功能开关等字段。
- **`Board::GetBoardName()`**：返回设备型号字符串，用于 OTA 鉴权和系统信息上报。
- **`Board::StartBlufiMode()`**：启动蓝牙配网流程（BluFi），支持小程序操作设备入网。
- **`Board::StartBlufiOtaMode()`**：蓝牙配网模块内的 OTA 升级流程。

#### 4.2.2 `dual_network_board.cc`

添加了 `Board::GetBoardName()` 的实现，适用于同时支持 WiFi 和 4G 的双网络板型。非双网络板型可不添加。

#### 4.2.3 `wifi_board.cc`

新增进入蓝牙配网模式的入口逻辑，当用户触发配网操作时调用 `StartBlufiMode()`。

### 4.3 显示模块（`main/display/`）

#### 4.3.1 `main/display/display.h`

1、追加 `SetEmotionForce()` 方法，用于设置表情强制播放。需要在本地对应的 display 中添加 `SetEmotionForce()` 方法。
2、如果不需要强制播放表情，可以在 `main\protocols\nertc_protocol.cc` 中将 `SetEmotionForce()` 方法改成 `SetEmotion()`。然后放弃`main/display/display.h`的修改。

### 4.4 云音乐播放器（`main/music_player/`）

| 文件 | 说明 |
|------|------|
| `music_player.cc/h` | 云音乐播放器逻辑 |
| `mp3_online_player.cc/h` | MP3 在线流播放实现 |

**使用前提**：
1. 后台智能体已配置云音乐 MCP。
2. `menuconfig` 中开启 `CONFIG_USE_MUSIC_PLAYER`（仅 ESP32-S3/P4 + PSRAM 支持）。
3. OTA 同步接口返回中包含云音乐权限字段，且后台已开启设备的云音乐权限。

### 4.5 协议层（`main/protocols/`）

#### 4.5.1 `protocol.h`

云信扩展了 Protocol 基类接口，新增 `SetAISleep()`、`SendTTSText()` 等空实现，便于 `NeRtcProtocol` 在 NERtc 通道上实现这些扩展功能。

#### 4.5.2 `nertc_protocol.cc`

**核心文件**。封装了云信 AI 通道的完整通信逻辑，包括：
- NERtc 引擎的创建、初始化、加入房间、AI 启动与销毁流程。
- 音频帧推送（PCM / OPUS）与接收回调。
- 从 SPIFFS 分区读取 `config.json`，加载 appkey、证书、音频参数等本地配置。
- 服务器下发 JSON 消息的解析（如 `system.sleep`、`updateSongList`、`alarm`、`app` 等扩展指令）。

#### 4.5.3 `nertc_external_network.cc`

`NeRtcExternalNetwork` 是外部网络抽象层，封装了 HTTP、TCP、UDP、MQTT 的具体实现，并通过函数指针表（`ext_net_handle`）暴露给 NERtc SDK。SDK 收到该 handle 后，会将所有网络 I/O 委托给应用侧提供的实现，**完全绕过 SDK 内置的网络栈**。

**需要设置 `ext_net_handle` 的场景：**

| 场景 | 说明 |
|------|------|
| 4G 模块（ML307/EC801E 等） | SDK 内置网络栈依赖 lwIP socket API，4G 模块有独立的网络接口，必须通过外部 handle 桥接。参考实现见 `components/esp-ml307`（详见[第 8.4 节](#84-4g-模组接入esp-ml307)） |
| **ESP32-P4 + ESP-Hosted 仆从芯片** | **P4 无内置 WiFi**，通过 SPI/SDIO 连接仆从 ESP32 提供网络，网络操作需通过 esp-hosted host API 实现，必须设置外部 handle。仆从芯片固件参考 https://github.com/espressif/esp-hosted（详见[第 8.5 节](#85-esp32-p4--esp-hosted-仆从芯片网络接入)） |
| **IDF 版本不一致** | SDK 预编译库的 IDF 版本与用户工程不同时，SDK 内置网络栈可能存在 ABI 不兼容（如 `esp_http_client`、socket 结构体布局变化），导致运行时崩溃或连接异常。此时需要设置 `ext_net_handle`，让 SDK 调用用户工程编译的网络实现 |

**WiFi + IDF 版本一致**时，`ext_net_handle` 可设为 `nullptr`，SDK 使用内置网络栈。

**设置方式**（在 `nertc_protocol.cc` 的引擎初始化处）：

```cpp
// 方式一：仅 4G 模块才启用（默认写法）
if (Board::GetInstance().GetBoardType() == "ml307") {
    engine_config.ext_net_handle = NeRtcExternalNetwork::GetInstance()->GetHandle();
}

// 方式二：IDF 版本与 SDK 不一致时，WiFi 也需要启用（始终设置）
engine_config.ext_net_handle = NeRtcExternalNetwork::GetInstance()->GetHandle();
```

> **排查建议**：若使用 WiFi 方案但出现 SDK 连接失败、崩溃在网络相关调用栈中，可优先尝试方式二，即无条件设置 `ext_net_handle`。

### 4.6 Application 层

#### 4.6.1 `application.h`

云信扩展需要在头文件中添加：
- 包含 `music_player/music_player.h`、`alarm/alarm_manager.h` 等模块头文件。
- `AecMode` 枚举新增 `kAecOnNertc` 类型（对应云信服务器端 AEC）。
- `CONFIG_CONNECTION_TYPE_NERTC` 宏保护下的成员变量和接口声明。

#### 4.6.2 `application.cc`

主要集成点，涉及以下扩展逻辑（均通过编译宏隔离）：

| 宏 | 扩展内容 |
|----|----------|
| `CONFIG_CONNECTION_TYPE_NERTC` | 创建 `NeRtcProtocol` 实例，初始化云信通道 |
| `CONFIG_USE_MUSIC_PLAYER` | 初始化云音乐播放器，处理 `updateSongList` 指令 |
| `CONFIG_USE_NERTC_PTT_MODE` | 启用按键对讲（PTT）模式逻辑 |
| `CONFIG_USE_NERTC_SERVER_AEC` | 切换到云端 AEC 模式，使用 PCM 推送接口 |

其他关键扩展：
- `protocol_->OnAudioChannelClosed`：关闭音频通道后异步设置表情为 `sleepy`。
- `protocol_->OnIncomingJson`：解析 `system->sleep`、`app`、`alarm` 等服务器下发的扩展消息。
- `ai_sleep_` 标志位管理，完善 AI 休眠逻辑。

#### 4.6.3 `application_nertc.cc`

云信功能在 Application 中的具体实现文件。包含 NERtc 协议初始化、设备信息上报、OTA 同步时的云信扩展逻辑等，与 `application.cc` 解耦，便于维护。

使用内置唤醒词或不使用唤醒词时，无需修改此文件。

### 4.7 main/CMakeLists.txt

以下是 NERtc 集成相关的 CMake 配置要点：

```cmake
# 1. 当 CONFIG_CONNECTION_TYPE_NERTC 开启时，自动添加 NERtc 相关源文件
if(CONFIG_CONNECTION_TYPE_NERTC)
    list(APPEND INCLUDE_DIRS "alarm")
    list(APPEND SOURCES "application_nertc.cc")
    list(APPEND SOURCES "protocols/nertc_protocol.cc")
    list(APPEND SOURCES "protocols/nertc_external_network.cc")
    list(APPEND SOURCES "alarm/alarm_manager.cc")
    list(APPEND SOURCES "alarm/ccronexpr.c")
endif()

# 2. 云音乐播放器（可选）
if(CONFIG_USE_MUSIC_PLAYER)
    list(APPEND INCLUDE_DIRS "music_player")
    list(APPEND SOURCES "music_player/mp3_online_player.cc")
    list(APPEND SOURCES "music_player/music_player.cc")
endif()

# 3. PRIV_REQUIRES 中添加 NERtc 库
idf_component_register(SRCS ${SOURCES}
    ...
    PRIV_REQUIRES
        ...
        nertc_sdk
        nertc_wake_up
        ...
)
```

### 4.8 main/idf_component.yml

`nertc_sdk`、`nertc_wake_up` 均作为私有组件存在于工程本地，无需在 `idf_component.yml` 中声明。该文件主要管理来自 ESP Component Registry 的第三方依赖，IDF 5.4 / 5.5 版本差异部分已通过 `matches` 条件处理。

### 4.9 main/Kconfig.projbuild

见下节 [Kconfig 配置项说明](#5-kconfig-配置项说明)。

### 4.10 main/mcp_server.cc

云信相关扩展：
- **`self.good_bye`** MCP Tool：让设备主动退出 AI 对话。
- **`self.photo_explain`** MCP Tool：触发摄像头拍照并上传识别。
- **`self.cancel_alarm_ringing`** MCP Tool：取消正在响铃的闹钟。
- **服务器动态禁用 Tool**：解析 `capabilities.disableTools` 字段，配合 `IsToolDisabled()` 接口，支持服务器动态下发不支持的 Tool 列表。
- **`GetToolsList()`**：`max_payload_size` 扩大至 `8000`，避免 Tool 列表过长时被截断。

### 4.11 main/ota.cc

云信 OTA 扩展：
- **`Ota::GetCheckVersionUrl()`**：当 `CONFIG_CONNECTION_TYPE_NERTC` 开启时，使用云信的 OTA 服务地址（`https://nrtc.netease.im/v1/ota`）替换默认地址。
- **OTA 返回解析**：从 OTA 响应中解析 `agent` 字段，用于更新智能体配置。
- **`Ota::Upgrade()`**：使用 `GetNextSafePartition()` 替代原生的 `esp_ota_get_next_update_partition()`，确保在有 `blufi` 分区的情况下 OTA 写入正确的目标分区。

### 4.12 分区表文件

详见[第 3 节](#3-分区配置)。

---

## 5. Kconfig 配置项说明

在 `idf.py menuconfig` → **Xiaozhi Assistant** 菜单下可找到以下云信相关配置项。

### 5.1 核心开关

| 配置项 | 默认值 | 说明 |
|--------|--------|------|
| `CONFIG_CONNECTION_TYPE_NERTC` | `y` | **NERtc 能力总开关**，开启后编译所有 NERtc 相关代码 |
| `CONFIG_OTA_URL` | `https://nrtc.netease.im/v1/ota` | OTA 检查地址，集成云信后默认指向云信 OTA 服务 |

### 5.2 音频模式

| 配置项 | 默认值 | 说明 |
|--------|--------|------|
| `CONFIG_USE_NERTC_SERVER_AEC` | `n` | 开启云信服务器端 AEC（回声消除），与本地设备 AEC 互斥，与 PTT 模式互斥 |
| `CONFIG_USE_NERTC_PTT_MODE` | `n` | 开启按键对讲模式（PTT），按住说话松开停止，与 Server AEC 互斥 |

> 三种 AEC 模式说明：
> - **服务器 AEC**（`CONFIG_USE_NERTC_SERVER_AEC`）：推送 PCM 帧，服务端处理回声，需同时推送参考帧。
> - **本地设备 AEC**（`CONFIG_USE_DEVICE_AEC`）：设备本地处理，推送 OPUS 编码帧。
> - **无 AEC**（两者均关闭）：推送 OPUS 编码帧，无自动打断功能。

### 5.3 唤醒词

| 配置项 | 默认值 | 说明 |
|--------|--------|------|
| `CONFIG_USE_CUSTOM_WAKE_WORD` | 视芯片而定 | 使用云信自定义唤醒词（Multinet 模型），需要 ESP32-S3/P4 + PSRAM |
| `CONFIG_SR_MN_CN_MULTINET7_AC_QUANT` | y | 自定义唤醒词推荐使用 multinet7 模型，该配置在 sdkconfig.defaults.esp32s3 |

## 6. 本地配置文件（config.bin）

设备从 `custom` SPIFFS 分区读取 `config.json`，在运行时加载 appkey 等参数，无需重新编译固件即可更换 appkey。

### 6.1 config.json 格式说明

```json
{
    "appkey": "your_appkey_here",
    "audio_config": {
        "frame_size": 60
    },
    "license_config": {
        "license": ""
    }
}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| `appkey` | string | **必填**，云信控制台分配的 App Key |
| `audio_config.frame_size` | number | 音频帧时长（ms），推荐值 `20` 或 `60` |
| `license_config.license` | string | SDK 授权证书（如留空则使用服务器同步证书） |

### 6.2 使用 config.py 生成并烧录 config.bin（推荐）

工程提供了 `config.py` 脚本，能自动读取分区表确定烧录地址和镜像大小，无需手动计算偏移量。

**Step 1：填入 appkey**

根据目标芯片编辑对应的模板文件：

- **ESP32-S3 / P4**：编辑 `create_local_config/config.json.s3`
- **ESP32-C3 及其他**：编辑 `create_local_config/config.json.c3`

将 `appkey` 字段替换为从云信控制台获取的真实 App Key。

**Step 2：生成并烧录**

```bash
# ESP32-S3（默认 target，可省略 --target）
python3 config.py -p COM6 --build --flash --target esp32-s3

# ESP32-C3
python3 config.py -p COM6 --build --flash --target esp32-c3

# 同时烧录蓝牙配网固件（blufi）
python3 config.py -p COM6 --build --flash --blufi --target esp32-s3
```

脚本执行流程：
1. 根据 `--target` 自动选择分区表（`partitions/v2/16m.csv` 或 `16m_c3.csv`）
2. 从分区表解析 `custom` 分区的偏移地址和大小
3. 将模板配置文件复制到 `local_config/config.json`
4. 调用 `create_local_config/spiffsgen.py` 生成 `config.bin`
5. 调用 esptool 将 `config.bin` 烧入 `custom` 分区
6. 若指定 `--blufi`，额外将 `third_party/blufi_app/bin/blufi_app.bin`（C3 为 `blufi_app_c3.bin`）烧入 `0x520000`

> **参数说明**：
> - `-p`：串口号（Windows 示例：`COM6`；Linux 示例：`/dev/ttyUSB0`）
> - `--build`：生成 config.bin
> - `--flash`：烧录 config.bin（需 `-p`）
> - `--blufi`：同时烧录蓝牙配网固件（需 `-p`）
> - `--target`：目标芯片，默认 `esp32-s3`，可选 `esp32-c3`、`esp32-p4`
> - `-o`：自定义输出文件路径，默认 `config.bin`
> - `-i`：自定义输入目录，默认自动从 `create_local_config/` 复制模板

---

## 7. 构建与烧录

### 7.1 固件构建与烧录

```bash
cd demo/esp32

# 配置目标芯片（idf.py 使用下划线格式，如 esp32s3）
idf.py set-target esp32s3

# 打开菜单配置
idf.py menuconfig

# 构建
idf.py build

# 烧录并监视日志
idf.py -p /dev/ttyUSB0 flash monitor
```

### 7.2 烧录本地配置与蓝牙固件（config.py）

使用工程自带的 `config.py` 脚本统一处理 config.bin 生成、烧录及蓝牙配网固件烧录，脚本会自动从分区表读取偏移地址，无需手动计算。

```bash
# 1. 仅生成 config.bin，不烧录（用于检查生成结果）
python3 config.py --build --target esp32-s3

# 2. 生成并烧录 config.bin
python3 config.py -p COM6 --build --flash --target esp32-s3

# 3. 生成并烧录 config.bin，同时烧录蓝牙配网固件
python3 config.py -p COM6 --build --flash --blufi --target esp32-s3

# 4. ESP32-C3 示例
python3 config.py -p COM6 --build --flash --blufi --target esp32-c3
```

> `config.py` 常用参数一览：
>
> | 参数 | 说明 |
> |------|------|
> | `-p PORT` | 串口号，Windows 如 `COM6`，Linux 如 `/dev/ttyUSB0` |
> | `--build` | 生成 config.bin（自动从模板复制 config.json） |
> | `--flash` | 将 config.bin 烧入 `custom` 分区（需 `-p`） |
> | `--blufi` | 将蓝牙配网固件烧入 `blufi` 分区，地址 `0x520000`（需 `-p`） |
> | `--target` | 目标芯片，默认 `esp32-s3`，可选 `esp32-c3`、`esp32-p4` |
> | `-o FILE` | 自定义输出 bin 文件路径，默认 `config.bin` |
> | `-i DIR` | 自定义输入目录，默认自动使用 `create_local_config/` 中的模板 |

---

## 8. 进阶功能

### 8.1 自定义唤醒词

自定义唤醒词基于 ESP-SR Multinet 模型，支持在 ESP32-S3 / P4 + PSRAM 硬件上运行。

**配置步骤**：

1. 在 `menuconfig` 中选择 **Wake Word Implementation Type** → `Multinet model (Custom Wake Word)`。
2. 在 `menuconfig`中选择 **ESP Speech Recognition** → `Chinese Speech Commands Model（chinese recognition for air conditioner controller (mn7_cn_ac)）`
3. 若assets.bin 是自定义的，由于小智提供的xiaozhi-assets-generator 网页生成工具在选择自定义唤醒模型不支持mn7_cn的，为此您需要参考我们fork出来的项目 https://github.com/netease-im/xiaozhi-assets-generator 进行代码修改[仅仅两行修改]，让assets.bin 中一定是包含mn7_cn的模型。

> 自定义唤醒词参考文档：待完善。如有需求，请联系技术支持。

### 8.2 蓝牙配网（BluFi）

蓝牙配网依赖预编译的独立固件文件，烧入 `blufi` 分区（偏移 `0x520000`）：
- **ESP32-S3**：`third_party/blufi_app/bin/blufi_app.bin`
- **ESP32-C3**：`third_party/blufi_app/bin/blufi_app_c3.bin`

**烧录蓝牙固件**：使用 `config.py` 的 `--blufi` 参数一并烧录，无需单独操作：

```bash
# 同时生成 config.bin、烧录 config.bin 和蓝牙配网固件
python3 config.py -p COM6 --build --flash --blufi --target esp32-s3
```

若只需单独烧录蓝牙固件（不更新 config.bin）：

```bash
python3 config.py -p COM6 --blufi --target esp32-s3
```

> **注意**：部分特殊版本或带 display 的硬件可能存在兼容问题，遇到问题请联系技术支持。

### 8.3 云音乐播放

云音乐功能需要以下前提条件全部满足：
- 后台智能体已配置并启用云音乐 MCP 服务。
- `menuconfig` 中开启 `CONFIG_USE_MUSIC_PLAYER`（仅 ESP32-S3/P4 + PSRAM）。
- OTA 同步返回中包含云音乐权限字段，且后台已开启该设备的云音乐访问权限。

服务器通过 `updateSongList` JSON 指令下发歌单，设备收到后更新本地播放列表并开始播放。参考接入官方文档：https://doc.yunxin.163.com/emotional-ai/guide/jE5NDExNzc?platform=client，注意当前版本已经废弃了music_player_api.h和music_player_api.c 文件。

### 8.4 4G 模组接入（esp-ml307）

#### 8.4.1 概述

`components/esp-ml307` 是本项目独立维护的 4G 模组网络库，支持通过 AT 命令驱动以下模组：

| 模组 | 说明 |
|------|------|
| ML307R / ML307A | 移远 Cat.1 模组（主要支持目标） |
| EC801E | 移远 Cat.1 模组，需确认固件是否支持 SSL TCP |
| NT26K | 需确认固件是否支持 SSL TCP |

提供的协议支持：HTTP/HTTPS、MQTT/MQTTS、TCP/SSL TCP、UDP、WebSocket。

#### 8.4.2 两种引入方式

**方式一：使用本项目本地维护版本（推荐）**

直接使用工程中的 `components/esp-ml307`，已在本地针对实际使用场景做了若干稳定性修复（见下节）。

**方式二：通过 ESP Component Registry 引入上游版本**

`main/idf_component.yml` 中预留了上游组件的注释入口，如需使用可取消注释：

```yaml
# main/idf_component.yml
dependencies:
  # 78/esp-ml307: ~3.6.4   ← 取消注释即可通过 idf.py update-dependencies 拉取
```

> 上游版本（`78/esp-ml307`）是社区公开版本，本项目本地修复不一定已合并回上游，使用上游版本时需自行评估稳定性。

#### 8.4.3 使用建议

> **用户有自己稳定的网络库版本时，应优先使用自己工程的网络库**，无需引入 `components/esp-ml307`。

`components/esp-ml307` 主要作为**参考实现**，适用于以下场景：

| 场景 | 说明 |
|------|------|
| 自研网络库遇到问题 | 可对照 `components/esp-ml307` 的 AT 命令交互逻辑排查 |
| 接入新模组或新功能 | 参考已有模组的实现模式（ML307/EC801E 并列实现） |
| 还没有自己的网络库 | 可直接使用 `components/esp-ml307` 作为起点，按需修改 |

#### 8.4.4 本地版本的重要修复

本地维护版本相对上游做了以下关键修复，自研网络库遇到类似问题可参考对应文件：

| 修复内容 | 涉及文件 |
|----------|----------|
| HTTP 死锁：`OnTcpData` 在 `write_cv_` 等待前持有 `mutex_`，TCP 断开路径同时尝试加锁，触发看门狗 | `src/http_client.cc` |
| `cv_` 在 `Read()` 和 `ReadAll()` 中使用了不同的锁，导致潜在的等待无法被唤醒 | `src/http_client.cc` |
| TCP 断开回调重入及 use-after-free：用原子 `callback_called_` 保证回调只触发一次；`HttpClient::Close()` 始终调用 `Disconnect()` 等待 `ReceiveTask` 退出；重新连接前清空旧连接的回调 | `src/esp/esp_tcp.cc`、`src/http_client.cc` |
| ML307 HTTP 连接关闭时 connect ID 未重置，影响连接复用 | `src/ml307/ml307_http.cc` |

#### 8.4.5 与 NERtc 的衔接

使用 4G 模组时，需通过 `nertc_external_network.cc` 将 `esp-ml307` 的网络能力桥接给 NERtc SDK（详见[第 4.5.3 节](#453-nertc_external_networkcc)）。`components/esp-ml307` 中的 `NetworkInterface` 抽象基类与 `NeRtcExternalNetwork` 的外部 handle 设计一致，可直接对接。

```cpp
// nertc_protocol.cc 引擎初始化处（4G 模组方案）
engine_config.ext_net_handle = NeRtcExternalNetwork::GetInstance()->GetHandle();
```

`NeRtcExternalNetwork` 内部的 HTTP、TCP、UDP、MQTT 实现，可参照 `components/esp-ml307/src/ml307/` 下对应文件（`ml307_http.cc`、`ml307_tcp.cc` 等）进行适配。

### 8.5 ESP32-P4 + ESP-Hosted 仆从芯片网络接入

#### 8.5.1 适用场景

**ESP32-P4 无内置 WiFi/BT**，需要外挂一颗 ESP32 模组（如 ESP32-C6、ESP32-S3）作为仆从芯片（Slave）提供 WiFi 网络能力。仆从芯片通过 SPI 或 SDIO 与 P4 通信，运行 esp-hosted slave 固件后，P4 侧通过 esp-hosted host 驱动访问网络。

> 与 4G 模组方案的区别：同为"外部网络"，4G 模组走 AT 命令 + 蜂窝网，esp-hosted 走 SPI/SDIO + WiFi（仆从 ESP32）。NERtc 侧的桥接方式完全相同，均通过 `ext_net_handle`。

#### 8.5.2 仆从芯片固件

仆从 ESP32 需烧录 esp-hosted slave 固件，固件源码及烧录说明见官方仓库：

- 仓库地址：https://github.com/espressif/esp-hosted
- 路径：`esp_hosted_fg/esp/esp_driver/`（FreeRTOS 版）或 `esp_hosted_ng/esp/`（Linux NG 版，推荐用 FreeRTOS 版）
- 支持的传输接口：**SPI**（推荐）、SDIO、UART

选择与 P4 硬件连接方式对应的 slave 目标进行编译烧录，例如：

```bash
cd esp-hosted/esp_hosted_fg/esp/esp_driver
idf.py set-target esp32c6
idf.py -DCONFIG_ESP_SPI_HOST_INTERFACE=y build flash
```

#### 8.5.3 主控（P4）侧适配

P4 侧需在工程中集成 esp-hosted host 驱动，并将其网络能力桥接给 NERtc SDK，方式与 4G 模组完全一致：

```cpp
// nertc_protocol.cc 引擎初始化处（P4 + esp-hosted 方案）
engine_config.ext_net_handle = NeRtcExternalNetwork::GetInstance()->GetHandle();
```

`nertc_external_network.cc` 中 HTTP、TCP、UDP、MQTT 的底层实现，需替换为调用 esp-hosted host 侧提供的 socket API（esp-hosted host 驱动在 P4 上注册标准 netif，可直接使用 lwIP socket，**无需额外适配**）。

> **与 4G 模组的关键区别**：4G 模组的 AT 命令接口无法直接使用 lwIP socket，因此 `esp-ml307` 需要完整实现 HTTP/TCP/UDP/MQTT；而 esp-hosted host 驱动注册了标准 netif，P4 上可直接使用 lwIP socket，`NeRtcExternalNetwork` 中的 WiFi 默认实现（即 `ext_net_handle = nullptr` 的内置路径）理论上可复用，但为确保 ABI 一致性，**建议始终显式设置 `ext_net_handle`**（参见 [第 4.5.3 节](#453-nertc_external_networkcc) 方式二）。

#### 8.5.4 与其他方案对比

| 维度 | WiFi（S3 内置） | 4G 模组（ML307） | ESP-Hosted（P4 专用） |
|------|----------------|-----------------|----------------------|
| 典型主控 | ESP32-S3 | ESP32-S3 / C3 | **ESP32-P4** |
| 网络介质 | 内置 WiFi | 蜂窝 Cat.1 | WiFi（仆从 ESP32） |
| 主控↔网络接口 | 内置 | UART AT 命令 | SPI / SDIO |
| 仆从固件 | 无 | 模组出厂固件 | esp-hosted slave |
| NERtc 桥接 | 可不设置 handle | 必须设置 handle | 建议设置 handle |
| 参考实现 | — | `components/esp-ml307` | https://github.com/espressif/esp-hosted |

## 附录：常见问题

**Q：IDF 5.5 编译报 `touch_element` 相关错误？**
A：`touch_element` 在 IDF 5.5.2 中被移除，已通过 `idf_component.yml` 条件依赖处理。请确认使用的是最新的 `idf_component.yml`，并运行 `idf.py update-dependencies`。

**Q：OTA 升级后设备进入 blufi 分区而非主程序？**
A：请确认 `ota.cc` 中使用了 `GetNextSafePartition()` 而非默认的 `esp_ota_get_next_update_partition()`，后者可能在存在 `blufi` 分区时选错目标分区。

**Q：config.bin 烧录后 appkey 未生效？**
A：检查 `config.json` 格式是否合法（标准 JSON，无注释），以及烧录地址是否与分区表中 `custom` 分区的 Offset 一致。可通过串口日志中的 `local config set appkey to ...` 确认是否读取成功。

**Q：Server AEC 和本地 AEC 能同时开启吗？**
A：不能。两者互斥，`CONFIG_USE_NERTC_SERVER_AEC` 和 `CONFIG_USE_DEVICE_AEC` 不能同时为 `y`。PTT 模式开启时两种 AEC 均不可用。

**Q：WiFi 方案下 SDK 连接失败或崩溃在网络调用栈中？**
A：大概率是 SDK 预编译库的 IDF 版本与用户工程不一致，导致内置网络栈 ABI 不兼容。解决方法：在引擎初始化时无条件设置外部网络 handle，让 SDK 使用应用侧编译的网络实现：

```cpp
engine_config.ext_net_handle = NeRtcExternalNetwork::GetInstance()->GetHandle();
```

详见[第 4.5.3 节](#453-nertc_external_networkcc)。
