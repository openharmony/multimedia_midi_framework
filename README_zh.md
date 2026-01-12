# midi_framework部件<a name="ZH-CN_TOPIC_MIDI_001"></a>

* [简介](#section_intro)
* [基本概念](#section_concepts)


* [目录](#section_dir)
* [编译构建](#section_build)
* [使用说明](#section_usage)
* [接口说明](#section_api)
* [开发步骤](#section_steps)
* [支持设备](#section_devices)


* [相关仓](#section_related)

## 简介<a name="section_intro"></a>

midi_framework部件为OpenHarmony系统提供了统一的MIDI设备访问、数据传输及协议处理能力，使得应用能够直接调用系统提供的接口实现外部MIDI设备的发现、连接以及高性能的指令收发功能。

midi_framework部件主要具备以下常见功能：

* **设备发现与连接**：支持MIDI设备的枚举、信息查询、热插拔监听（USB MIDI设备）以及设备连接。
* **数据传输**：支持基于UMP（Universal MIDI Packet）协议的高性能数据发送与接收。

**图 1** MIDI部件架构图<a name="fig_midi_arch"></a>
![midi_framework部件架构图](figures/zh-cn_image_midi_framework.png)

### 基本概念<a name="section_concepts"></a>

* **MIDI (Musical Instrument Digital Interface)**
乐器数字接口，是电子乐器、终端设备之间交换音乐信息（如音符、控制参数等）的标准协议。
* **UMP (Universal MIDI Packet)**
通用 MIDI 数据包。这是 MIDI 2.0 规范引入的一种基于 32 位字（Word）构建的数据格式。
> **注意**：本部件采用 **UMP Native** 设计。无论底层硬件是 MIDI 1.0 还是 MIDI 2.0 设备，也无论应用层选择何种协议语义，**应用层收到的所有 MIDI 数据均为 UMP 格式**。对于 MIDI 1.0 设备，系统会自动将其数据封装为 UMP 数据包（Message Type 0x2 或 0x3）。

* **协议语义 (Midi Protocol)**
指 MIDI 连接在交互行为上的规则，而非数据格式。
* **MIDI 1.0 语义**：限制数据传输仅使用 MIDI 1.0 兼容的消息类型（如 Note On/Off, Control Change），适用于需要与传统软件或硬件互通的场景。
* **MIDI 2.0 语义**：允许使用高精度的 MIDI 2.0 消息（如 32-bit Velocity, Attribute Controller），适用于高性能音乐创作场景。

* **MIDI 端口 (MIDI Port)**
MIDI 设备上用于输入或输出数据的逻辑接口。一个 MIDI 设备可以拥有多个输入端口和输出端口，每个端口独立传输 MIDI 数据流。

架构中主要模块的功能说明如下：

**表 1** 模块功能介绍

| 模块名称            | 功能                                                                                                               |
| ------------------- | ------------------------------------------------------------------------------------------------------------------ |
| **MIDI客户端**      | 提供MIDI客户端创建、销毁及回调注册的接口，是应用与服务交互的入口。                                                 |
| **设备发现与管理**  | 提供当前系统连接的MIDI设备枚举、设备信息查询、设备上下线事件监听接口以及通过MAC地址链接蓝牙设备的接口。            |
| **连接会话管理**    | 管理应用端与服务端的连接会话，处理端口（Port）的打开、关闭及多客户端路由分发逻辑。                                 |
| **数据传输**        | 基于共享内存与无锁环形队列（Shared Ring Buffer）实现跨进程的高性能MIDI数据传输。                                   |
| **协议处理器**      | 用于处理MIDI 2.0（UMP）与MIDI 1.0（Byte Stream）之间的协议解析与自动转换。                                         |
| **USB设备适配**     | 负责与MIDI HDI交互，间接访问ALSA驱动，实现标准USB MIDI设备的指令读写与控制。                                       |
| **BLE设备适配**     | 负责对接系统蓝牙服务，实现BLE MIDI设备的指令读写与控制。                                                           |
| **IPC通信模块**     | 定义并实现客户端与服务端之间的跨进程通信接口，处理请求调度。                                                       |
| **MIDI硬件驱动HDI** | 提供MIDI硬件驱动的抽象接口，通过该接口对服务层屏蔽底层硬件差异（如声卡设备节点），向服务层提供统一的数据读写能力。 |

## 目录<a name="section_dir"></a>

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
│   ├── common                             # 服务端通用组件 (Futex同步, 共享内存, 无锁队列, UMP协议处理)
│   ├── etc                                # 进程启动配置 (midi_server.cfg)
│   ├── idl                                # IPC通信接口定义
│   └── server                             # 服务端核心逻辑 (设备管理, 驱动适配, 客户端连接管理)
└── test                                   # 测试代码
    ├── fuzzer                             # Fuzzing测试用例
    └── unittest                           # 单元测试用例

```

## 编译构建<a name="section_build"></a>

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

## 使用说明<a name="section_usage"></a>

### 接口说明<a name="section_api"></a>

midi_framework部件向开发者提供了C语言原生接口（Native API），主要涵盖客户端管理、设备管理及端口操作。主要接口及其功能如下：

**表 2** 接口说明

| 接口名称                  | 功能描述                                                             |
| ------------------------- | -------------------------------------------------------------------- |
| **OH_MidiClientCreate**   | 创建MIDI客户端实例，初始化上下文环境，并可注册设备热插拔及错误回调。 |
| **OH_MidiClientDestroy**  | 销毁MIDI客户端实例，释放相关资源。                                   |
| **OH_MidiGetDevices**     | 获取当前系统已连接的MIDI设备列表及设备详细信息。                     |
| **OH_MidiGetDevicePorts** | 获取指定设备的端口信息。                                             |
| **OH_MidiOpenDevice**     | 打开指定的MIDI设备，建立连接会话。                                   |
| **OH_MidiCloseDevice**    | 关闭已打开的MIDI设备，断开连接。                                     |
| **OH_MidiOpenInputPort**  | 打开设备的指定输入端口，准备接收MIDI数据。                           |
| **OH_MidiOpenOutputPort** | 打开设备的指定输出端口，准备发送MIDI数据。                           |
| **OH_MidiSend**           | 向指定输出端口发送MIDI数据。                                         |
| **OH_MidiClosePort**      | 关闭指定的输入或输出端口，停止数据传输。                             |

### 开发步骤<a name="section_steps"></a>

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
#include <midi/native_midi.h>
#include <vector>
#include <iostream>
#include <thread>
#include <chrono>

// 1. 定义设备热插拔回调
void OnDeviceChange(void *userData, OH_MidiDeviceChangeAction action, OH_MidiDeviceInformation info) {
    if (action == MIDI_DEVICE_CHANGE_ACTION_CONNECTED) {
        printf("[Hotplug] Device Connected: ID=%ld, Name=%s\n", info.midiDeviceId, info.name);
    } else if (action == MIDI_DEVICE_CHANGE_ACTION_DISCONNECTED) {
        printf("[Hotplug] Device Disconnected: ID=%ld\n", info.midiDeviceId);
    }
}

// 2. 定义服务错误回调
// 当 MIDI 服务发生严重错误（如服务崩溃）时触发，建议此时重建客户端
void OnError(void *userData, OH_MidiStatusCode code) {
    printf("[Error] Critical Service Error occurred! Code=%d. Client may need recreation.\n", code);
}

// 3. 定义数据接收回调
// 注意：OH_MidiEvent 中的 data 是 uint32_t* 类型，指向 UMP 数据包
void OnMidiReceived(void *userData, const OH_MidiEvent *events, size_t eventCount) {
    for (size_t i = 0; i < eventCount; ++i) {
        // 打印该事件的第一个 32位 UMP 字
        // 如果是 MIDI 1.0 Channel Voice (32-bit)，length 通常为 1 (word)
        if (events[i].data != nullptr) {
            printf("[Rx] Timestamp=%llu, Data=0x%08X\n", events[i].timestamp, events[i].data[0]);
        }
    }
}

void MidiDemo() {
    // 1. 创建 MIDI 客户端并注册回调
    OH_MidiClient *client = nullptr;
    OH_MidiCallbacks callbacks;
    callbacks.onDeviceChange = OnDeviceChange;
    callbacks.onError = nullptr;

    OH_MidiStatusCode ret = OH_MidiClientCreate(&client, callbacks, nullptr);
    if (ret != MIDI_STATUS_OK) {
        printf("Failed to create client.\n");
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
        printf("Target Device ID: %ld\n", targetDeviceId);

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
                        // 使用 MIDI 1.0 语义 (数据仍为 UMP 封装)
                        OH_MidiPortDescriptor desc = {port.portIndex, MIDI_PROTOCOL_1_0};
                        if (OH_MidiOpenInputPort(device, desc, OnMidiReceived, nullptr) == MIDI_STATUS_OK) {
                            printf("Input port %d opened.\n", port.portIndex);
                        }
                    }
                    // --- 场景 B: 输出端口 (发送) ---
                    else if (port.direction == MIDI_PORT_DIRECTION_OUTPUT) {
                        OH_MidiPortDescriptor desc = {port.portIndex, MIDI_PROTOCOL_1_0};
                        if (OH_MidiOpenOutputPort(device, desc) == MIDI_STATUS_OK) {
                            printf("Output port %d opened. Sending data...\n", port.portIndex);

                            // 构建 UMP 数据包
                            // 示例: MIDI 1.0 Note On -> Channel 0, Note 60, Vel 100
                            // UMP (32-bit) = [MT(4) | Group(4) | Status(8) | Note(8) | Vel(8)]
                            // 0x2 -> MT (MIDI 1.0 Channel Voice)
                            // 0x90 -> Note On, Channel 0
                            // 0x3C -> Note 60
                            // 0x64 -> Velocity 100
                            uint32_t umpMsg[1] = { 0x20903C64 };

                            OH_MidiEvent event;
                            event.timestamp = 0; // 0 表示立即发送
                            event.length = 1; // 数据长度 (1 word)
                            event.data = umpMsg; // 指向 32位 数组

                            uint32_t written = 0;
                            OH_MidiSend(device, port.portIndex, &event, 1, &written);
                        }
                    }
                }

                // 模拟业务运行，等待数据接收
                std::this_thread::sleep_for(std::chrono::seconds(2));

                // 6. 资源释放：关闭端口 (这里简化逻辑，实际应记录打开的端口索引)
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

## 支持设备<a name="section_devices"></a>

midi_framework 部件目前主要支持以下类型的 MIDI 设备连接：

1. **USB MIDI 设备**
支持符合 **USB Audio Class (UAC)** 规范的通用设备。此类设备接入系统后，由内核自动识别并生成 ALSA 设备节点，midi_framework 通过访问这些节点实现数据交互。
* **常见设备**：USB MIDI 键盘、电子鼓、打击垫、合成器等。
* **连接方式**：USB Type-C 有线连接。

2. **BLE MIDI 设备**
支持符合 **MIDI over Bluetooth Low Energy** 标准的无线 MIDI 设备。
* **常见设备**：蓝牙 MIDI 键盘等。
* **连接方式**：蓝牙无线连接。

## 相关仓<a name="section_related"></a>

[媒体子系统](https://gitcode.com/openharmony/docs/blob/master/zh-cn/readme/媒体子系统.md)

**[midi_framework](https://gitcode.com/openharmony/midi_framework-sig)**