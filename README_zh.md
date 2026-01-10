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

**图 1** MIDI组件架构图<a name="fig_midi_arch"></a>
![midi_framework部件架构图](figures/zh-cn_image_midi_framework.png)

### 基本概念<a name="section_concepts"></a>

* **MIDI (Musical Instrument Digital Interface)**
乐器数字接口，是电子乐器、计算机和其他音频设备之间交换音乐信息（如音符、控制参数等）的行业标准协议。
* **UMP (Universal MIDI Packet)**
通用 MIDI 数据包。这是 MIDI 2.0 规范引入的一种基于 32 位字（Word）构建的全新数据格式。
> **注意**：本部件采用 **UMP Native** 设计。无论底层硬件是 MIDI 1.0 还是 MIDI 2.0 设备，也无论应用层选择何种协议语义，**应用层收到的所有 MIDI 数据均为 UMP 格式**。对于 MIDI 1.0 设备，系统会自动将其数据封装为 UMP 数据包（Message Type 0x2 或 0x3）。

* **协议语义 (Protocol Semantics)**
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
/foundation/multimedia/midi_framework      # MIDI组件业务代码
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

| 接口名称                  | 功能描述                                         |
| ------------------------- | ------------------------------------------------ |
| **OH_MidiClientCreate**   | 创建MIDI客户端实例，初始化上下文环境。           |
| **OH_MidiClientDestroy**  | 销毁MIDI客户端实例，释放相关资源。               |
| **OH_MidiGetDevices**     | 获取当前系统已连接的MIDI设备列表及设备详细信息。 |
| **OH_MidiOpenDevice**     | 打开指定的MIDI设备，建立连接会话。               |
| **OH_MidiCloseDevice**    | 关闭已打开的MIDI设备，断开连接。                 |
| **OH_MidiGetDevicePorts** | 获取指定设备的端口信息。                         |
| **OH_MidiOpenInputPort**  | 打开设备的指定输入端口，准备接收MIDI数据。       |
| **OH_MidiOpenOutputPort** | 打开设备的指定输出端口，准备发送MIDI数据。       |
| **OH_MidiSend**           | 向指定输出端口发送MIDI数据。                     |
| **OH_MidiClosePort**      | 关闭指定的输入/输出端口，停止数据传输。          |

### 开发步骤<a name="section_steps"></a>

可以使用此仓库内提供的 Native API 接口实现 MIDI 设备访问。以下步骤描述了如何开发一个基础的 MIDI 数据接收功能：

1. 使用 **OH_MidiClientCreate** 接口创建客户端实例。
```cpp
OH_MidiClient *client = nullptr;
OH_MidiCallbacks callbacks = {nullptr, nullptr}; // 系统回调暂为空
OH_MidiStatusCode ret = OH_MidiClientCreate(&client, callbacks, nullptr);

```


2. 使用 **OH_MidiGetDevices** 接口获取连接的设备列表。
> **注意**：该接口采用两次调用模式。第一次调用获取设备数量，第二次调用获取实际数据。


```cpp
size_t devCount = 0;
// 第一次调用：获取数量
OH_MidiGetDevices(client, nullptr, &devCount);

if (devCount > 0) {
    std::vector<OH_MidiDeviceInformation> devices(devCount);
    // 第二次调用：获取详情
    OH_MidiGetDevices(client, devices.data(), &devCount);
}

```


3. 使用 **OH_MidiGetDevicePorts** 获取指定设备的端口信息。


```cpp
int64_t targetDeviceId = devices[0].midiDeviceId;
size_t portCount = 0;
OH_MidiGetDevicePorts(client, targetDeviceId, nullptr, &portCount);
std::vector<OH_MidiPortInformation> ports(portCount);
OH_MidiGetDevicePorts(client, targetDeviceId, ports.data(), &portCount);

```


4. 使用 **OH_MidiOpenDevice** 打开指定设备，建立连接会话。
```cpp
OH_MidiDevice *device = nullptr;
ret = OH_MidiOpenDevice(client, targetDeviceId, &device);

```


5. 定义数据接收回调函数，并使用 **OH_MidiOpenInputPort** 打开输入端口。
> **重要**：无论在 `OH_MidiPortDescriptor` 中指定的是 `MIDI_PROTOCOL_1_0` 还是 `MIDI_PROTOCOL_2_0`，回调函数中接收到的数据 **始终为 UMP (Universal MIDI Packet) 格式**。SDK 会自动将传统的 MIDI 1.0 字节流封装为 UMP 包。


```cpp
// 回调函数实现
void OnMidiReceived(void *userData, const OH_MidiEvent *events, size_t eventCount) {
    for (size_t i = 0; i < eventCount; ++i) {
        // events[i].data 包含 4~16字节(32位~128位) 的 UMP 数据包
        // 即使是 MIDI 1.0 协议，数据也已封装在 UMP 中
    }
}

// 在遍历端口时打开 Input 类型的端口
if (ports[i].direction == MIDI_PORT_DIRECTION_INPUT) {
    // 选择协议语义：
    // MIDI_PROTOCOL_1_0: 服务端只投递兼容 MIDI 1.0 的 UMP 包
    // MIDI_PROTOCOL_2_0: 服务端投递所有类型的 UMP 包
    OH_MidiPortDescriptor desc = {ports[i].portIndex, MIDI_PROTOCOL_1_0};
    OH_MidiOpenInputPort(device, desc, OnMidiReceived, nullptr);
}

```

6. 打开输出端口发送数据（可选）： 使用 OH_MidiOpenOutputPort 打开输出端口，并构建 UMP 数据包进行发送。
```cpp
// 打开输出端口
if (ports[i].direction == MIDI_PORT_DIRECTION_OUTPUT) {
    OH_MidiPortDescriptor desc = {ports[i].portIndex, MIDI_PROTOCOL_1_0};
    OH_MidiOpenOutputPort(device, desc);

    // 构建 UMP 数据包 (示例：发送 Note On)
    // 即使是 MIDI 1.0 协议，也必须封装在 UMP 格式中 (Message Type 0x2)
    OH_MidiEvent event;
    event.timestamp = 0; // 0 表示立即发送
    // 0x20903C64: MT=0x2(MIDI 1.0), Group=0, Status=0x90(NoteOn Ch1), Note=60, Vel=100
    event.data[0] = 0x20903C64;

    // 发送数据
    uint32_t written = 0;
    OH_MidiSend(device, ports[i].portIndex, &event, 1, &written);

    if (written < 1) {
        // 处理缓冲区满的情况
    }
}

```

7. 数据处理完成后，依次调用 **OH_MidiClosePort**、**OH_MidiCloseDevice** 和 **OH_MidiClientDestroy** 释放资源。
```cpp
OH_MidiClosePort(device, inputPortIndex);
OH_MidiClosePort(device, outputPortIndex);
OH_MidiCloseDevice(device);
OH_MidiClientDestroy(client);

```

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