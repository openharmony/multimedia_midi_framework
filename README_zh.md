# midi_framework

## 简介

midi_framework部件为OpenHarmony系统提供了统一的MIDI设备访问、数据传输及协议处理能力，使得应用能够直接调用系统提供的接口实现外部MIDI设备的发现、连接以及高性能的指令收发功能。

midi_framework部件提供了以下常用功能：

* MIDI设备发现、热插拔监听（USB MIDI 设备）以及链接
* MIDI数据传输（基于UMP协议的发送与接收）

**图 1** midi_framework部件架构图

### 模块介绍

| 模块名称           | 功能                                                                                                           |
| ------------------ | -------------------------------------------------------------------------------------------------------------- |
| MIDI客户端         | 提供MIDI客户端创建、销毁及回调注册的接口，是应用与服务交互的入口。                                             |
| 设备发现           | 提供当前系统连接的MIDI设备枚举、设备信息查询及热插拔事件监听接口。                                             |
| 数据传输           | 提供基于UMP（Universal MIDI Packet）格式的高性能数据发送（Send）与批量接收（OnReceived）接口。                 |
| 客户端生命周期管理 | 用于服务端管理应用的连接会话、资源配额及权限控制。                                                             |
| 设备连接管理       | 用于服务端管理物理设备的独占/共享连接状态、USB/BLE链路的建立与断开。                                           |
| 端口管理           | 管理设备的输入/输出端口逻辑，处理端口的打开、关闭及多客户端路由分发。                                          |
| 协议处理器         | 用于处理MIDI 2.0（UMP）与MIDI 1.0（Byte Stream）之间的自动转换逻辑。                                           |
| USB设备适配        | 服务端加载HDI直通库的模块，负责与底层ALSA/USB驱动交互。                                                        |
| BLE设备适配        | 服务端对接系统蓝牙服务的模块，负责BLE MIDI协议的封包与解包。                                                   |
| MIDI硬件驱动HDI    | 提供MIDI硬件驱动的抽象接口，通过该接口对服务层屏蔽底层硬件差异（如ALSA节点），向服务层提供统一的数据读写能力。 |

## 目录

```
/foundation/multimedia/midi_framework   # midi_framework部件业务代码
├── bundle.json                         # 部件编译文件
├── frameworks                          # Midi组件业务代码
│   └── native                          # 内部接口实现
├── interfaces                          # 接口代码
│   ├── inner_api                       # 系统内部件接口
│   └── kits                            # 外部接口
├── sa_profile                          # 系统服务配置
├── services                            # 服务实现代码
│   ├── etc                             # 部件进程启动配置 (.cfg)
│   ├── idl                             # Midi服务IPC接口定义
│   ├── server                          # Midi服务核心实现
│   └── common                          # Midi服务通用实现
└── test                                # 测试代码
    ├── fuzztest                        # fuzz测试
    └── unittest                        # 单元测试

```

## 编译构建

编译32位ARM系统midi_framework部件

```
./build.sh --product-name {product_name} --ccache --build-target midi_framework

```

编译64位ARM系统midi_framework部件

```
./build.sh --product-name {product_name} --ccache --target-cpu arm64 --build-target midi_framework

```

{product_name}为当前支持的平台，比如rk3568。

## 说明

### 使用说明



## 相关仓

* [媒体子系统](https://gitee.com/openharmony/docs/blob/master/zh-cn/readme/媒体子系统.md)