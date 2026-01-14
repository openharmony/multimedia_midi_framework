# midi_framework

## 简介

MIDI（Musical Instrument Digital Interface）设备是指符合 MIDI 标准协议的电子乐器、控制器及周边音频设备（如电子琴、电子鼓、打击垫、合成器等）。

`midi_framework` 是 OpenHarmony 操作系统中用于管理和控制 MIDI 设备的模块。它提供统一的 MIDI 设备发现、数据传输及协议解析接口，屏蔽底层硬件差异，使得应用能够方便地通过 Native API 与外部 MIDI 设备进行高性能交互。

midi_framework 包含以下常用功能：

* **设备发现与管理**：支持查询已连接 USB 及 BLE MIDI 设备的列表、热插拔监听及连接 BLE MIDI 设备。
* **高性能数据传输**：支持基于 UMP（Universal MIDI Packet）协议的指令收发。

## 系统架构

![midi_framework部件架构图](figures/zh-cn_image_midi_framework.png)
**图 1** OpenHarmony MIDI 服务架构图

* **MIDI 服务 (midi_server)**: 系统核心服务，提供设备全生命周期管理与数据交互能力。包含**设备管理**（维护设备列表）、**客户端连接管理**（管理应用会话与权限）、**数据传输**（基于共享内存的高性能通道）、**协议转换**（UMP 与 Byte Stream 互转）以及 **USB 适配**和 **蓝牙适配**模块。 midi_server 采用按需启动（On-Demand） 的独立进程模式运行。当应用调用客户端创建接口（OH_MidiClientCreate）时，系统通过 samgr 服务（System Ability Manager）自动拉起 MIDI 服务进程，物理设备的插入不会触发服务启动；当所有客户端销毁且一段时间内无活跃会话时，服务进程将自动退出，以优化系统资源占用。
* **Audio Kit (OHMIDI)**: 音频开发套件中负责 MIDI 能力的接口集合。OHMIDI 为应用层（如 DAW 或 Demo）提供标准 Native API，实现设备列表获取、连接建立及 MIDI 消息收发功能。（注：Audio Kit 还包含 OHAudio 等其他音频接口，与本部件无直接联系且不属于本部件范围）。
* **MIDI Demo / DAW 应用**: 使用 MIDI 能力的应用。**DAW 应用**指待接入的第三方数字音频工作站；**MIDI Demo** 为参考示例（位于test/demo目录下），用于演示和验证 MIDI 设备的连接与指令交互流程。
* **蓝牙服务和 USB 服务**: MIDI 服务依赖的关键系统能力。**USB 服务**负责监听 USB 设备的热插拔状态，并及时通知 MIDI 服务有设备上线或下线；**蓝牙服务**与 MIDI 服务进行交互，负责 BLE MIDI 设备的连接建立及数据 I/O 传输。
* **MIDI 驱动**: 实现 MIDI 硬件抽象的核心组件。它对接 MIDI 服务，负责底层的**端口管理**与**数据 I/O**，屏蔽不同硬件的差异，向服务层提供统一的读写标准。
* **蓝牙驱动和 USB 驱动**: 支撑物理通信的基础驱动。**USB 驱动**确保标准 UAC 设备被内核正确识别；**蓝牙驱动**实现无线协议栈，保障 BLE MIDI 设备连接的稳定性与低延迟传输。

## 目录

仓目录结构如下：


```
/foundation/multimedia/midi_framework      # MIDI部件业务代码
├── bundle.json                            # 部件描述与编译配置文件
├── config.gni                             # 编译配置参数
├── figures                                # 架构图等资源文件
├── frameworks                             # 框架层实现
│   └── native
│       ├── midi                           # MIDI客户端核心逻辑 (Client Context)
│       ├── midiutils                      # 基础工具库
│       └── ohmidi                         # Native API 接口实现
├── interfaces                             # 接口定义
│   ├── inner_api                          # 内部接口
│   └── kits                               # 对外接口
├── sa_profile                             # 系统服务配置文件
├── services                               # MIDI服务层实现
│   ├── common                             # 服务端通用组件 (共享内存, UMP协议处理)
│   ├── etc                                # 进程启动配置 (midi_server.cfg)
│   ├── idl                                # IPC通信接口定义
│   └── server                             # 服务端核心逻辑 (设备管理, 驱动适配, 客户端连接管理)
└── test                                   # 测试代码
    ├── fuzzer                             # Fuzzing测试用例
    └── unittest                           # 单元测试用例

```

## 编译构建

根据不同的目标平台，使用以下命令进行编译：

**编译32位ARM系统midi_framework部件**

```bash
./build.sh --product-name {product_name} --ccache --build-target midi_framework

```

**编译64位ARM系统midi_framework部件**

```bash
./build.sh --product-name {product_name} --ccache --target-cpu arm64 --build-target midi_framework

```

> **说明：**
> `{product_name}` 为当前支持的平台名称，例如 `rk3568`。

## 使用说明

### 接口说明

midi_framework部件向开发者提供了 **Native API**，主要涵盖客户端管理、设备管理及端口操作。主要接口及其功能如下：

**表 2** 接口说明

| 接口名称                  | 功能描述                                                             |
| ------------------------- | -------------------------------------------------------------------- |
| **OH_MidiClientCreate**   | 创建MIDI客户端实例，初始化上下文环境，并可注册设备热插拔及错误回调。 |
| **OH_MidiClientDestroy**  | 销毁MIDI客户端实例，释放相关资源。                                   |
| **OH_MidiGetDevices**     | 获取当前系统已连接的MIDI设备列表及设备详细信息。                     |
| **OH_MidiGetDevicePorts** | 获取指定设备的端口信息。                                             |
| **OH_MidiOpenDevice**     | 打开指定的MIDI设备，建立连接会话。                                   |
| **OH_MidiOpenBleDevice**  | 打开指定的BLE MIDI设备，建立连接会话。                               |
| **OH_MidiCloseDevice**    | 关闭已打开的MIDI设备，断开连接。                                     |
| **OH_MidiOpenInputPort**  | 打开设备的指定输入端口，准备接收MIDI数据。                           |
| **OH_MidiOpenOutputPort** | 打开设备的指定输出端口，准备发送MIDI数据。                           |
| **OH_MidiSend**           | 向指定输出端口发送MIDI数据。                                         |
| **OH_MidiClosePort**      | 关闭指定的输入或输出端口，停止数据传输。                             |

### 开发步骤

以下演示使用 Native API 开发 MIDI 应用的完整流程，包含客户端创建、热插拔监听、设备发现、数据收发及资源释放。

1. **创建客户端**：初始化 MIDI 客户端上下文，并注册设备热插拔回调及服务异常回调。
2. **发现设备与端口**：获取当前连接的设备列表，并查询设备的端口能力。
3. **打开设备**：建立设备连接会话。
4. **打开端口**：根据端口方向（Input/Output）分别打开端口。
5. **数据交互**：
* **接收**：通过回调函数接收 UMP 格式的 MIDI 数据。
* **发送**：构建 UMP 数据包并通过 Output 端口发送。
6. **释放资源**：使用完毕后关闭端口、设备并销毁客户端。

#### 代码示例

```cpp
#include <native_midi.h>
#include <vector>
#include <iostream>
#include <thread>
#include <chrono>
#include <iomanip>

// 1. 定义设备热插拔回调
void OnDeviceChange(void *userData, OH_MidiDeviceChangeAction action, OH_MidiDeviceInformation info) {
    if (action == MIDI_DEVICE_CHANGE_ACTION_CONNECTED) {
        std::cout << "[Hotplug] Device Connected: ID=" << info.midiDeviceId
                  << ", Name=" << info.productName << std::endl;
    } else if (action == MIDI_DEVICE_CHANGE_ACTION_DISCONNECTED) {
        std::cout << "[Hotplug] Device Disconnected: ID=" << info.midiDeviceId << std::endl;
    }
}

// 2. 定义服务错误回调
void OnError(void *userData, OH_MidiStatusCode code) {
    std::cout << "[Error] Critical Service Error occurred! Code=" << code
              << ". Client may need recreation." << std::endl;
}

// 3. 定义数据接收回调
void OnMidiReceived(void *userData, const OH_MidiEvent *events, size_t eventCount) {
    for (size_t i = 0; i < eventCount; ++i) {
        if (events[i].data != nullptr) {
            // 使用 hex, setw, setfill 格式化 32 位十六进制输出，并在结束后用 dec 恢复十进制
            std::cout << "[Rx] Timestamp=" << events[i].timestamp
                      << ", Data=0x" << std::hex << std::setw(8) << std::setfill('0')
                      << events[i].data[0] << std::dec << std::endl;
        }
    }
}

void MidiDemo() {
    // 1. 创建 MIDI 客户端并注册回调
    OH_MidiClient *client = nullptr;
    OH_MidiCallbacks callbacks;
    callbacks.onDeviceChange = OnDeviceChange;
    callbacks.onError = OnError;

    OH_MidiStatusCode ret = OH_MidiClientCreate(&client, callbacks, nullptr);
    if (ret != MIDI_STATUS_OK) {
        std::cout << "Failed to create client." << std::endl;
        return;
    }

    // 2. 获取设备列表 (两次调用模式)
    size_t devCount = 0;
    OH_MidiGetDevices(client, nullptr, &devCount);

    if (devCount > 0) {
        std::vector<OH_MidiDeviceInformation> devices(devCount);
        OH_MidiGetDevices(client, devices.data(), &devCount);

        // 示例：操作列表中的第一个设备
        int64_t targetDeviceId = devices[0].midiDeviceId;
        std::cout << "Target Device ID: " << targetDeviceId << std::endl;

        // 3. 获取端口信息 (无需 OpenDevice 即可查询)
        size_t portCount = 0;
        OH_MidiGetDevicePorts(client, targetDeviceId, nullptr, &portCount);

        if (portCount > 0) {
            std::vector<OH_MidiPortInformation> ports(portCount);
            OH_MidiGetDevicePorts(client, targetDeviceId, ports.data(), &portCount);

            // 4. 打开设备
            OH_MidiDevice *device = nullptr;
            ret = OH_MidiOpenDevice(client, targetDeviceId, &device);

            if (ret == MIDI_STATUS_OK && device != nullptr) {
                // 5. 遍历并打开端口
                for (const auto& port : ports) {
                    // --- 场景 A: 输入端口 (接收) ---
                    if (port.direction == MIDI_PORT_DIRECTION_INPUT) {
                        OH_MidiPortDescriptor desc = {port.portIndex, MIDI_PROTOCOL_1_0};
                        if (OH_MidiOpenInputPort(device, desc, OnMidiReceived, nullptr) == MIDI_STATUS_OK) {
                            std::cout << "Input port " << port.portIndex << " opened." << std::endl;
                        }
                    }
                    // --- 场景 B: 输出端口 (发送) ---
                    else if (port.direction == MIDI_PORT_DIRECTION_OUTPUT) {
                        OH_MidiPortDescriptor desc = {port.portIndex, MIDI_PROTOCOL_1_0};
                        if (OH_MidiOpenOutputPort(device, desc) == MIDI_STATUS_OK) {
                            std::cout << "Output port " << port.portIndex << " opened. Sending data..." << std::endl;

                            // 构建 UMP 数据包: MIDI 1.0 Note On -> Channel 0, Note 60, Vel 100
                            uint32_t umpMsg[1] = { 0x20903C64 };

                            OH_MidiEvent event;
                            event.timestamp = 0; // 0 表示立即发送
                            event.length = 1;    // 数据长度 (1 word)
                            event.data = umpMsg; // 指向 32位 数组

                            uint32_t written = 0;
                            OH_MidiSend(device, port.portIndex, &event, 1, &written);
                        }
                    }
                }

                // 模拟业务运行，等待数据接收
                std::this_thread::sleep_for(std::chrono::seconds(2));

                // 6. 资源释放：关闭端口
                for (const auto& port : ports) {
                    OH_MidiClosePort(device, port.portIndex);
                }
                OH_MidiCloseDevice(device);
            }
        }
    }

    // 7. 销毁客户端
    OH_MidiClientDestroy(client);
    client = nullptr;
}

```

#### 注意事项

* **数据格式**：`OH_MidiEvent` 中的 `data` 指针类型为 `uint32_t*`。在处理 MIDI 2.0 (UMP) 数据时，每个 UMP 数据包由 1 至 4 个 32 位字组成。
* **内存获取模式**：`OH_MidiGetDevices` 和 `OH_MidiGetDevicePorts` 均采用“两次调用”模式。第一次传入 `nullptr` 获取数量，第二次传入分配好的缓冲区获取实际数据。
* **非阻塞发送**：`OH_MidiSend` 为非阻塞接口。如果底层缓冲区已满，该接口可能只发送部分数据，请务必检查 `eventsWritten` 返回值。
* **回调限制**：`OnMidiReceived` 和 `OnDeviceChange` 回调函数运行在非 UI 线程，请勿直接在回调中执行耗时操作或操作 UI 控件。

### BLE MIDI 设备接入说明

对于需要使用蓝牙 MIDI 设备的场景，应用需自行负责设备的扫描与发现，通过特定接口将设备接入 MIDI 服务。具体流程如下：

1. **扫描设备**：
应用需调用系统蓝牙接口（如 `@ohos.bluetooth.ble`）启动 BLE 扫描。为了过滤出支持 MIDI 协议的设备，需设置 `ScanFilter` 的 `serviceUuid` 为蓝牙 SIG 定义的 MIDI 服务 UUID。
* **MIDI Service UUID**: `03B80E5A-EDE8-4B33-A751-6CE34EC4C700` (参照 [Bluetooth Low Energy MIDI Specification](https://midi.org/midi-over-bluetooth-low-energy-ble-midi))
2. **建立连接**：
扫描获取到目标设备的 MAC 地址（`deviceId`）后，调用 **OH_MidiOpenBleDevice** 接口进行连接。
```cpp
OH_MidiDevice *device = nullptr;
int64_t midiDeviceId = 0;
// deviceAddr: 从蓝牙扫描结果中获取的 MAC 地址字符串 (如 "XX:XX:XX:XX:XX:XX")
OH_MidiStatusCode ret = OH_MidiOpenBleDevice(client, deviceAddr, &device, &midiDeviceId);
```
3. **统一管理**：
连接成功后，MIDI 服务会将该 BLE 设备视为已连接的 MIDI 设备：
* 该设备会被自动添加至 `OH_MidiGetDevices` 能够获取的设备列表中。
* 返回的 `OH_MidiDevice` 句柄与 USB 设备获取的句柄功能一致，均可使用 **OH_MidiGetDevicePorts**、**OH_MidiOpenInputPort**、**OH_MidiSend** 等接口进行端口操作与数据收发。

## 约束

* **硬件与内核要求**
* **USB MIDI**：基于当前 MIDI HDI 标准驱动的实现，当前仅支持符合 **USB Audio Class (UAC)** 规范的通用免驱（Class Compliant）设备（如 USB MIDI 键盘、电子鼓）。
* **BLE MIDI**：OpenHarmony开发设备必须支持 BLE（Bluetooth Low Energy）协议。

* **驱动开发状态**
* 当前版本的 **MIDI HDI** 主要对接标准 ALSA 接口以支持 USB 设备。
* MIDI HDI 驱动接口尚在标准化过程中。

* **协议与数据格式**
* **UMP Native**：midi_framework 采用全链路 UMP 设计。无论物理设备是 MIDI 1.0 还是 MIDI 2.0，Native API 接口收发的数据**始终为 UMP 格式**。

* **权限说明**
* 应用访问 BLE MIDI 设备需要申请相应的系统权限(@ohos.permission.ACCESS_BLUETOOTH)。

## 相关仓
[媒体子系统](https://gitcode.com/openharmony/docs/blob/master/zh-cn/readme/媒体子系统.md)
[drivers_interface](https://gitcode.com/openharmony/drivers_interface)
[audio_framework](https://gitcode.com/openharmony/multimedia_audio_framework)
**[midi_framework](https://gitcode.com/openharmony/midi_framework-sig)**