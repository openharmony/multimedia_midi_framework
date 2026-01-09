## 概述

Midi模块提供MIDI（乐器数字接口）功能的C语言API接口定义。

**系统能力：** SystemCapability.Multimedia.Audio.Core

**起始版本：** 24

## 汇总

### 类型定义

| 名称 | 描述 |
| --- | --- |
| typedef enum [OH_MidiStatusCode](#oh_midistatuscode) | 此枚举定义Midi状态码。 |
| typedef enum [OH_MidiPortDirection](#oh_midiportdirection) | 此枚举定义端口方向。 |
| typedef enum [OH_MidiProtocol](#oh_midiprotocol) | 此枚举定义Midi传输协议语义。 |
| typedef enum [OH_MidiDeviceType](#oh_mididevicetype) | 此枚举定义Midi设备类型。 |
| typedef enum [OH_MidiDeviceChangeAction](#oh_mididevicechangeaction) | 此枚举定义设备连接状态变化动作。 |
| typedef struct [OH_MidiEvent](#oh_midievent) | 定义通用Midi事件结构（支持Midi 1.0和UMP）。 |
| typedef struct [OH_MidiDeviceInformation](#oh_midideviceinformation) | 定义设备信息。 |
| typedef struct [OH_MidiPortDescriptor](#oh_midiportdescriptor) | 定义端口描述符。 |
| typedef struct [OH_MidiPortInformation](#oh_midiportinformation) | 定义详细的端口信息。 |
| typedef struct [OH_MidiClient](#oh_midiclient) | 定义Midi客户端句柄。 |
| typedef struct [OH_MidiDevice](#oh_mididevice) | 定义Midi设备句柄。 |
| typedef void (*[OH_OnMidiDeviceChange](#oh_onmididevicechange)) (void *userData, [OH_MidiDeviceChangeAction](#oh_mididevicechangeaction) action, [OH_MidiDeviceInformation](#oh_midideviceinformation) deviceInfo) | 定义监听设备连接/断开的回调函数。 |
| typedef void (*[OH_OnMidiReceived](#oh_onmidireceived)) (void *userData, const [OH_MidiEvent](#oh_midievent) *events, size_t eventCount) | 定义接收Midi数据的回调函数（批量处理）。 |
| typedef void (*[OH_OnMidiError](#oh_onmidierror)) (void *userData, [OH_MidiStatusCode](#oh_midistatuscode) code) | 定义处理客户端错误的监听回调函数。 |
| typedef struct [OH_MidiCallbacks](#oh_midicallbacks) | 定义客户端回调函数结构体。 |

### 枚举

| 名称 | 描述 |
| --- | --- |
| [OH_MidiStatusCode](#oh_midistatuscode) | Midi状态码。 |
| [OH_MidiPortDirection](#oh_midiportdirection) | 端口方向。 |
| [OH_MidiProtocol](#oh_midiprotocol) | Midi传输协议语义。 |
| [OH_MidiDeviceType](#oh_mididevicetype) | Midi设备类型。 |
| [OH_MidiDeviceChangeAction](#oh_mididevicechangeaction) | 设备连接状态变化动作。 |

## 类型定义说明

### OH_MidiCallbacks

```c
typedef struct OH_MidiCallbacks OH_MidiCallbacks

```

**描述**

定义客户端回调函数结构体。

**起始版本：** 24

### OH_MidiClient

```c
typedef struct OH_MidiClientStruct OH_MidiClient

```

**描述**

声明Midi客户端结构体。

**起始版本：** 24

### OH_MidiDevice

```c
typedef struct OH_MidiDeviceStruct OH_MidiDevice

```

**描述**

声明Midi设备结构体。

**起始版本：** 24

### OH_MidiDeviceChangeAction

```c
typedef enum OH_MidiDeviceChangeAction OH_MidiDeviceChangeAction

```

**描述**

此枚举定义设备连接状态变化动作。

**起始版本：** 24

### OH_MidiDeviceInformation

```c
typedef struct OH_MidiDeviceInformation OH_MidiDeviceInformation

```

**描述**

定义设备信息，用于枚举和显示。

**起始版本：** 24

### OH_MidiDeviceType

```c
typedef enum OH_MidiDeviceType OH_MidiDeviceType

```

**描述**

此枚举定义Midi设备类型。

**起始版本：** 24

### OH_MidiEvent

```c
typedef struct OH_MidiEvent OH_MidiEvent

```

**描述**

定义通用Midi事件结构。设计用于处理原始字节流（Midi 1.0）和UMP（通用MIDI数据包）。

**起始版本：** 24

### OH_MidiPortDescriptor

```c
typedef struct OH_MidiPortDescriptor OH_MidiPortDescriptor

```

**描述**

定义端口描述符。

**起始版本：** 24

### OH_MidiPortDirection

```c
typedef enum OH_MidiPortDirection OH_MidiPortDirection

```

**描述**

此枚举定义端口方向。

**起始版本：** 24

### OH_MidiPortInformation

```c
typedef struct OH_MidiPortInformation OH_MidiPortInformation

```

**描述**

定义详细的端口信息，用于枚举（包含显示名称）。

**起始版本：** 24

### OH_MidiProtocol

```c
typedef enum OH_MidiProtocol OH_MidiProtocol

```

**描述**

此枚举定义Midi传输协议语义。

**注意**：SDK始终使用 **UMP (通用MIDI数据包)** 格式进行数据传输，无论选择何种协议。此枚举定义连接的“行为”和“语义”，而非数据结构。

**起始版本：** 24

### OH_MidiStatusCode

```c
typedef enum OH_MidiStatusCode OH_MidiStatusCode

```

**描述**

此枚举定义Midi状态码。

**起始版本：** 24

### OH_OnMidiDeviceChange

```c
typedef void (*OH_OnMidiDeviceChange)(void *userData, OH_MidiDeviceChangeAction action, OH_MidiDeviceInformation deviceInfo)

```

**描述**

定义监听设备连接/断开的回调函数。

**起始版本：** 24

**参数:**

| 名称 | 描述 |
| --- | --- |
| userData | 用户在创建客户端时提供的上下文数据。 |
| action | 设备状态变化动作（连接/断开），参见[OH_MidiDeviceChangeAction](#oh_mididevicechangeaction)。 |
| deviceInfo | 发生变化的设备信息，参见[OH_MidiDeviceInformation](#oh_midideviceinformation)。 |

### OH_OnMidiError

```c
typedef void (*OH_OnMidiError)(void *userData, OH_MidiStatusCode code)

```

**描述**

定义处理客户端级错误的监听回调函数。当Midi服务发生严重错误（例如服务崩溃）时调用。此时应用程序可能需要重新创建客户端。

**起始版本：** 24

**参数:**

| 名称 | 描述 |
| --- | --- |
| userData | 用户在创建客户端时提供的上下文数据。 |
| code | 指示原因的错误码，参见[OH_MidiStatusCode](#oh_midistatuscode)。 |

### OH_OnMidiReceived

```c
typedef void (*OH_OnMidiReceived)(void *userData, const OH_MidiEvent *events, size_t eventCount)

```

**描述**

定义接收Midi数据的回调函数（批量处理）。

**注意**：

1. 该回调在非UI线程（高优先级线程）上调用。
2. `events` 数组及其数据指针仅在此回调期间有效。如果需要保存数据，请进行拷贝。

**起始版本：** 24

**参数:**

| 名称 | 描述 |
| --- | --- |
| userData | 用户在打开端口时提供的上下文数据。 |
| events | 指向接收到的Midi事件数组的指针，参见[OH_MidiEvent](#oh_midievent)。 |
| eventCount | 数组中的事件数量。 |

## 枚举类型说明

### OH_MidiDeviceChangeAction

```c
enum OH_MidiDeviceChangeAction

```

**描述**

此枚举定义设备连接状态变化动作。

**起始版本：** 24

| 枚举值 | 描述 |
| --- | --- |
| MIDI_DEVICE_CHANGE_ACTION_CONNECTED | 设备已连接。 |
| MIDI_DEVICE_CHANGE_ACTION_DISCONNECTED | 设备已断开。 |

### OH_MidiDeviceType

```c
enum OH_MidiDeviceType

```

**描述**

此枚举定义Midi设备类型。

**起始版本：** 24

| 枚举值 | 描述 |
| --- | --- |
| MIDI_DEVICE_TYPE_USB | USB设备。 |
| MIDI_DEVICE_TYPE_BLE | 蓝牙低功耗（BLE）设备。 |

### OH_MidiPortDirection

```c
enum OH_MidiPortDirection

```

**描述**

此枚举定义端口方向。

**起始版本：** 24

| 枚举值 | 描述 |
| --- | --- |
| MIDI_PORT_DIRECTION_INPUT | 输入端口（设备 -> 主机）。 |
| MIDI_PORT_DIRECTION_OUTPUT | 输出端口（主机 -> 设备）。 |

### OH_MidiProtocol

```c
enum OH_MidiProtocol

```

**描述**

此枚举定义Midi传输协议语义。

**注意**：无论选择何种协议，SDK始终使用 **UMP (Universal Midi Packet, 通用MIDI数据包)** 格式进行数据传输。此枚举定义连接的“行为”和“语义”，而非数据结构。

**起始版本：** 24

| 枚举值 | 描述 |
| --- | --- |
| MIDI_PROTOCOL_1_0 | **传统Midi 1.0 语义。** 服务期望严格兼容Midi 1.0的UMP数据包。支持的消息类型包括：**MT 0x0**（工具消息/JR时间戳）、**MT 0x1**（系统实时和系统通用消息）、**MT 0x2**（32位 Midi 1.0 通道声音消息）、**MT 0x3**（用于7位SysEx负载的64位数据消息）。硬件处理：如果目标硬件是Midi 1.0，服务将UMP转换回字节流；如果是Midi 2.0，则按原样发送。 |
| MIDI_PROTOCOL_2_0 | **Midi 2.0 语义。** 服务期望利用Midi 2.0特性的UMP数据包。兼容以上1.0的数据包外，还支持的消息类型包括：**MT 0x4**（64位 Midi 2.0 高精度通道声音消息）、**MT 0x0**（工具消息）、**MT 0xD**（128位 Flex Data消息）、**MT 0xF**（128位 UMP流消息）、**MT 0x3/MT 0x5**（数据消息）。**回退策略：** 如果硬件仅支持Midi 1.0，服务将执行“尽力而为”的转换（例如降低精度、Type 4转Type 2），部分数据可能丢失。 |

### OH_MidiStatusCode

```c
enum OH_MidiStatusCode

```

**描述**

此枚举定义Midi状态码。

**起始版本：** 24

| 枚举值 | 描述 |
| --- | --- |
| MIDI_STATUS_OK | 操作成功。 |
| MIDI_STATUS_GENERIC_INVALID_ARGUMENT | 无效参数（例如空指针）。 |
| MIDI_STATUS_GENERIC_IPC_FAILURE | IPC（进程间通信）失败，服务端异常会返回此错误码。 |
| MIDI_STATUS_INSUFFICIENT_RESULT_SPACE | 结果空间不足。当调用者提供的缓冲区太小无法容纳结果时返回。 |
| MIDI_STATUS_INVALID_CLIENT | 无效的客户端句柄。 |
| MIDI_STATUS_INVALID_DEVICE_HANDLE | 无效的设备句柄。 |
| MIDI_STATUS_INVALID_PORT | 无效的端口索引。 |
| MIDI_STATUS_WOULD_BLOCK | 发送缓冲区暂时已满。表示共享内存缓冲区当前没有空间。应用程序应稍候重试。非阻塞发送在消息无法放入缓冲区时返回此错误。 |
| MIDI_STATUS_TIMEOUT | 操作无法在合理的时间内完成。 |
| MIDI_STATUS_TOO_MANY_OPEN_DEVICES | 客户端已达到允许打开的最大设备数。要打开新设备，客户端必须先关闭现有设备。 |
| MIDI_STATUS_TOO_MANY_OPEN_PORTS | 客户端已达到允许打开的最大端口数。要打开新端口，客户端必须先关闭现有端口。 |
| MIDI_STATUS_DEVICE_ALREADY_OPEN | 客户端已打开此设备。 |
| MIDI_STATUS_SERVICE_DIED | Midi系统服务已停止或断开连接。必须销毁并重新创建客户端。 |
| MIDI_STATUS_UNKNOWN_ERROR | 未知系统错误。 |