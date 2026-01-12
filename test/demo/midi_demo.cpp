/*
 * Copyright (C) 2025 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <iomanip>
#include <string>
#include <native_midi.h>

using namespace std;

namespace {
// 定义 MIDI 协议类型
constexpr OH_MidiProtocol DEMO_MIDI_PROTOCOL = MIDI_PROTOCOL_1_0;

// 辅助函数：将 UMP 数据打印为 Hex 格式
void PrintUmpData(const uint32_t *data, size_t length)
{
    if (!data)
        return;
    cout << "[Rx] Data: ";
    for (size_t i = 0; i < length; ++i) {
        cout << "0x" << setfill('0') << setw(8) << hex << data[i] << " ";
    }
    cout << dec << endl;
}
}  // namespace

// 1. 设备热插拔回调
static void OnDeviceChange(void *userData, OH_MidiDeviceChangeAction action, OH_MidiDeviceInformation info)
{
    if (action == MIDI_DEVICE_CHANGE_ACTION_CONNECTED) {
        cout << "[Hotplug] Device Connected: ID=" << info.midiDeviceId << ", Name=" << info.productName << endl;
    } else if (action == MIDI_DEVICE_CHANGE_ACTION_DISCONNECTED) {
        cout << "[Hotplug] Device Disconnected: ID=" << info.midiDeviceId << endl;
    }
}

// 2. 服务错误回调
static void OnError(void *userData, OH_MidiStatusCode code)
{
    cout << "[Error] Critical Service Error! Code=" << code << endl;
}

// 3. 数据接收回调
static void OnMidiReceived(void *userData, const OH_MidiEvent *events, size_t eventCount)
{
    for (size_t i = 0; i < eventCount; ++i) {
        // 打印时间戳和 UMP 数据
        cout << "[Rx] Timestamp=" << events[i].timestamp << " ";
        PrintUmpData(events[i].data, events[i].length);
    }
}

// 发送 MIDI 数据的 Demo 逻辑
static void SendMidiNote(OH_MidiDevice *device, int8_t portIndex)
{
    // 构建 MIDI 1.0 Note On 消息 (Channel 0, Note 60, Velocity 100)
    // UMP Format (32-bit): [ MT(4) | Group(4) | Status(8) | Note(8) | Velocity(8) ]
    // 0x2 (MT=MIDI 1.0 Ch Voice) | 0x0 (Group 0) | 0x90 (NoteOn Ch0) | 0x3C (60) | 0x64 (100)
    uint32_t noteOnMsg[1] = {0x20903C64};

    OH_MidiEvent event;
    event.timestamp = 0;  // 0 表示立即发送
    event.length = 1;     // 数据长度 (1个32位字)
    event.data = noteOnMsg;

    uint32_t written = 0;
    int ret = OH_MidiSend(device, portIndex, &event, 1, &written);
    if (ret == MIDI_STATUS_OK) {
        cout << "[Tx] Note On sent to port " << (int)portIndex << endl;
    } else {
        cout << "[Tx] Failed to send data, error: " << ret << endl;
    }

    // 延时 500ms 后发送 Note Off
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Note Off (Velocity 0)
    uint32_t noteOffMsg[1] = {0x20803C00};
    event.data = noteOffMsg;
    OH_MidiSend(device, portIndex, &event, 1, &written);
    cout << "[Tx] Note Off sent to port " << (int)portIndex << endl;
}

// 主 MIDI 运行逻辑
static int RunMidiDemo()
{
    cout << "Starting MIDI Demo..." << endl;

    // 1. 创建客户端
    OH_MidiClient *client = nullptr;
    OH_MidiCallbacks callbacks;
    callbacks.onDeviceChange = OnDeviceChange;
    callbacks.onError = OnError;

    OH_MidiStatusCode ret = OH_MidiClientCreate(&client, callbacks, nullptr);
    if (ret != MIDI_STATUS_OK) {
        cout << "Failed to create MIDI client." << endl;
        return -1;
    }

    // 2. 获取设备列表
    size_t devCount = 0;
    OH_MidiGetDevices(client, nullptr, &devCount);

    if (devCount == 0) {
        cout << "No MIDI devices found." << endl;
        OH_MidiClientDestroy(client);
        return 0;
    }

    cout << "Found " << devCount << " device(s)." << endl;
    vector<OH_MidiDeviceInformation> devices(devCount);
    OH_MidiGetDevices(client, devices.data(), &devCount);

    // 3. 默认操作第一个设备（实际场景可增加选择逻辑）
    int64_t targetDeviceId = devices[0].midiDeviceId;
    string deviceName = devices[0].productName;
    cout << "Opening Device: " << deviceName << " (ID: " << targetDeviceId << ")" << endl;

    // 4. 打开设备
    OH_MidiDevice *device = nullptr;
    ret = OH_MidiOpenDevice(client, targetDeviceId, &device);
    if (ret != MIDI_STATUS_OK || device == nullptr) {
        cout << "Failed to open device." << endl;
        OH_MidiClientDestroy(client);
        return -1;
    }

    // 5. 获取端口信息
    size_t portCount = 0;
    OH_MidiGetDevicePorts(client, targetDeviceId, nullptr, &portCount);
    vector<OH_MidiPortInformation> ports(portCount);
    OH_MidiGetDevicePorts(client, targetDeviceId, ports.data(), &portCount);

    vector<int> openedPorts;

    // 6. 遍历并打开端口
    for (const auto &port : ports) {
        OH_MidiPortDescriptor desc = {port.portIndex, DEMO_MIDI_PROTOCOL};

        if (port.direction == MIDI_PORT_DIRECTION_INPUT) {
            // 打开输入端口 (接收)
            if (OH_MidiOpenInputPort(device, desc, OnMidiReceived, nullptr) == MIDI_STATUS_OK) {
                cout << "Input Port " << port.portIndex << " opened (Listening...)" << endl;
                openedPorts.push_back(port.portIndex);
            }
        } else if (port.direction == MIDI_PORT_DIRECTION_OUTPUT) {
            // 打开输出端口 (发送)
            if (OH_MidiOpenOutputPort(device, desc) == MIDI_STATUS_OK) {
                cout << "Output Port " << port.portIndex << " opened." << endl;
                openedPorts.push_back(port.portIndex);
                // 简单的发送测试
                SendMidiNote(device, port.portIndex);
            }
        }
    }

    cout << "\n========================================" << endl;
    cout << "MIDI Running. Press ENTER to stop..." << endl;
    cout << "========================================" << endl;

    // 阻塞主线程，等待用户输入以退出
    string dummy;
    getline(cin, dummy);

    // 7. 资源清理
    cout << "Cleaning up..." << endl;
    for (int portIndex : openedPorts) {
        OH_MidiClosePort(device, portIndex);
    }
    OH_MidiCloseDevice(device);
    OH_MidiClientDestroy(client);

    cout << "MIDI Demo End." << endl;
    return 0;
}

int main()
{
    (void)RunMidiDemo();
    return 0;
}