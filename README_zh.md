# midi_framework部件

## 简介

midi_framework部件为OpenHarmony系统提供了统一的MIDI设备访问、数据传输及协议处理能力，使得应用能够直接调用系统提供的接口实现外部MIDI设备的发现、连接以及高性能的指令收发功能。

midi_framework部件主要具备以下功能：

* **设备发现与连接**：支持MIDI设备的枚举、信息查询、热插拔监听（USB MIDI设备）以及设备连接。
* **数据传输**：支持基于UMP（Universal MIDI Packet）协议的高性能数据发送与接收。

**图 1** midi_framework部件架构图

架构中主要模块的功能说明如下：

**表 1** 模块功能介绍

| 模块名称            | 功能                                                                                                           |
| ------------------- | -------------------------------------------------------------------------------------------------------------- |
| **MIDI客户端**      | 提供MIDI客户端创建、销毁及回调注册的接口，是应用与服务交互的入口。                                             |
| **设备发现与管理**  | 提供当前系统连接的MIDI设备枚举、设备信息查询及USB设备热插拔事件监听接口。                                      |
| **连接会话管理**    | 管理应用端与服务端的连接会话，处理端口（Port）的打开、关闭及多客户端路由分发逻辑。                             |
| **数据传输**        | 基于共享内存与无锁环形队列（Shared Ring Buffer）实现跨进程的高性能MIDI数据传输。                               |
| **协议处理器**      | 用于处理MIDI 2.0（UMP）与MIDI 1.0（Byte Stream）之间的协议解析与自动转换。                                     |
| **USB设备适配**     | 负责与底层ALSA驱动交互，实现标准USB MIDI设备的指令读写与控制。                                                 |
| **IPC通信**         | 定义并实现客户端与服务端之间的跨进程通信接口，处理请求调度。                                                   |
| **MIDI硬件驱动HDI** | 提供MIDI硬件驱动的抽象接口，通过该接口对服务层屏蔽底层硬件差异（如ALSA节点），向服务层提供统一的数据读写能力。 |

## 目录

```
/foundation/multimedia/midi_framework
├── bundle.json                            # 部件描述与编译配置文件
├── config.gni                             # 编译配置参数
├── figures                                # 架构图等资源文件
├── frameworks                             # 框架层实现
│   └── native
│       ├── midi                           # MIDI客户端核心逻辑 (Client Context)
│       ├── midiutils                      # 基础工具库
│       └── ohmidi                         # Native API 接口实现
├── interfaces                             # 接口定义
│   ├── inner_api                          # 系统内部接口头文件 (C++ Client)
│   └── kits                               # 对外接口头文件 (Native C API)
├── sa_profile                             # 系统服务配置文件
├── services                               # MIDI服务层实现
│   ├── common                             # 服务端通用组件 (Futex同步, 共享内存, 无锁队列, UMP协议处理)
│   ├── etc                                # 进程启动配置 (midi_server.cfg)
│   ├── idl                                # IPC通信接口定义
│   └── server                             # 服务端核心逻辑 (设备管理, USB驱动适配, 客户端连接管理)
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

## 说明

### 接口说明

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
| **OH_MidiClosePort**      | 关闭指定的输入端口，停止数据传输。               |

### 使用说明

以下主要演示使用 Native API 开发 MIDI 应用的基本流程，包含客户端创建、设备发现、端口打开（接收数据）及资源释放。

#### 开发步骤

1. **创建客户端**：初始化 MIDI 客户端上下文。
2. **发现设备**：获取系统当前连接的 MIDI 设备列表。
3. **打开设备与端口**：建立设备连接并打开指定端口。
4. **数据交互**：通过回调函数接收数据。
5. **释放资源**：使用完毕后关闭端口、设备并销毁客户端。

#### 代码示例

```cpp
#include <midi/native_midi.h>
#include <vector>
#include <iostream>

// 定义接收 MIDI 数据的回调函数
void OnMidiReceived(void *userData, const OH_MidiEvent *events, size_t eventCount) {
    for (size_t i = 0; i < eventCount; ++i) {
        // 处理接收到的 MIDI 事件
        // events[i].data, events[i].timestamp 等
    }
}

void MidiDemo() {
    // 1. 创建 MIDI 客户端
    OH_MidiClient *client = nullptr;
    // 系统级回调（如设备热插拔），此处暂不注册
    OH_MidiCallbacks callbacks = {nullptr, nullptr};
    OH_MidiStatusCode ret = OH_MidiClientCreate(&client, callbacks, nullptr);
    if (ret != MIDI_STATUS_OK) {
        return; // 创建失败处理
    }

    // 2. 获取设备列表
    // 2.1 先获取设备数量
    size_t devCount = 0;
    OH_MidiGetDevices(client, nullptr, &devCount);

    if (devCount > 0) {
        // 2.2 分配内存并获取设备详情
        std::vector<OH_MidiDeviceInformation> devices(devCount);
        OH_MidiGetDevices(client, devices.data(), &devCount);

        // 3. 打开第一个设备 (示例)
        OH_MidiDevice *device = nullptr;
        int64_t targetDeviceId = devices[0].midiDeviceId;
        ret = OH_MidiOpenDevice(client, targetDeviceId, &device);

        if (ret == MIDI_STATUS_OK && device != nullptr) {
            // 4. 获取端口信息
            size_t portCount = 0;
            OH_MidiGetDevicePorts(device, nullptr, &portCount);

            if (portCount > 0) {
                std::vector<OH_MidiPortInformation> ports(portCount);
                OH_MidiGetDevicePorts(device, ports.data(), &portCount);

                // 5. 遍历并打开输入端口
                for (const auto& port : ports) {
                    if (port.direction == MIDI_PORT_DIRECTION_INPUT) {
                        OH_MidiPortDescriptor desc = {port.portIndex, MIDI_PROTOCOL_1_0};

                        // 注册接收回调，打开端口
                        ret = OH_MidiOpenInputPort(device, desc, OnMidiReceived, nullptr);
                        if (ret == MIDI_STATUS_OK) {
                            // 端口打开成功，此时可以接收数据
                            // ... 业务逻辑 ...

                            // 6. 关闭端口
                            OH_MidiClosePort(device, port.portIndex);
                        }
                    }
                }
            }
            // 7. 关闭设备
            OH_MidiCloseDevice(device);
        }
    }

    // 8. 销毁客户端
    OH_MidiClientDestroy(client);
    client = nullptr;
}

```

#### 注意事项

* **内存获取模式**：`OH_MidiGetDevices` 和 `OH_MidiGetDevicePorts` 均采用“两次调用”模式。第一次传入 `nullptr` 获取数量，第二次传入分配好的缓冲区获取实际数据。
* **回调限制**：`OnMidiReceived` 回调函数运行在非 UI 线程，请勿直接在回调中执行耗时操作或直接操作 UI 控件。
* **端口方向**：在打开端口前，务必通过 `OH_MidiPortInformation.direction` 确认端口方向（`MIDI_PORT_DIRECTION_INPUT` 或 `MIDI_PORT_DIRECTION_OUTPUT`），错误的打开方式会导致 `MIDI_STATUS_INVALID_PORT` 错误。

## 相关仓

[媒体子系统](https://gitee.com/openharmony/docs/blob/master/zh-cn/readme/媒体子系统.md)

**[midi_framework](https://gitcode.com/openharmony/midi_framework-sig)**