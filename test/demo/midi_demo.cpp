/*
 * Copyright (C) 2024 Huawei Device Co., Ltd.
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

#include <chrono>
#include <iomanip>
#include <iostream>
#include <native_midi.h>
#include <string>
#include <thread>
#include <vector>

using namespace std;

namespace {
constexpr int32_t HEX_WIDTH = 8;
constexpr OH_MidiProtocol MIDI_PROTOCOL_VERSION = MIDI_PROTOCOL_1_0;
constexpr int32_t NOTE_ON_DELAY_MS = 500;
// MIDI 1.0 Note On (Ch0, Note 60, Vel 100)
constexpr uint32_t NOTE_ON_MSG = 0x20903C64;
// MIDI 1.0 Note Off (Ch0, Note 60, Vel 0)
constexpr uint32_t NOTE_OFF_MSG = 0x20803C00;

void PrintUmpData(const uint32_t *data, size_t length)
{
    if (!data) {
        return;
    }
    cout << "[Rx] Data: ";
    for (size_t i = 0; i < length; ++i) {
        cout << "0x" << setfill('0') << setw(HEX_WIDTH) << hex << data[i] << " ";
    }
    cout << dec << endl;
}
} // namespace

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
        cout << "[Rx] Timestamp=" << events[i].timestamp << " ";
        PrintUmpData(events[i].data, events[i].length);
    }
}

// 发送 MIDI 数据的 Demo 逻辑
static void SendMidiNote(OH_MidiDevice *device, int8_t portIndex)
{
    uint32_t noteOnMsg[1] = {NOTE_ON_MSG};
    OH_MidiEvent event;
    event.timestamp = 0;
    event.length = 1;
    event.data = noteOnMsg;

    uint32_t written = 0;
    int ret = OH_MidiSend(device, portIndex, &event, 1, &written);
    if (ret == MIDI_STATUS_OK) {
        cout << "[Tx] Note On sent to port " << static_cast<int>(portIndex) << endl;
    } else {
        cout << "[Tx] Failed to send data, error: " << ret << endl;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(NOTE_ON_DELAY_MS));

    uint32_t noteOffMsg[1] = {NOTE_OFF_MSG};
    event.data = noteOffMsg;
    OH_MidiSend(device, portIndex, &event, 1, &written);
    cout << "[Tx] Note Off sent to port " << static_cast<int>(portIndex) << endl;
}

// 辅助函数：初始化并打开端口
static void SetupPorts(OH_MidiClient *client, OH_MidiDevice *device, int64_t deviceId, vector<int> &outOpenedPorts)
{
    size_t portCount = 0;
    OH_MidiGetDevicePorts(client, deviceId, nullptr, &portCount);
    if (portCount == 0) {
        return;
    }

    vector<OH_MidiPortInformation> ports(portCount);
    OH_MidiGetDevicePorts(client, deviceId, ports.data(), &portCount);

    for (const auto &port : ports) {
        OH_MidiPortDescriptor desc = {port.portIndex, MIDI_PROTOCOL_VERSION};

        if (port.direction == MIDI_PORT_DIRECTION_INPUT) {
            if (OH_MidiOpenInputPort(device, desc, OnMidiReceived, nullptr) == MIDI_STATUS_OK) {
                cout << "Input Port " << port.portIndex << " opened (Listening...)" << endl;
                outOpenedPorts.push_back(port.portIndex);
            }
        } else if (port.direction == MIDI_PORT_DIRECTION_OUTPUT) {
            if (OH_MidiOpenOutputPort(device, desc) == MIDI_STATUS_OK) {
                cout << "Output Port " << port.portIndex << " opened." << endl;
                outOpenedPorts.push_back(port.portIndex);
                SendMidiNote(device, port.portIndex);
            }
        }
    }
}

// 主 MIDI 运行逻辑
static int RunMidiDemo()
{
    cout << "Starting MIDI Demo..." << endl;

    OH_MidiClient *client = nullptr;
    OH_MidiCallbacks callbacks = {OnDeviceChange, OnError};

    if (OH_MidiClientCreate(&client, callbacks, nullptr) != MIDI_STATUS_OK) {
        cout << "Failed to create MIDI client." << endl;
        return -1;
    }

    size_t devCount = 0;
    OH_MidiGetDevices(client, nullptr, &devCount);
    if (devCount == 0) {
        cout << "No MIDI devices found." << endl;
        OH_MidiClientDestroy(client);
        return 0;
    }

    vector<OH_MidiDeviceInformation> devices(devCount);
    OH_MidiGetDevices(client, devices.data(), &devCount);

    // 默认操作第一个设备
    int64_t targetDeviceId = devices[0].midiDeviceId;
    cout << "Opening Device: " << devices[0].productName << " (ID: " << targetDeviceId << ")" << endl;

    OH_MidiDevice *device = nullptr;
    if (OH_MidiOpenDevice(client, targetDeviceId, &device) != MIDI_STATUS_OK || !device) {
        cout << "Failed to open device." << endl;
        OH_MidiClientDestroy(client);
        return -1;
    }

    vector<int> openedPorts;
    SetupPorts(client, device, targetDeviceId, openedPorts);

    cout << "\n=== MIDI Running. Press ENTER to stop... ===" << endl;
    string dummy;
    getline(cin, dummy);

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
    RunMidiDemo();
    return 0;
}