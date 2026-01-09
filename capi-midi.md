# MidiFramework

## 概述

Midi模块提供MIDI设备管理、消息发送接收以及设备状态监测的API接口。

**系统能力：** SystemCapability.Multimedia.Audio.Midi

**起始版本：** 24

## 汇总

### 文件

| 名称 | 描述 |
| --- | --- |
| [native_midi_base.h](capi-midi-base.md) | 声明Midi相关结构体与枚举。 |

### 函数

| 名称 | 描述 |
| --- | --- |
| [OH_MidiClient_Create](#oh_midiclient_create) | 创建Midi客户端实例。 |
| [OH_MidiClient_Destroy](#oh_midiclient_destroy) | 销毁Midi客户端并释放资源。 |
| [OH_MidiGetDevices](#oh_midigetdevices) | 枚举所有Midi设备（双次调用模式）。 |
| [OH_MidiOpenDevice](#oh_midiopendevice) | 打开指定的Midi设备。 |
| [OH_MidiOpenBleDevice](#oh_midiopenbledevice) | 打开Midi BLE设备。 |
| [OH_MidiCloseDevice](#oh_midiclosedevice) | 关闭Midi设备。 |
| [OH_MidiGetDevicePorts](#oh_midigetdeviceports) | 获取端口信息（双次调用模式）。 |
| [OH_MidiOpenInputPort](#oh_midiopeninputport) | 打开Midi输入端口（接收数据）。 |
| [OH_MidiOpenOutputPort](#oh_midiopenoutputport) | 打开Midi输出端口（发送数据）。 |
| [OH_MidiClosePort](#oh_midicloseport) | 关闭Midi端口。 |
| [OH_MidiSend](#oh_midisend) | 发送Midi消息（批量、非阻塞且原子操作）。 |
| [OH_MidiSendSysEx](#oh_midisendsysex) | 发送长SysEx消息（字节流到UMP的辅助函数）。 |
| [OH_MidiFlushOutputPort](#oh_midiflushoutputport) | 刷新输出缓冲区中的挂起消息。 |

## 函数说明

### OH_MidiClient_Create()

```c
OH_MidiStatusCode OH_MidiClient_Create (OH_MidiClient **client, OH_MidiCallbacks callbacks, void *userData)

```

**描述**

创建Midi客户端实例。

**起始版本：** 24

**参数:**

| 名称 | 描述 |
| --- | --- |
| client | 输出参数，用于接收新客户端句柄的指针。 |
| callbacks | 系统事件的回调结构体。 |
| userData | 传递给回调函数的用户上下文。 |

**返回：**

函数执行结果状态码 `OH_MidiStatusCode`：

* `MIDI_STATUS_OK`：操作成功。
* `MIDI_STATUS_GENERIC_INVALID_ARGUMENT`：client为nullptr。
* `MIDI_STATUS_GENERIC_IPC_FAILURE`：连接系统服务失败。

### OH_MidiClient_Destroy()

```c
OH_MidiStatusCode OH_MidiClient_Destroy (OH_MidiClient *client)

```

**描述**

销毁Midi客户端并释放资源。

**起始版本：** 24

**参数:**

| 名称 | 描述 |
| --- | --- |
| client | 目标客户端句柄。 |

**返回：**

函数执行结果状态码 `OH_MidiStatusCode`：

* `MIDI_STATUS_OK`：操作成功。
* `MIDI_STATUS_INVALID_CLIENT`：client为NULL或无效。
* `MIDI_STATUS_GENERIC_IPC_FAILURE`：连接系统服务失败。

### OH_MidiCloseDevice()

```c
OH_MidiStatusCode OH_MidiCloseDevice (OH_MidiDevice *device)

```

**描述**

关闭Midi设备。

**起始版本：** 24

**参数:**

| 名称 | 描述 |
| --- | --- |
| device | 目标设备句柄。 |

**返回：**

函数执行结果状态码 `OH_MidiStatusCode`：

* `MIDI_STATUS_OK`：操作成功。
* `Midi_STATUS_INVALID_DEVICE_HANDLE`：device无效。

### OH_MidiClosePort()

```c
OH_MidiStatusCode OH_MidiClosePort (OH_MidiDevice *device, uint32_t portIndex)

```

**描述**

关闭Midi输入或输出端口。

**起始版本：** 24

**参数:**

| 名称 | 描述 |
| --- | --- |
| device | 目标设备句柄。 |
| portIndex | 端口索引。 |

**返回：**

函数执行结果状态码 `OH_MidiStatusCode`：

* `MIDI_STATUS_OK`：操作成功。
* `MIDI_STATUS_INVALID_DEVICE_HANDLE`：device无效。
* `MIDI_STATUS_INVALID_PORT`：portIndex无效或未打开。
* `MIDI_STATUS_GENERIC_IPC_FAILURE`：连接系统服务失败。

### OH_MidiFlushOutputPort()

```c
OH_MidiStatusCode OH_MidiFlushOutputPort (OH_MidiDevice *device, uint32_t portIndex)

```

**描述**

立即丢弃指定端口输出缓冲区中当前等待的所有MIDI事件。这包括计划在未来时间戳发送但尚未被服务处理的事件。

**注意**：此操作**不会**发送“关闭所有音符（All Notes Off）”消息，它只是简单地清空队列。

**起始版本：** 24

**参数:**

| 名称 | 描述 |
| --- | --- |
| device | 目标设备句柄。 |
| portIndex | 目标端口索引。 |

**返回：**

函数执行结果状态码 `OH_MidiStatusCode`：

* `MIDI_STATUS_OK`：操作成功。
* `MIDI_STATUS_INVALID_DEVICE_HANDLE`：device无效。
* `MIDI_STATUS_INVALID_PORT`：portIndex无效或不是输出端口。
* `MIDI_STATUS_GENERIC_IPC_FAILURE`：连接系统服务失败。

### OH_MidiGetDevicePorts()

```c
OH_MidiStatusCode OH_MidiGetDevicePorts (OH_MidiClient *client, int64_t deviceId, OH_MidiPortInformation *infos, size_t *numPorts)

```

**描述**

获取端口信息（双次调用模式）。

**模式说明：**

1. 调用时将 `infos` 设为 `nullptr` 以在 `numPorts` 中获取数量。
2. 分配内存。
3. 使用分配的缓冲区再次调用以获取数据。

**起始版本：** 24

**参数:**

| 名称 | 描述 |
| --- | --- |
| client | 目标客户端句柄。 |
| deviceId | 设备ID。 |
| infos | 用户分配的缓冲区，或nullptr。 |
| numPorts | 输入为容量，输出为实际数量。 |

**返回：**

函数执行结果状态码 `OH_MidiStatusCode`：

* `MIDI_STATUS_OK`：操作成功。
* `Midi_STATUS_INVALID_DEVICE_HANDLE`：device无效。
* `MIDI_STATUS_INSUFFICIENT_RESULT_SPACE`：缓冲区容量太小。
* `MIDI_STATUS_GENERIC_INVALID_ARGUMENT`：numPorts或infos为nullptr。
* `MIDI_STATUS_GENERIC_IPC_FAILURE`：连接系统服务失败。

### OH_MidiGetDevices()

```c
OH_MidiStatusCode OH_MidiGetDevices (OH_MidiClient *client, OH_MidiDeviceInformation *infos, size_t *numDevices)

```

**描述**

枚举所有Midi设备（双次调用模式）。

**模式说明：**

1. 调用时将 `infos` 设为 `nullptr` 以在 `numDevices` 中获取数量。
2. 分配内存。
3. 使用分配的缓冲区再次调用以获取数据。

**起始版本：** 24

**参数:**

| 名称 | 描述 |
| --- | --- |
| client | 目标客户端句柄。 |
| infos | 用户分配的缓冲区，或nullptr。 |
| numDevices | 输入为容量，输出为实际数量。 |

**返回：**

函数执行结果状态码 `OH_MidiStatusCode`：

* `MIDI_STATUS_OK`：操作成功。
* `MIDI_STATUS_INVALID_CLIENT`：client无效。
* `MIDI_STATUS_INSUFFICIENT_RESULT_SPACE`：缓冲区容量太小。
* `MIDI_STATUS_GENERIC_INVALID_ARGUMENT`：numDevices或infos为nullptr。
* `MIDI_STATUS_GENERIC_IPC_FAILURE`：连接系统服务失败。

### OH_MidiOpenBleDevice()

```c
OH_MidiStatusCode OH_MidiOpenBleDevice (OH_MidiClient *client, const char *deviceAddr, OH_MidiDevice **device, int64_t *deviceId)

```

**描述**

打开Midi BLE设备。

**需要的权限：** ohos.permission.ACCESS_BLUETOOTH

**起始版本：** 24

**参数:**

| 名称 | 描述 |
| --- | --- |
| client | 目标客户端句柄。 |
| deviceAddr | BLE Mac地址。 |
| device | 输出参数，用于接收设备句柄的指针。 |

**返回：**

函数执行结果状态码 `OH_MidiStatusCode`：

* `MIDI_STATUS_OK`：操作成功。
* `MIDI_STATUS_INVALID_CLIENT`：client无效。
* `MIDI_STATUS_GENERIC_INVALID_ARGUMENT`：device为nullptr，或deviceAddr不存在。
* `MIDI_STATUS_GENERIC_IPC_FAILURE`：连接系统服务失败。

### OH_MidiOpenDevice()

```c
OH_MidiStatusCode OH_MidiOpenDevice (OH_MidiClient *client, int64_t deviceId, OH_MidiDevice **device)

```

**描述**

打开Midi设备。

**起始版本：** 24

**参数:**

| 名称 | 描述 |
| --- | --- |
| client | 目标客户端句柄。 |
| deviceId | 设备ID。 |
| device | 输出参数，用于接收设备句柄的指针。 |

**返回：**

函数执行结果状态码 `OH_MidiStatusCode`：

* `MIDI_STATUS_OK`：操作成功。
* `MIDI_STATUS_INVALID_CLIENT`：client无效。
* `MIDI_STATUS_DEVICE_ALREADY_OPEN`：设备已被该客户端打开。
* `MIDI_STATUS_GENERIC_INVALID_ARGUMENT`：device为nullptr，或deviceId不存在。
* `MIDI_STATUS_GENERIC_IPC_FAILURE`：连接系统服务失败。

### OH_MidiOpenInputPort()

```c
OH_MidiStatusCode OH_MidiOpenInputPort (OH_MidiDevice *device, uint32_t portIndex, OH_OnMidiReceived callback, void *userData)

```

**描述**

打开Midi输入端口（接收数据）。注册一个回调函数以批量接收Midi数据。

**起始版本：** 24

**参数:**

| 名称 | 描述 |
| --- | --- |
| device | 目标设备句柄。 |
| portIndex | 设备上的端口索引。 |
| callback | 当数据可用时调用的回调函数。 |
| userData | 传递给回调函数的上下文指针。 |

**返回：**

函数执行结果状态码 `OH_MidiStatusCode`：

* `MIDI_STATUS_OK`：操作成功。
* `MIDI_STATUS_INVALID_DEVICE_HANDLE`：device无效。
* `MIDI_STATUS_INVALID_PORT`：portIndex无效或不是输入端口。
* `MIDI_STATUS_GENERIC_INVALID_ARGUMENT`：inputHandler为nullptr。
* `MIDI_STATUS_GENERIC_IPC_FAILURE`：连接系统服务失败。

### OH_MidiOpenOutputPort()

```c
OH_MidiStatusCode OH_MidiOpenOutputPort (OH_MidiDevice *device, OH_MidiPortDescriptor descriptor)

```

**描述**

打开Midi输出端口（发送数据）。

**起始版本：** 24

**参数:**

| 名称 | 描述 |
| --- | --- |
| device | 目标设备句柄。 |
| descriptor | 端口索引和协议配置描述符。 |

**返回：**

函数执行结果状态码 `OH_MidiStatusCode`：

* `MIDI_STATUS_OK`：操作成功。
* `MIDI_STATUS_INVALID_DEVICE_HANDLE`：device无效。
* `MIDI_STATUS_INVALID_PORT`：portIndex无效或不是输出端口。
* `MIDI_STATUS_GENERIC_IPC_FAILURE`：连接系统服务失败。

### OH_MidiSend()

```c
OH_MidiStatusCode OH_MidiSend (OH_MidiDevice *device, uint32_t portIndex, OH_MidiEvent *events, uint32_t eventCount, uint32_t *eventsWritten)

```

**描述**

发送Midi消息（批量、非阻塞且原子操作）。尝试将事件数组写入共享内存缓冲区。

* **原子性（Atomicity）**：数组中的每个事件都被视为原子操作。它要么完全写入，要么根本不写入。
* **部分成功（Partial Success）**：如果缓冲区中途变满，函数返回 `MIDI_STATUS_WOULD_BLOCK`，并将 `eventsWritten` 设置为成功入队的事件数量。

**起始版本：** 24

**参数:**

| 名称 | 描述 |
| --- | --- |
| device | 目标设备句柄。 |
| portIndex | 目标端口索引。 |
| events | 指向要发送的事件数组的指针。 |
| eventCount | 数组中的事件数量。 |
| eventsWritten | 返回成功消费的事件数量。 |

**返回：**

函数执行结果状态码 `OH_MidiStatusCode`：

* `MIDI_STATUS_OK`：所有事件已写入。
* `MIDI_STATUS_INVALID_DEVICE_HANDLE`：device无效。
* `MIDI_STATUS_INVALID_PORT`：portIndex无效或未打开。
* `MIDI_STATUS_WOULD_BLOCK`：缓冲区已满（请检查 eventsWritten）。
* `MIDI_STATUS_GENERIC_INVALID_ARGUMENT`：参数无效。
* `MIDI_STATUS_GENERIC_IPC_FAILURE`：连接系统服务失败。

### OH_MidiSendSysEx()

```c
OH_MidiStatusCode OH_MidiSendSysEx (OH_MidiDevice *device, uint32_t portIndex, uint8_t *data, uint32_t byteSize)

```

**描述**

发送长SysEx消息（字节流到UMP的辅助函数）。

这是一个为处理SysEx作为原始字节流（Midi 1.0 风格，F0...F7）的应用程序提供的**工具函数**。它适用于 `MIDI_PROTOCOL_1_0` 和 `MIDI_PROTOCOL_2_0` 会话。底层服务根据设备的实际能力处理最终转换。

**工作原理：**

1. 自动将原始字节分片为一系列 UMP Type 3（64位数据消息）数据包。
2. 使用 `OH_MidiSend` 顺序发送这些数据包。

**警告**：**阻塞调用**。此函数执行循环操作，如果缓冲区填满可能会阻塞。

**起始版本：** 24

**参数:**

| 名称 | 描述 |
| --- | --- |
| device | 目标设备句柄。 |
| portIndex | 目标端口索引。 |
| data | 指向要发送的数据数组的指针。 |
| byteSize | 数组中的字节数。 |

**返回：**

函数执行结果状态码 `OH_MidiStatusCode`：

* `MIDI_STATUS_OK`：所有事件已写入。
* `MIDI_STATUS_INVALID_DEVICE_HANDLE`：device无效。
* `MIDI_STATUS_INVALID_PORT`：portIndex无效或未打开。
* `MIDI_STATUS_TIMEOUT`：无法在合理时间内完成（可以使用 OH_MidiFlushOutputPort 重置）。
* `MIDI_STATUS_GENERIC_INVALID_ARGUMENT`：参数无效。