/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
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

#ifndef LOG_TAG
#define LOG_TAG "MidiIpcStubFuzzTest"
#endif

#include <algorithm>
#include <cstdint>
#include <fcntl.h>
#include <fuzzer/FuzzedDataProvider.h>
#include <memory>
#include <securec.h>
#include <string>
#include <unordered_set>
#include <unistd.h>
#include <vector>

#include "accesstoken_kit.h"
#include "iremote_object.h"
#include "ipc_skeleton.h"
#include "message_option.h"
#include "message_parcel.h"
#include "nativetoken_kit.h"
#include "token_setproc.h"

#include "iipc_midi_in_server.h"
#include "imidi_callback.h"
#include "imidi_device_open_callback.h"
#include "imidi_service.h"
#include "midi_callback_stub.h"
#include "midi_device_driver.h"
#include "midi_device_mananger.h"
#include "midi_info.h"
#include "midi_log.h"
#include "midi_server.h"
#include "midi_service_controller.h"
#include "midi_shared_ring.h"

namespace OHOS {
namespace MIDI {
using namespace std;

namespace {
constexpr int64_t TEST_DRIVER_ID_USB1 = 1001;
constexpr int64_t TEST_DRIVER_ID_USB2 = 1005;
constexpr int64_t TEST_DRIVER_ID_BLE = 2001;
constexpr int32_t TEST_SA_ID = 123;
constexpr int32_t RING_BUFFER_DEFAULT_SIZE = 2048;
constexpr int32_t REPEAT_COMMAND_COUNT = 2;
constexpr const char *ACCESS_BLUETOOTH_PERMISSION = "ohos.permission.ACCESS_BLUETOOTH";
constexpr uint8_t MODE_HAPPY_PATH = 0;
constexpr uint8_t MODE_BAD_TOKEN = 1;
constexpr uint8_t MODE_INVALID_CLIENT_ID = 2;
constexpr uint8_t MODE_INVALID_DEVICE_ID = 3;
constexpr uint8_t MODE_REPEAT_OPEN_CLOSE = 4;
constexpr uint8_t MODE_DESTROY_THEN_OPERATE = 5;
constexpr uint8_t MODE_QUERY_DEVICES = 6;
constexpr uint8_t MODE_OPEN_BLE_DEVICE = 7;
constexpr uint8_t FUZZ_MODE_COUNT = 8;
} // namespace

// ====== Test-only stubs ======

class MidiCallbackFuzzer : public MidiCallbackStub {
public:
    int32_t NotifyDeviceChange(int32_t change, const MidiDeviceInfo &deviceInfo) override { return 0; }
    int32_t NotifyError(int32_t code) override { return 0; }

    bool AddDeathRecipient(const sptr<DeathRecipient> &recipient) override
    {
        if (recipient == nullptr) {
            return false;
        }
        deathRecipients_.push_back(recipient);
        return true;
    }

    bool RemoveDeathRecipient(const sptr<DeathRecipient> &recipient) override
    {
        auto it = std::find(deathRecipients_.begin(), deathRecipients_.end(), recipient);
        if (it == deathRecipients_.end()) {
            return false;
        }
        deathRecipients_.erase(it);
        return true;
    }

private:
    std::vector<sptr<DeathRecipient>> deathRecipients_;
};

class MidiDeviceOpenCallbackFuzzer : public IRemoteStub<IMidiDeviceOpenCallback> {
public:
    int32_t OnRemoteRequest(uint32_t code, MessageParcel &data, MessageParcel &reply,
                            MessageOption &option) override
    {
        (void)code;
        (void)data;
        (void)reply;
        (void)option;
        return 0;
    }
    int32_t NotifyDeviceOpened(bool opened, const MidiDeviceInfo &deviceInfo) override { return 0; }

    bool AddDeathRecipient(const sptr<DeathRecipient> &recipient) override
    {
        if (recipient == nullptr) {
            return false;
        }
        deathRecipients_.push_back(recipient);
        return true;
    }

    bool RemoveDeathRecipient(const sptr<DeathRecipient> &recipient) override
    {
        auto it = std::find(deathRecipients_.begin(), deathRecipients_.end(), recipient);
        if (it == deathRecipients_.end()) {
            return false;
        }
        deathRecipients_.erase(it);
        return true;
    }

private:
    std::vector<sptr<DeathRecipient>> deathRecipients_;
};

// ====== Mock drivers ======

class MockUsbDriver : public MidiDeviceDriver {
public:
    std::vector<DeviceInformation> GetRegisteredDevices() override { return mockDevices_; }

    int32_t OpenDevice(int64_t deviceId) override
    {
        if (openedDevices_.count(deviceId) > 0) {
            return OH_MIDI_STATUS_DEVICE_ALREADY_OPEN;
        }
        openedDevices_.insert(deviceId);
        return OH_MIDI_STATUS_OK;
    }

    int32_t OpenDevice(std::string deviceAddr, BleDriverCallback deviceCallback) override
    {
        (void)deviceAddr;
        (void)deviceCallback;
        return OH_MIDI_STATUS_OK;
    }

    int32_t CloseDevice(int64_t deviceId) override
    {
        openedDevices_.erase(deviceId);
        return OH_MIDI_STATUS_OK;
    }

    int32_t OpenInputPort(int64_t deviceId, uint32_t portIndex, UmpInputCallback cb) override
    {
        uint64_t key = (static_cast<uint64_t>(deviceId) << 32) | portIndex;
        if (openedInputPorts_.count(key) > 0) {
            return OH_MIDI_STATUS_PORT_ALREADY_OPEN;
        }
        openedInputPorts_.insert(key);
        return OH_MIDI_STATUS_OK;
    }

    int32_t OpenOutputPort(int64_t deviceId, uint32_t portIndex) override
    {
        uint64_t key = (static_cast<uint64_t>(deviceId) << 32) | portIndex;
        if (openedOutputPorts_.count(key) > 0) {
            return OH_MIDI_STATUS_PORT_ALREADY_OPEN;
        }
        openedOutputPorts_.insert(key);
        return OH_MIDI_STATUS_OK;
    }

    int32_t CloseInputPort(int64_t deviceId, uint32_t portIndex) override
    {
        uint64_t key = (static_cast<uint64_t>(deviceId) << 32) | portIndex;
        openedInputPorts_.erase(key);
        return OH_MIDI_STATUS_OK;
    }

    int32_t CloseOutputPort(int64_t deviceId, uint32_t portIndex) override
    {
        uint64_t key = (static_cast<uint64_t>(deviceId) << 32) | portIndex;
        openedOutputPorts_.erase(key);
        return OH_MIDI_STATUS_OK;
    }

    int32_t HandleUmpInput(int64_t deviceId, uint32_t portIndex, std::vector<MidiEventInner> &list) override
    {
        (void)deviceId;
        (void)portIndex;
        (void)list;
        return OH_MIDI_STATUS_OK;
    }

    void AddMockDevice(int64_t driverId, const std::string &name, DeviceType type)
    {
        DeviceInformation info;
        info.midiDeviceInfo.driverDeviceId = driverId;
        info.midiDeviceInfo.deviceType = type;
        info.midiDeviceInfo.deviceName = name;
        info.midiDeviceInfo.productId = 0x1234;
        info.midiDeviceInfo.vendorId = 0x5678;
        info.midiDeviceInfo.transportProtocol = TransportProtocol::PROTOCOL_1_0;

        MidiPortInfo port1;
        port1.portId = 0;
        port1.direction = PortDirection::PORT_DIRECTION_INPUT;
        port1.name = "Input Port";
        info.portInfos.push_back(port1);

        MidiPortInfo port2;
        port2.portId = 1;
        port2.direction = PortDirection::PORT_DIRECTION_OUTPUT;
        port2.name = "Output Port";
        info.portInfos.push_back(port2);

        mockDevices_.push_back(info);
    }

private:
    std::vector<DeviceInformation> mockDevices_;
    std::unordered_set<int64_t> openedDevices_;
    std::unordered_set<uint64_t> openedInputPorts_;
    std::unordered_set<uint64_t> openedOutputPorts_;
};

class MockBleDriver : public MidiDeviceDriver {
public:
    std::vector<DeviceInformation> GetRegisteredDevices() override { return mockDevices_; }

    int32_t OpenDevice(int64_t deviceId) override
    {
        (void)deviceId;
        return OH_MIDI_STATUS_OK;
    }

    int32_t OpenDevice(std::string deviceAddr, BleDriverCallback deviceCallback) override
    {
        (void)deviceAddr;
        // Codex finding 4: 保存 callback 指针，harness 在 OnRemoteRequest 返回后主动触发
        savedCallback_ = std::move(deviceCallback);
        savedAddr_ = deviceAddr;
        return OH_MIDI_STATUS_OK;
    }

    int32_t CloseDevice(int64_t deviceId) override
    {
        (void)deviceId;
        return OH_MIDI_STATUS_OK;
    }

    int32_t OpenInputPort(int64_t deviceId, uint32_t portIndex, UmpInputCallback cb) override
    {
        (void)deviceId;
        (void)portIndex;
        (void)cb;
        return OH_MIDI_STATUS_OK;
    }

    int32_t OpenOutputPort(int64_t deviceId, uint32_t portIndex) override
    {
        (void)deviceId;
        (void)portIndex;
        return OH_MIDI_STATUS_OK;
    }

    int32_t CloseInputPort(int64_t deviceId, uint32_t portIndex) override
    {
        (void)deviceId;
        (void)portIndex;
        return OH_MIDI_STATUS_OK;
    }

    int32_t CloseOutputPort(int64_t deviceId, uint32_t portIndex) override
    {
        (void)deviceId;
        (void)portIndex;
        return OH_MIDI_STATUS_OK;
    }

    int32_t HandleUmpInput(int64_t deviceId, uint32_t portIndex, std::vector<MidiEventInner> &list) override
    {
        (void)deviceId;
        (void)portIndex;
        (void)list;
        return OH_MIDI_STATUS_OK;
    }

    void AddMockBleDevice(int64_t driverId, const std::string &name)
    {
        DeviceInformation info;
        info.midiDeviceInfo.driverDeviceId = driverId;
        info.midiDeviceInfo.deviceType = DeviceType::DEVICE_TYPE_BLE;
        info.midiDeviceInfo.deviceName = name;
        info.midiDeviceInfo.productId = 0xBEEF;
        info.midiDeviceInfo.vendorId = 0xCAFE;
        info.midiDeviceInfo.transportProtocol = TransportProtocol::PROTOCOL_1_0;

        MidiPortInfo port1;
        port1.portId = 0;
        port1.direction = PortDirection::PORT_DIRECTION_INPUT;
        port1.name = "BLE Input";
        info.portInfos.push_back(port1);

        MidiPortInfo port2;
        port2.portId = 1;
        port2.direction = PortDirection::PORT_DIRECTION_OUTPUT;
        port2.name = "BLE Output";
        info.portInfos.push_back(port2);

        mockDevices_.push_back(info);
    }

    // Harness 主动触发 callback（OnRemoteRequest 返回后调用）
    void TriggerSavedCallback(bool success)
    {
        if (savedCallback_) {
            DeviceInformation info;
            if (!mockDevices_.empty()) {
                info = mockDevices_[0];
            }
            savedCallback_(success, info);
            savedCallback_ = nullptr;
        }
    }

    bool HasSavedCallback() const { return static_cast<bool>(savedCallback_); }

private:
    std::vector<DeviceInformation> mockDevices_;
    BleDriverCallback savedCallback_;
    std::string savedAddr_;
};

// ====== Globals ======

std::shared_ptr<MidiServiceController> g_controller;
sptr<MidiServer> g_midiServer;
sptr<MidiCallbackFuzzer> g_callbackStub;
sptr<MidiDeviceOpenCallbackFuzzer> g_deviceOpenCallbackStub;
MockBleDriver *g_mockBleDriver = nullptr;  // raw pointer for harness access (non-owning)

struct ClientContext {
    sptr<IRemoteObject> clientObj;
    uint32_t clientId = 0;
    std::shared_ptr<MidiSharedRing> buffer;
};

std::vector<ClientContext> g_activeClients;
std::vector<int64_t> g_activeDevices;

// ====== IPC Parcel Helpers ======

// 走 MidiServiceStub::OnRemoteRequest (descriptor = u"OHOS.MIDI.IMidiService")
// code = COMMAND_CREATE_MIDI_IN_SERVER (0)
// parcel: interfaceToken + IRemoteObject(callback)
int32_t IpcCreateMidiInServer(sptr<IRemoteObject> callbackObj, sptr<IRemoteObject> &outClient,
                              uint32_t &outClientId, bool badToken = false)
{
    MessageParcel data;
    MessageParcel reply;
    MessageOption option;
    std::u16string token = badToken ? u"OHOS.MIDI.WRONG" : u"OHOS.MIDI.IMidiService";
    if (!data.WriteInterfaceToken(token)) {
        return -1;
    }
    if (!data.WriteRemoteObject(callbackObj)) {
        return -1;
    }
    int32_t ret = g_midiServer->OnRemoteRequest(
        static_cast<uint32_t>(IMidiServiceIpcCode::COMMAND_CREATE_MIDI_IN_SERVER), data, reply, option);
    if (ret != 0) {
        return ret;
    }
    int32_t errCode = 0;
    if (!reply.ReadInt32(errCode)) {
        return -1;
    }
    if (errCode != 0) {
        return errCode;
    }
    outClient = reply.ReadRemoteObject();
    outClientId = reply.ReadUint32();
    return 0;
}

// 走 IpcMidiInServerStub::OnRemoteRequest (descriptor = u"OHOS.MIDI.IIpcMidiInServer")
// 通用 parcel 构造器：caller 必须先 WriteInterfaceToken，再写参数（顺序匹配 stub ReadInterfaceToken）
int32_t CallIpcMidiInServer(sptr<IRemoteObject> clientObj, uint32_t code, MessageParcel &data,
                            MessageParcel &reply)
{
    MessageOption option;
    auto *stub = reinterpret_cast<IpcMidiInServerStub *>(clientObj.GetRefPtr());
    if (stub == nullptr) {
        return -1;
    }
    return stub->OnRemoteRequest(code, data, reply, option);
}

// ====== Scenario helpers ======

bool EnsureActiveClient(FuzzedDataProvider &fdp)
{
    if (!g_activeClients.empty()) {
        return true;
    }
    if (g_callbackStub == nullptr) {
        g_callbackStub = new MidiCallbackFuzzer();
    }
    sptr<IRemoteObject> clientObj;
    uint32_t clientId = 0;
    int32_t ret = IpcCreateMidiInServer(g_callbackStub->AsObject(), clientObj, clientId);
    if (ret != 0 || clientObj == nullptr) {
        return false;
    }
    ClientContext ctx;
    ctx.clientObj = clientObj;
    ctx.clientId = clientId;
    ctx.buffer = std::make_shared<MidiSharedRing>(RING_BUFFER_DEFAULT_SIZE);
    g_activeClients.push_back(std::move(ctx));
    return true;
}

void DrainBleCallback(bool success)
{
    if (g_mockBleDriver != nullptr && g_mockBleDriver->HasSavedCallback()) {
        g_mockBleDriver->TriggerSavedCallback(success);
    }
}

void RebuildState()
{
    // 清理 active clients
    for (auto &c : g_activeClients) {
        if (g_controller != nullptr) {
            g_controller->DestroyMidiClient(c.clientId);
        }
    }
    g_activeClients.clear();
    if (g_controller != nullptr) {
        g_controller->ClearStateForTest();
    }

    // 重新注入 USB mock driver
    auto usbDriver = std::make_unique<MockUsbDriver>();
    usbDriver->AddMockDevice(TEST_DRIVER_ID_USB1, "USB MIDI Device 1", DeviceType::DEVICE_TYPE_USB);
    usbDriver->AddMockDevice(TEST_DRIVER_ID_USB2, "USB MIDI Device 2", DeviceType::DEVICE_TYPE_USB);
    g_controller->GetDeviceManagerForTest()->InjectDriverForTest(DeviceType::DEVICE_TYPE_USB, std::move(usbDriver));

    // 重新注入 BLE mock driver（保存 raw pointer 供 harness 触发 callback）
    auto bleDriver = std::make_unique<MockBleDriver>();
    bleDriver->AddMockBleDevice(TEST_DRIVER_ID_BLE, "BLE MIDI Device 1");
    g_mockBleDriver = bleDriver.get();
    g_controller->GetDeviceManagerForTest()->InjectDriverForTest(DeviceType::DEVICE_TYPE_BLE, std::move(bleDriver));

    g_controller->GetDeviceManagerForTest()->UpdateDevices();
    auto devices = g_controller->GetDeviceManagerForTest()->GetDevices();
    g_activeDevices.clear();
    for (const auto &dev : devices) {
        g_activeDevices.push_back(dev.midiDeviceInfo.deviceId);
    }
}

// ====== Mode 0: Happy path —— 6 步完整 IPC 生命周期 ======
void ModeHappyPath(FuzzedDataProvider &fdp)
{
    if (!EnsureActiveClient(fdp)) {
        return;
    }
    auto &client = g_activeClients[0];
    if (g_activeDevices.empty()) {
        return;
    }
    int64_t deviceId = g_activeDevices[0];

    // OpenDevice (IPC code=2)
    {
        MessageParcel data;
        MessageParcel reply;
        data.WriteInterfaceToken(u"OHOS.MIDI.IIpcMidiInServer");
        data.WriteInt64(deviceId);
        CallIpcMidiInServer(client.clientObj, static_cast<uint32_t>(IIpcMidiInServerIpcCode::COMMAND_OPEN_DEVICE),
                            data, reply);
    }

    // OpenInputPort (IPC code=4)
    {
        MessageParcel data;
        MessageParcel reply;
        data.WriteInterfaceToken(u"OHOS.MIDI.IIpcMidiInServer");
        data.WriteInt64(deviceId);
        data.WriteUint32(0);
        CallIpcMidiInServer(client.clientObj, static_cast<uint32_t>(IIpcMidiInServerIpcCode::COMMAND_OPEN_INPUT_PORT),
                            data, reply);
    }

    // OpenOutputPort (IPC code=5)
    {
        MessageParcel data;
        MessageParcel reply;
        data.WriteInterfaceToken(u"OHOS.MIDI.IIpcMidiInServer");
        data.WriteInt64(deviceId);
        data.WriteUint32(1);
        CallIpcMidiInServer(client.clientObj, static_cast<uint32_t>(IIpcMidiInServerIpcCode::COMMAND_OPEN_OUTPUT_PORT),
                            data, reply);
    }

    // DestroyMidiClient (IPC code=10)
    {
        MessageParcel data;
        MessageParcel reply;
        data.WriteInterfaceToken(u"OHOS.MIDI.IIpcMidiInServer");
        CallIpcMidiInServer(client.clientObj,
                            static_cast<uint32_t>(IIpcMidiInServerIpcCode::COMMAND_DESTROY_MIDI_CLIENT), data, reply);
    }
    g_activeClients.clear();
}

// ====== Mode 1: Bad token ======
void ModeBadToken(FuzzedDataProvider &fdp)
{
    if (g_callbackStub == nullptr) {
        g_callbackStub = new MidiCallbackFuzzer();
    }
    sptr<IRemoteObject> clientObj;
    uint32_t clientId = 0;
    // CreateMidiInServer with bad token -> should fail
    IpcCreateMidiInServer(g_callbackStub->AsObject(), clientObj, clientId, true);

    // 对已有 client 也试 bad token
    if (EnsureActiveClient(fdp)) {
        auto &client = g_activeClients[0];
        MessageParcel data;
        MessageParcel reply;
        data.WriteInterfaceToken(u"OHOS.MIDI.WRONG");
        data.WriteInt64(0);
        CallIpcMidiInServer(client.clientObj, static_cast<uint32_t>(IIpcMidiInServerIpcCode::COMMAND_GET_DEVICE_PORTS),
                            data, reply);
    }
}

// ====== Mode 2: Invalid clientId ======
// 先销毁合法 client，再复用 stale IpcMidiInServerStub 覆盖 invalid-client 分支。
void ModeInvalidClientId(FuzzedDataProvider &fdp)
{
    if (!EnsureActiveClient(fdp)) {
        return;
    }
    auto client = g_activeClients[0];

    // 先通过 IPC destroy，保留 stale clientObj，再复用它覆盖 invalid-client 分支。
    {
        MessageParcel data;
        MessageParcel reply;
        data.WriteInterfaceToken(u"OHOS.MIDI.IIpcMidiInServer");
        CallIpcMidiInServer(client.clientObj,
                            static_cast<uint32_t>(IIpcMidiInServerIpcCode::COMMAND_DESTROY_MIDI_CLIENT), data, reply);
    }
    g_activeClients.clear();

    MessageParcel data;
    MessageParcel reply;
    data.WriteInterfaceToken(u"OHOS.MIDI.IIpcMidiInServer");
    data.WriteInt64(fdp.ConsumeIntegral<int64_t>());
    CallIpcMidiInServer(client.clientObj, static_cast<uint32_t>(IIpcMidiInServerIpcCode::COMMAND_OPEN_DEVICE), data,
                        reply);
}

// ====== Mode 3: Invalid deviceId/portIndex ======
void ModeInvalidDeviceId(FuzzedDataProvider &fdp)
{
    if (!EnsureActiveClient(fdp)) {
        return;
    }
    auto &client = g_activeClients[0];
    int64_t invalidDeviceId = fdp.ConsumeIntegral<int64_t>();
    uint32_t invalidPortIndex = fdp.ConsumeIntegral<uint32_t>();

    // OpenDevice invalid
    {
        MessageParcel data;
        MessageParcel reply;
        data.WriteInterfaceToken(u"OHOS.MIDI.IIpcMidiInServer");
        data.WriteInt64(invalidDeviceId);
        CallIpcMidiInServer(client.clientObj, static_cast<uint32_t>(IIpcMidiInServerIpcCode::COMMAND_OPEN_DEVICE),
                            data, reply);
    }
    // OpenInputPort invalid
    {
        MessageParcel data;
        MessageParcel reply;
        data.WriteInterfaceToken(u"OHOS.MIDI.IIpcMidiInServer");
        data.WriteInt64(invalidDeviceId);
        data.WriteUint32(invalidPortIndex);
        CallIpcMidiInServer(client.clientObj, static_cast<uint32_t>(IIpcMidiInServerIpcCode::COMMAND_OPEN_INPUT_PORT),
                            data, reply);
    }
    // OpenOutputPort invalid
    {
        MessageParcel data;
        MessageParcel reply;
        data.WriteInterfaceToken(u"OHOS.MIDI.IIpcMidiInServer");
        data.WriteInt64(invalidDeviceId);
        data.WriteUint32(invalidPortIndex);
        CallIpcMidiInServer(client.clientObj, static_cast<uint32_t>(IIpcMidiInServerIpcCode::COMMAND_OPEN_OUTPUT_PORT),
                            data, reply);
    }
    // CloseDevice invalid
    {
        MessageParcel data;
        MessageParcel reply;
        data.WriteInterfaceToken(u"OHOS.MIDI.IIpcMidiInServer");
        data.WriteInt64(invalidDeviceId);
        CallIpcMidiInServer(client.clientObj, static_cast<uint32_t>(IIpcMidiInServerIpcCode::COMMAND_CLOSE_DEVICE),
                            data, reply);
    }
    // GetDevicePorts invalid
    {
        MessageParcel data;
        MessageParcel reply;
        data.WriteInterfaceToken(u"OHOS.MIDI.IIpcMidiInServer");
        data.WriteInt64(invalidDeviceId);
        CallIpcMidiInServer(client.clientObj,
                            static_cast<uint32_t>(IIpcMidiInServerIpcCode::COMMAND_GET_DEVICE_PORTS), data, reply);
    }
}

void CallDeviceCommand(const ClientContext &client, uint32_t code, int64_t deviceId)
{
    MessageParcel data;
    MessageParcel reply;
    data.WriteInterfaceToken(u"OHOS.MIDI.IIpcMidiInServer");
    data.WriteInt64(deviceId);
    CallIpcMidiInServer(client.clientObj, code, data, reply);
}

void CallPortCommand(const ClientContext &client, uint32_t code, int64_t deviceId, uint32_t portIndex)
{
    MessageParcel data;
    MessageParcel reply;
    data.WriteInterfaceToken(u"OHOS.MIDI.IIpcMidiInServer");
    data.WriteInt64(deviceId);
    data.WriteUint32(portIndex);
    CallIpcMidiInServer(client.clientObj, code, data, reply);
}

void RepeatDeviceCommand(const ClientContext &client, uint32_t code, int64_t deviceId)
{
    for (int i = 0; i < REPEAT_COMMAND_COUNT; i++) {
        CallDeviceCommand(client, code, deviceId);
    }
}

void RepeatPortCommand(const ClientContext &client, uint32_t code, int64_t deviceId, uint32_t portIndex)
{
    for (int i = 0; i < REPEAT_COMMAND_COUNT; i++) {
        CallPortCommand(client, code, deviceId, portIndex);
    }
}

// ====== Mode 4: Repeat open/close ======
void ModeRepeatOpenClose(FuzzedDataProvider &fdp)
{
    if (!EnsureActiveClient(fdp) || g_activeDevices.empty()) {
        return;
    }
    auto &client = g_activeClients[0];
    int64_t deviceId = g_activeDevices[0];

    RepeatDeviceCommand(client, static_cast<uint32_t>(IIpcMidiInServerIpcCode::COMMAND_OPEN_DEVICE), deviceId);
    RepeatPortCommand(client, static_cast<uint32_t>(IIpcMidiInServerIpcCode::COMMAND_OPEN_INPUT_PORT), deviceId, 0);
    RepeatDeviceCommand(client, static_cast<uint32_t>(IIpcMidiInServerIpcCode::COMMAND_CLOSE_DEVICE), deviceId);
    CallPortCommand(client, static_cast<uint32_t>(IIpcMidiInServerIpcCode::COMMAND_FLUSH_OUTPUT_PORT), deviceId, 0);
    CallPortCommand(client, static_cast<uint32_t>(IIpcMidiInServerIpcCode::COMMAND_CLOSE_INPUT_PORT), deviceId, 0);
    CallPortCommand(client, static_cast<uint32_t>(IIpcMidiInServerIpcCode::COMMAND_CLOSE_OUTPUT_PORT), deviceId, 1);
}

// ====== Mode 5: Destroy then operate ======
void ModeDestroyThenOperate(FuzzedDataProvider &fdp)
{
    if (!EnsureActiveClient(fdp)) {
        return;
    }
    auto client = g_activeClients[0];  // copy
    // Destroy
    {
        MessageParcel data;
        MessageParcel reply;
        data.WriteInterfaceToken(u"OHOS.MIDI.IIpcMidiInServer");
        CallIpcMidiInServer(client.clientObj,
                            static_cast<uint32_t>(IIpcMidiInServerIpcCode::COMMAND_DESTROY_MIDI_CLIENT), data, reply);
    }
    g_activeClients.clear();
    // Destroy again (already destroyed)
    {
        MessageParcel data;
        MessageParcel reply;
        data.WriteInterfaceToken(u"OHOS.MIDI.IIpcMidiInServer");
        CallIpcMidiInServer(client.clientObj,
                            static_cast<uint32_t>(IIpcMidiInServerIpcCode::COMMAND_DESTROY_MIDI_CLIENT), data, reply);
    }
    // OpenDevice after destroy
    if (!g_activeDevices.empty()) {
        MessageParcel data;
        MessageParcel reply;
        data.WriteInterfaceToken(u"OHOS.MIDI.IIpcMidiInServer");
        data.WriteInt64(g_activeDevices[0]);
        CallIpcMidiInServer(client.clientObj, static_cast<uint32_t>(IIpcMidiInServerIpcCode::COMMAND_OPEN_DEVICE),
                            data, reply);
    }
}

// ====== Mode 6: GetDevices / GetDevicePorts ======
void ModeQueryDevices(FuzzedDataProvider &fdp)
{
    if (!EnsureActiveClient(fdp)) {
        return;
    }
    auto &client = g_activeClients[0];
    // GetDevices
    {
        MessageParcel data;
        MessageParcel reply;
        data.WriteInterfaceToken(u"OHOS.MIDI.IIpcMidiInServer");
        CallIpcMidiInServer(client.clientObj, static_cast<uint32_t>(IIpcMidiInServerIpcCode::COMMAND_GET_DEVICES),
                            data, reply);
    }
    // GetDevicePorts valid + invalid
    if (!g_activeDevices.empty()) {
        MessageParcel data;
        MessageParcel reply;
        data.WriteInterfaceToken(u"OHOS.MIDI.IIpcMidiInServer");
        data.WriteInt64(g_activeDevices[0]);
        CallIpcMidiInServer(client.clientObj,
                            static_cast<uint32_t>(IIpcMidiInServerIpcCode::COMMAND_GET_DEVICE_PORTS), data, reply);
    }
    {
        MessageParcel data;
        MessageParcel reply;
        data.WriteInterfaceToken(u"OHOS.MIDI.IIpcMidiInServer");
        data.WriteInt64(fdp.ConsumeIntegral<int64_t>());
        CallIpcMidiInServer(client.clientObj,
                            static_cast<uint32_t>(IIpcMidiInServerIpcCode::COMMAND_GET_DEVICE_PORTS), data, reply);
    }
    // 直接调 controller 补充覆盖：NotifyError / Init / IsBluetoothDevice
    g_controller->NotifyError(fdp.ConsumeIntegral<int32_t>());
    g_controller->Init();
    if (!g_activeDevices.empty()) {
        g_controller->IsBluetoothDevice(g_activeDevices[0]);
    }
    // 补 midi_server.cpp 覆盖：OnDump + Dump + OnStop（后重设 controller_，避免破坏后续测试）
    g_midiServer->OnDump();
    std::vector<std::u16string> dumpArgs;
    int32_t devNullFd = open("/dev/null", O_WRONLY);
    if (devNullFd >= 0) {
        g_midiServer->Dump(devNullFd, dumpArgs);
        close(devNullFd);
    }
    // Fuzzer-only: cover the current OnStop controller_ reset path, then restore it for later scenarios.
    // Revisit this block if OnStop starts doing broader service teardown.
    g_midiServer->OnStop();
    g_midiServer->controller_ = g_controller;  // 重设，避免后续 controller_ 为空
}

// ====== Mode 7: OpenBleDevice 专项（含 mock callback 同步触发） ======
void ModeOpenBleDevice(FuzzedDataProvider &fdp)
{
    if (!EnsureActiveClient(fdp)) {
        return;
    }
    auto &client = g_activeClients[0];
    std::string address = fdp.ConsumeRandomLengthString(32);
    if (address.empty()) {
        address = "AA:BB:CC:DD:EE:FF";
    }

    // 准备 IMidiDeviceOpenCallback stub
    if (g_deviceOpenCallbackStub == nullptr) {
        g_deviceOpenCallbackStub = new MidiDeviceOpenCallbackFuzzer();
    }

    // 第一次 OpenBleDevice（发起 BLE 连接）
    {
        MessageParcel data;
        MessageParcel reply;
        data.WriteInterfaceToken(u"OHOS.MIDI.IIpcMidiInServer");
        data.WriteString16(Str8ToStr16(address));
        data.WriteRemoteObject(g_deviceOpenCallbackStub->AsObject());
        CallIpcMidiInServer(client.clientObj,
                            static_cast<uint32_t>(IIpcMidiInServerIpcCode::COMMAND_OPEN_BLE_DEVICE), data, reply);
    }

    // 直接调 controller OpenBleDevice 绕过 MidiInServer 蓝牙权限检查
    // （覆盖 controller OpenBleDevice/TryAttachToActiveBleDevice/EnqueueBleConnectionRequest）
    {
        g_controller->OpenBleDevice(client.clientId, address, g_deviceOpenCallbackStub->AsObject());
    }

    // 主动触发 BLE callback（success/fail 切换）
    bool triggerSuccess = fdp.ConsumeBool();
    DrainBleCallback(triggerSuccess);

    // 第二次 OpenBleDevice 同地址（走 TryAttachToActiveBleDevice 路径，如果第一次成功）
    if (triggerSuccess) {
        // 直接调 controller，绕过权限
        g_controller->OpenBleDevice(client.clientId, address, g_deviceOpenCallbackStub->AsObject());
    }

    // 试 CloseDevice 对 BLE device（用 invalid id，覆盖 close 异常分支）
    {
        MessageParcel data;
        MessageParcel reply;
        data.WriteInterfaceToken(u"OHOS.MIDI.IIpcMidiInServer");
        data.WriteInt64(fdp.ConsumeIntegral<int64_t>());
        CallIpcMidiInServer(client.clientObj, static_cast<uint32_t>(IIpcMidiInServerIpcCode::COMMAND_CLOSE_DEVICE),
                            data, reply);
    }
}

// ====== Init ======

void MidiIpcStubInit()
{
    g_controller = MidiServiceController::GetInstance();
    RebuildState();

    // 构造 MidiServer，不调 OnStart，直接注入 controller_
    g_midiServer = sptr<MidiServer>::MakeSptr(TEST_SA_ID, true);
    // -fno-access-control 让我们访问 private controller_
    g_midiServer->controller_ = g_controller;

    g_callbackStub = new MidiCallbackFuzzer();
    g_deviceOpenCallbackStub = new MidiDeviceOpenCallbackFuzzer();
}

void MidiIpcStubFuzz(const uint8_t *data, size_t size)
{
    if (g_midiServer == nullptr) {
        return;
    }
    if (size < 1) {
        return;
    }
    FuzzedDataProvider fdp(data, size);
    uint8_t mode = fdp.ConsumeIntegral<uint8_t>() % FUZZ_MODE_COUNT;

    // 每个 input 前重建状态
    RebuildState();

    switch (mode) {
        case MODE_HAPPY_PATH:
            ModeHappyPath(fdp);
            break;
        case MODE_BAD_TOKEN:
            ModeBadToken(fdp);
            break;
        case MODE_INVALID_CLIENT_ID:
            ModeInvalidClientId(fdp);
            break;
        case MODE_INVALID_DEVICE_ID:
            ModeInvalidDeviceId(fdp);
            break;
        case MODE_REPEAT_OPEN_CLOSE:
            ModeRepeatOpenClose(fdp);
            break;
        case MODE_DESTROY_THEN_OPERATE:
            ModeDestroyThenOperate(fdp);
            break;
        case MODE_QUERY_DEVICES:
            ModeQueryDevices(fdp);
            break;
        case MODE_OPEN_BLE_DEVICE:
            ModeOpenBleDevice(fdp);
            break;
        default:
            break;
    }

    // 清理：drain 任何残留 BLE callback
    DrainBleCallback(true);
    for (auto &c : g_activeClients) {
        if (g_controller != nullptr) {
            g_controller->DestroyMidiClient(c.clientId);
        }
    }
    g_activeClients.clear();
    if (g_controller != nullptr) {
        g_controller->ClearStateForTest();
    }
}

bool GrantBluetoothPermissionForFuzzer()
{
    static const char *perms[] = { ACCESS_BLUETOOTH_PERMISSION };
    NativeTokenInfoParams infoInstance = {
        .dcapsNum = 0,
        .permsNum = 1,
        .aclsNum = 0,
        .dcaps = nullptr,
        .perms = perms,
        .acls = nullptr,
        .processName = "MidiIpcStubFuzzTest",
        .aplStr = "system_core",
        .uid = 0,
    };
    uint64_t tokenId = GetAccessTokenId(&infoInstance);
    if (tokenId == 0) {
        return false;
    }
    if (SetSelfTokenID(tokenId) < 0) {
        return false;
    }
    OHOS::Security::AccessToken::AccessTokenKit::ReloadNativeTokenInfo();
    return true;
}

} // namespace MIDI
} // namespace OHOS

extern "C" int LLVMFuzzerInitialize(int *argc, char ***argv)
{
    (void)argc;
    (void)argv;
    if (!OHOS::MIDI::GrantBluetoothPermissionForFuzzer()) {
        return -1;
    }
    OHOS::MIDI::MidiIpcStubInit();
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    OHOS::MIDI::MidiIpcStubFuzz(data, size);
    return 0;
}
