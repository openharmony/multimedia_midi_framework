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
#define LOG_TAG "MidiServiceControllerTest"
#endif

#include <algorithm>
#include <cstdint>
#include <fuzzer/FuzzedDataProvider.h>
#include <map>
#include <securec.h>
#include <string>
#include <vector>
#include <memory>
#include <unordered_set>
#include <utility>

#include "accesstoken_kit.h"
#include "iremote_object.h"
#include "midi_core_coverage_scenarios.h"
#include "midi_info.h"
#include "midi_service_controller.h"
#include "midi_device_driver.h"
#include "midi_shared_ring.h"
#include "nativetoken_kit.h"
#include "token_setproc.h"
#include "midi_log.h"
#include "midi_callback_stub.h"
#include "imidi_device_open_callback.h"
#include "ipc_midi_in_server_stub.h"
#include "iremote_stub.h"

namespace OHOS {
namespace MIDI {
using namespace std;
namespace {
constexpr int32_t RING_BUFFER_DEFAULT_SIZE = 2048;
constexpr uint32_t MAX_FUZZ_PORTS = 2;

constexpr uint64_t TEST_TOKEN_ID = 718336240uLL | (1uLL << 32);
constexpr int64_t TEST_CLIENT_ID1 = 1001;
constexpr int64_t TEST_CLIENT_ID2 = 1005;
constexpr int64_t TEST_BLE_DRIVER_ID = 2001;
constexpr size_t REQUIRED_CLIENT_COUNT = 2;
constexpr const char *TEST_BLE_ADDRESS = "AA:BB:CC:DD:EE:01";
} // namespace

std::shared_ptr<MidiServiceController> midiServiceController_;

struct ClientContext {
    uint32_t clientId;
    sptr<IRemoteObject> clientObj;
    std::shared_ptr<MidiSharedRing> buffer;
};

std::vector<ClientContext> activeClients_;
std::vector<int64_t> activeDevices_;
class MockMidiDeviceDriver;
MockMidiDeviceDriver *g_rawMockDriver = nullptr;

class MockMidiDeviceDriver : public MidiDeviceDriver {
public:
    std::vector<DeviceInformation> GetRegisteredDevices() override
    {
        return mockDevices_;
    }

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
        pendingBleAddress_ = std::move(deviceAddr);
        pendingBleCallback_ = std::move(deviceCallback);
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

    void CompletePendingBleConnection(bool connected)
    {
        CHECK_AND_RETURN(pendingBleCallback_ != nullptr);
        DeviceInformation info;
        info.midiDeviceInfo.driverDeviceId = TEST_BLE_DRIVER_ID;
        info.midiDeviceInfo.deviceType = DeviceType::DEVICE_TYPE_BLE;
        info.midiDeviceInfo.deviceName = pendingBleAddress_;
        info.midiDeviceInfo.productId = 0x4321;
        info.midiDeviceInfo.vendorId = 0x8765;
        info.midiDeviceInfo.transportProtocol = TransportProtocol::PROTOCOL_1_0;

        MidiPortInfo inputPort;
        inputPort.portId = 0;
        inputPort.direction = PortDirection::PORT_DIRECTION_INPUT;
        inputPort.name = "BLE Input Port";
        info.portInfos.push_back(inputPort);

        MidiPortInfo outputPort;
        outputPort.portId = 1;
        outputPort.direction = PortDirection::PORT_DIRECTION_OUTPUT;
        outputPort.name = "BLE Output Port";
        info.portInfos.push_back(outputPort);

        auto callback = std::move(pendingBleCallback_);
        pendingBleCallback_ = nullptr;
        callback(connected, info);
    }

private:
    std::vector<DeviceInformation> mockDevices_;
    std::unordered_set<int64_t> openedDevices_;
    std::unordered_set<uint64_t> openedInputPorts_;
    std::unordered_set<uint64_t> openedOutputPorts_;
    std::string pendingBleAddress_;
    BleDriverCallback pendingBleCallback_;
};

class MidiServiceCallbackFuzzer : public MidiCallbackStub {
public:
    int32_t NotifyDeviceChange(int32_t change, const MidiDeviceInfo &deviceInfo) override
    {
        return 0;
    };
    int32_t NotifyError(int32_t code) override { return 0; };

    bool AddDeathRecipient(const sptr<DeathRecipient> &recipient) override
    {
        CHECK_AND_RETURN_RET(recipient != nullptr, false);
        deathRecipients_.push_back(recipient);
        return true;
    }

    bool RemoveDeathRecipient(const sptr<DeathRecipient> &recipient) override
    {
        auto it = std::find(deathRecipients_.begin(), deathRecipients_.end(), recipient);
        CHECK_AND_RETURN_RET(it != deathRecipients_.end(), false);
        deathRecipients_.erase(it);
        return true;
    }

private:
    std::vector<sptr<DeathRecipient>> deathRecipients_;
};

class MidiDeviceOpenCallbackFuzzer : public IRemoteStub<IMidiDeviceOpenCallback> {
public:
    int32_t NotifyDeviceOpened(bool opened, const MidiDeviceInfo &deviceInfo) override
    {
        (void)opened;
        (void)deviceInfo;
        return 0;
    }

    bool AddDeathRecipient(const sptr<DeathRecipient> &recipient) override
    {
        CHECK_AND_RETURN_RET(recipient != nullptr, false);
        deathRecipients_.push_back(recipient);
        return true;
    }

    bool RemoveDeathRecipient(const sptr<DeathRecipient> &recipient) override
    {
        auto it = std::find(deathRecipients_.begin(), deathRecipients_.end(), recipient);
        CHECK_AND_RETURN_RET(it != deathRecipients_.end(), false);
        deathRecipients_.erase(it);
        return true;
    }

private:
    std::vector<sptr<DeathRecipient>> deathRecipients_;
};

void CreateMidiInServer(FuzzedDataProvider &fdp)
{
    sptr<MidiServiceCallbackFuzzer> callback = new MidiServiceCallbackFuzzer();
    sptr<IRemoteObject> callbackObject = fdp.ConsumeBool() ? nullptr : callback->AsObject();
    sptr<IRemoteObject> clientObj;
    uint32_t clientId = 0;
    int32_t ret = midiServiceController_->CreateMidiInServer(callbackObject, clientObj, clientId);
    if (ret == OH_MIDI_STATUS_OK) {
        ClientContext ctx;
        ctx.clientId = clientId;
        ctx.clientObj = clientObj;
        ctx.buffer = std::make_shared<MidiSharedRing>(RING_BUFFER_DEFAULT_SIZE);
        activeClients_.push_back(ctx);
    }
}

void RefreshActiveDevices()
{
    CHECK_AND_RETURN(midiServiceController_ != nullptr);
    activeDevices_.clear();
    midiServiceController_->GetDeviceManagerForTest()->UpdateDevices();
    auto devices = midiServiceController_->GetDeviceManagerForTest()->GetDevices();
    for (const auto &dev : devices) {
        activeDevices_.push_back(dev.midiDeviceInfo.deviceId);
    }
}

void DestroyAllClients()
{
    CHECK_AND_RETURN(midiServiceController_ != nullptr);
    for (const auto &client : activeClients_) {
        midiServiceController_->DestroyMidiClient(client.clientId);
    }
    activeClients_.clear();
    midiServiceController_->CancelUnloadTask();
}

void ResetControllerStateForFuzzer()
{
    DestroyAllClients();
    midiServiceController_->ClearStateForTest();
    RefreshActiveDevices();
}

bool EnsureClientCount(size_t count)
{
    while (activeClients_.size() < count) {
        FuzzedDataProvider emptyProvider(nullptr, 0);
        size_t before = activeClients_.size();
        CreateMidiInServer(emptyProvider);
        CHECK_AND_RETURN_RET(activeClients_.size() > before, false);
    }
    return true;
}

bool EnsureDevice()
{
    if (activeDevices_.empty()) {
        RefreshActiveDevices();
    }
    return !activeDevices_.empty();
}

int32_t DispatchIpcMidiInServer(const sptr<IRemoteObject> &clientObj, uint32_t code, MessageParcel &data,
    MessageParcel &reply)
{
    MessageOption option;
    auto *stub = reinterpret_cast<IpcMidiInServerStub *>(clientObj.GetRefPtr());
    CHECK_AND_RETURN_RET(stub != nullptr, -1);
    return stub->OnRemoteRequest(code, data, reply, option);
}

void WriteIpcToken(MessageParcel &data)
{
    data.WriteInterfaceToken(u"OHOS.MIDI.IIpcMidiInServer");
}

void DispatchMidiInServerSimpleCommand(const ClientContext &client, uint32_t code)
{
    MessageParcel data;
    MessageParcel reply;
    WriteIpcToken(data);
    DispatchIpcMidiInServer(client.clientObj, code, data, reply);
}

void DispatchMidiInServerDeviceCommand(const ClientContext &client, uint32_t code, int64_t deviceId)
{
    MessageParcel data;
    MessageParcel reply;
    WriteIpcToken(data);
    data.WriteInt64(deviceId);
    DispatchIpcMidiInServer(client.clientObj, code, data, reply);
}

void DispatchMidiInServerPortCommand(const ClientContext &client, uint32_t code, int64_t deviceId, uint32_t portIndex)
{
    MessageParcel data;
    MessageParcel reply;
    WriteIpcToken(data);
    data.WriteInt64(deviceId);
    data.WriteUint32(portIndex);
    DispatchIpcMidiInServer(client.clientObj, code, data, reply);
}

void DispatchMidiInServerOpenBleDevice(const ClientContext &client)
{
    MessageParcel data;
    MessageParcel reply;
    WriteIpcToken(data);
    data.WriteString16(Str8ToStr16(TEST_BLE_ADDRESS));
    sptr<MidiDeviceOpenCallbackFuzzer> callback = new MidiDeviceOpenCallbackFuzzer();
    data.WriteRemoteObject(callback->AsObject());
    DispatchIpcMidiInServer(client.clientObj,
        static_cast<uint32_t>(IIpcMidiInServerIpcCode::COMMAND_OPEN_BLE_DEVICE), data, reply);
}

void ExerciseMidiInServerIpcPath()
{
    CHECK_AND_RETURN(EnsureClientCount(1) && EnsureDevice());
    auto &client = activeClients_[0];
    int64_t deviceId = activeDevices_[0];

    DispatchMidiInServerSimpleCommand(client, static_cast<uint32_t>(IIpcMidiInServerIpcCode::COMMAND_GET_DEVICES));
    DispatchMidiInServerDeviceCommand(client,
        static_cast<uint32_t>(IIpcMidiInServerIpcCode::COMMAND_GET_DEVICE_PORTS), deviceId);
    DispatchMidiInServerDeviceCommand(client,
        static_cast<uint32_t>(IIpcMidiInServerIpcCode::COMMAND_OPEN_DEVICE), deviceId);
    DispatchMidiInServerPortCommand(client,
        static_cast<uint32_t>(IIpcMidiInServerIpcCode::COMMAND_OPEN_INPUT_PORT), deviceId, 0);
    DispatchMidiInServerPortCommand(client,
        static_cast<uint32_t>(IIpcMidiInServerIpcCode::COMMAND_OPEN_OUTPUT_PORT), deviceId, 1);
    DispatchMidiInServerPortCommand(client,
        static_cast<uint32_t>(IIpcMidiInServerIpcCode::COMMAND_FLUSH_OUTPUT_PORT), deviceId, 1);
    DispatchMidiInServerOpenBleDevice(client);
    DispatchMidiInServerPortCommand(client,
        static_cast<uint32_t>(IIpcMidiInServerIpcCode::COMMAND_CLOSE_OUTPUT_PORT), deviceId, 1);
    DispatchMidiInServerPortCommand(client,
        static_cast<uint32_t>(IIpcMidiInServerIpcCode::COMMAND_CLOSE_INPUT_PORT), deviceId, 0);
    DispatchMidiInServerDeviceCommand(client,
        static_cast<uint32_t>(IIpcMidiInServerIpcCode::COMMAND_CLOSE_DEVICE), deviceId);
}

void ExerciseInvalidControllerPaths(FuzzedDataProvider &fdp)
{
    CHECK_AND_RETURN(EnsureClientCount(1) && EnsureDevice());
    auto &client = activeClients_[0];
    int64_t deviceId = activeDevices_[0];
    uint32_t invalidClientId = fdp.ConsumeIntegral<uint32_t>();
    if (invalidClientId == client.clientId) {
        invalidClientId++;
    }
    int64_t invalidDeviceId = fdp.ConsumeIntegral<int64_t>();
    if (invalidDeviceId == deviceId) {
        invalidDeviceId++;
    }

    midiServiceController_->OpenDevice(invalidClientId, deviceId);
    midiServiceController_->OpenDevice(client.clientId, invalidDeviceId);
    midiServiceController_->GetDevice(invalidDeviceId);
    std::vector<MidiPortInfo> ports;
    midiServiceController_->GetDevicePorts(invalidDeviceId, ports);
    midiServiceController_->OpenInputPort(client.clientId, client.buffer, invalidDeviceId, MAX_FUZZ_PORTS);
    midiServiceController_->OpenOutputPort(client.clientId, client.buffer, invalidDeviceId, MAX_FUZZ_PORTS);
    midiServiceController_->FlushOutputPort(client.clientId, invalidDeviceId, MAX_FUZZ_PORTS);
    midiServiceController_->CloseInputPort(client.clientId, invalidDeviceId, MAX_FUZZ_PORTS);
    midiServiceController_->CloseOutputPort(client.clientId, invalidDeviceId, MAX_FUZZ_PORTS);
    midiServiceController_->CloseDevice(client.clientId, invalidDeviceId);
    midiServiceController_->DestroyMidiClient(invalidClientId);
    midiServiceController_->CancelUnloadTask();
}

void ExerciseSharedPortLifecycle()
{
    CHECK_AND_RETURN(EnsureClientCount(REQUIRED_CLIENT_COUNT) && EnsureDevice());
    auto &client1 = activeClients_[0];
    auto &client2 = activeClients_[1];
    int64_t deviceId = activeDevices_[0];

    midiServiceController_->OpenDevice(client1.clientId, deviceId);
    midiServiceController_->OpenDevice(client1.clientId, deviceId);
    midiServiceController_->OpenDevice(client2.clientId, deviceId);

    midiServiceController_->OpenInputPort(client1.clientId, client1.buffer, deviceId, 0);
    midiServiceController_->OpenInputPort(client1.clientId, client1.buffer, deviceId, 0);
    midiServiceController_->OpenInputPort(client2.clientId, client2.buffer, deviceId, 0);
    midiServiceController_->OpenOutputPort(client1.clientId, client1.buffer, deviceId, 1);
    midiServiceController_->OpenOutputPort(client1.clientId, client1.buffer, deviceId, 1);
    midiServiceController_->OpenOutputPort(client2.clientId, client2.buffer, deviceId, 1);
    midiServiceController_->FlushOutputPort(client1.clientId, deviceId, 1);

    std::string dumpString;
    midiServiceController_->DumpClientInfo(dumpString);
    midiServiceController_->DumpDeviceOpenStatus(dumpString, deviceId);
    midiServiceController_->DumpDeviceOpenStatus(dumpString, -1);
    midiServiceController_->DumpPortMapping(dumpString);
    midiServiceController_->DumpStatistics(dumpString);
    midiServiceController_->NotifyError(OH_MIDI_STATUS_SYSTEM_ERROR);

    midiServiceController_->CloseOutputPort(client1.clientId, deviceId, 1);
    midiServiceController_->CloseOutputPort(client2.clientId, deviceId, 1);
    midiServiceController_->CloseInputPort(client1.clientId, deviceId, 0);
    midiServiceController_->CloseInputPort(client2.clientId, deviceId, 0);
    midiServiceController_->CloseDevice(client1.clientId, deviceId);
    midiServiceController_->CloseDevice(client2.clientId, deviceId);
}

void ExerciseDeviceRemovalCleanup()
{
    CHECK_AND_RETURN(EnsureClientCount(REQUIRED_CLIENT_COUNT) && EnsureDevice());
    auto &client1 = activeClients_[0];
    auto &client2 = activeClients_[1];
    int64_t deviceId = activeDevices_[0];

    midiServiceController_->OpenDevice(client1.clientId, deviceId);
    midiServiceController_->OpenDevice(client2.clientId, deviceId);
    midiServiceController_->OpenInputPort(client1.clientId, client1.buffer, deviceId, 0);
    midiServiceController_->OpenInputPort(client2.clientId, client2.buffer, deviceId, 0);
    midiServiceController_->OpenOutputPort(client1.clientId, client1.buffer, deviceId, 1);
    midiServiceController_->OpenOutputPort(client2.clientId, client2.buffer, deviceId, 1);

    DeviceInformation deviceInfo;
    deviceInfo.midiDeviceInfo.deviceId = deviceId;
    deviceInfo.midiDeviceInfo.deviceType = DeviceType::DEVICE_TYPE_USB;
    deviceInfo.midiDeviceInfo.deviceName = "Removed Device";
    midiServiceController_->NotifyDeviceChange(DeviceChangeType::ADD, deviceInfo);
    midiServiceController_->NotifyDeviceChange(DeviceChangeType::REMOVED, deviceInfo);
}

void ExerciseBleLifecycle()
{
    CHECK_AND_RETURN(EnsureClientCount(REQUIRED_CLIENT_COUNT));
    CHECK_AND_RETURN(g_rawMockDriver != nullptr);
    auto &client1 = activeClients_[0];
    auto &client2 = activeClients_[1];
    sptr<MidiDeviceOpenCallbackFuzzer> callback = new MidiDeviceOpenCallbackFuzzer();

    midiServiceController_->OpenBleDevice(client1.clientId, TEST_BLE_ADDRESS, callback->AsObject());
    midiServiceController_->OpenBleDevice(client2.clientId, TEST_BLE_ADDRESS, callback->AsObject());
    g_rawMockDriver->CompletePendingBleConnection(true);
    midiServiceController_->OpenBleDevice(client2.clientId, TEST_BLE_ADDRESS, callback->AsObject());

    auto devices = midiServiceController_->GetDevices();
    for (const auto &device : devices) {
        if (device.deviceType == DeviceType::DEVICE_TYPE_BLE) {
            std::vector<MidiPortInfo> ports;
            midiServiceController_->GetDevicePorts(device.deviceId, ports);
            midiServiceController_->CloseDevice(client1.clientId, device.deviceId);
            midiServiceController_->CloseDevice(client2.clientId, device.deviceId);
            break;
        }
    }

    constexpr const char *failedAddress = "AA:BB:CC:DD:EE:02";
    midiServiceController_->OpenBleDevice(client1.clientId, failedAddress, callback->AsObject());
    g_rawMockDriver->CompletePendingBleConnection(false);
}

void ExerciseDeterministicCoverage(FuzzedDataProvider &fdp)
{
    ResetControllerStateForFuzzer();
    ExerciseMidiInServerIpcPath();

    ResetControllerStateForFuzzer();
    ExerciseInvalidControllerPaths(fdp);

    ResetControllerStateForFuzzer();
    ExerciseSharedPortLifecycle();

    ResetControllerStateForFuzzer();
    ExerciseDeviceRemovalCleanup();

    ResetControllerStateForFuzzer();
    ExerciseBleLifecycle();
}

void GetDevicePorts(FuzzedDataProvider &fdp)
{
    CHECK_AND_RETURN(!activeDevices_.empty());
    size_t deviceIdx = fdp.ConsumeIntegralInRange<size_t>(0, activeDevices_.size() - 1);
    int64_t deviceId = activeDevices_[deviceIdx];
    std::vector<MidiPortInfo> portInfos;
    midiServiceController_->GetDevicePorts(deviceId, portInfos);
}

void OpenDevice(FuzzedDataProvider &fdp)
{
    CHECK_AND_RETURN(!(activeClients_.empty() || activeDevices_.empty()));
    size_t clientIdx = fdp.ConsumeIntegralInRange<size_t>(0, activeClients_.size() - 1);
    auto &client = activeClients_[clientIdx];
    size_t deviceIdx = fdp.ConsumeIntegralInRange<size_t>(0, activeDevices_.size() - 1);
    int64_t deviceId = activeDevices_[deviceIdx];
    midiServiceController_->OpenDevice(client.clientId, deviceId);
}

void OpenInputPort(FuzzedDataProvider &fdp)
{
    CHECK_AND_RETURN(!(activeClients_.empty() || activeDevices_.empty()));
    size_t clientIdx = fdp.ConsumeIntegralInRange<size_t>(0, activeClients_.size() - 1);
    auto &client = activeClients_[clientIdx];
    size_t deviceIdx = fdp.ConsumeIntegralInRange<size_t>(0, activeDevices_.size() - 1);
    int64_t deviceId = activeDevices_[deviceIdx];
    uint32_t portIndex = fdp.ConsumeIntegralInRange<uint32_t>(0, MAX_FUZZ_PORTS - 1);
    midiServiceController_->OpenInputPort(client.clientId, client.buffer, deviceId, portIndex);
}

void OpenOutputPort(FuzzedDataProvider &fdp)
{
    CHECK_AND_RETURN(!(activeClients_.empty() || activeDevices_.empty()));
    size_t clientIdx = fdp.ConsumeIntegralInRange<size_t>(0, activeClients_.size() - 1);
    auto &client = activeClients_[clientIdx];
    size_t deviceIdx = fdp.ConsumeIntegralInRange<size_t>(0, activeDevices_.size() - 1);
    int64_t deviceId = activeDevices_[deviceIdx];
    uint32_t portIndex = fdp.ConsumeIntegralInRange<uint32_t>(0, MAX_FUZZ_PORTS - 1);
    midiServiceController_->OpenOutputPort(client.clientId, client.buffer, deviceId, portIndex);
}

void CloseDevice(FuzzedDataProvider &fdp)
{
    CHECK_AND_RETURN(!(activeClients_.empty() || activeDevices_.empty()));
    size_t clientIdx = fdp.ConsumeIntegralInRange<size_t>(0, activeClients_.size() - 1);
    auto &client = activeClients_[clientIdx];
    size_t deviceIdx = fdp.ConsumeIntegralInRange<size_t>(0, activeDevices_.size() - 1);
    int64_t deviceId = activeDevices_[deviceIdx];
    midiServiceController_->CloseDevice(client.clientId, deviceId);
}

void CloseInputPort(FuzzedDataProvider &fdp)
{
    CHECK_AND_RETURN(!(activeClients_.empty() || activeDevices_.empty()));
    size_t clientIdx = fdp.ConsumeIntegralInRange<size_t>(0, activeClients_.size() - 1);
    auto &client = activeClients_[clientIdx];
    size_t deviceIdx = fdp.ConsumeIntegralInRange<size_t>(0, activeDevices_.size() - 1);
    int64_t deviceId = activeDevices_[deviceIdx];
    uint32_t portIndex = fdp.ConsumeIntegralInRange<uint32_t>(0, MAX_FUZZ_PORTS - 1);
    midiServiceController_->CloseInputPort(client.clientId, deviceId, portIndex);
}

void CloseOutputPort(FuzzedDataProvider &fdp)
{
    CHECK_AND_RETURN(!(activeClients_.empty() || activeDevices_.empty()));
    size_t clientIdx = fdp.ConsumeIntegralInRange<size_t>(0, activeClients_.size() - 1);
    auto &client = activeClients_[clientIdx];
    size_t deviceIdx = fdp.ConsumeIntegralInRange<size_t>(0, activeDevices_.size() - 1);
    int64_t deviceId = activeDevices_[deviceIdx];
    uint32_t portIndex = fdp.ConsumeIntegralInRange<uint32_t>(0, MAX_FUZZ_PORTS - 1);
    midiServiceController_->CloseOutputPort(client.clientId, deviceId, portIndex);
}

void FlushOutputPort(FuzzedDataProvider &fdp)
{
    CHECK_AND_RETURN(!(activeClients_.empty() || activeDevices_.empty()));
    size_t clientIdx = fdp.ConsumeIntegralInRange<size_t>(0, activeClients_.size() - 1);
    auto &client = activeClients_[clientIdx];
    size_t deviceIdx = fdp.ConsumeIntegralInRange<size_t>(0, activeDevices_.size() - 1);
    int64_t deviceId = activeDevices_[deviceIdx];
    uint32_t portIndex = fdp.ConsumeIntegralInRange<uint32_t>(0, MAX_FUZZ_PORTS - 1);
    midiServiceController_->FlushOutputPort(client.clientId, deviceId, portIndex);
}

void DestroyMidiClient(FuzzedDataProvider &fdp)
{
    CHECK_AND_RETURN(!activeClients_.empty());
    size_t idx = fdp.ConsumeIntegralInRange<size_t>(0, activeClients_.size() - 1);
    midiServiceController_->DestroyMidiClient(activeClients_[idx].clientId);
    activeClients_.erase(activeClients_.begin() + idx);
    midiServiceController_->CancelUnloadTask();
}

void MultipleClientsOpenSamePort(FuzzedDataProvider &fdp)
{
    CHECK_AND_RETURN(activeClients_.size() > 1 && !activeDevices_.empty());
    size_t deviceIdx = fdp.ConsumeIntegralInRange<size_t>(0, activeDevices_.size() - 1);
    int64_t deviceId = activeDevices_[deviceIdx];
    uint32_t portIndex = fdp.ConsumeIntegralInRange<uint32_t>(0, MAX_FUZZ_PORTS - 1);

    for (auto &client : activeClients_) {
        midiServiceController_->OpenDevice(client.clientId, deviceId);
        midiServiceController_->OpenInputPort(client.clientId, client.buffer, deviceId, portIndex);
    }
}

void OpenDeviceWithInvalidId(FuzzedDataProvider &fdp)
{
    CHECK_AND_RETURN(!activeClients_.empty());
    size_t clientIdx = fdp.ConsumeIntegralInRange<size_t>(0, activeClients_.size() - 1);
    auto &client = activeClients_[clientIdx];
    int64_t invalidDeviceId = fdp.ConsumeIntegral<int64_t>();
    midiServiceController_->OpenDevice(client.clientId, invalidDeviceId);
}

void OpenDeviceWithInvalidClientId(FuzzedDataProvider &fdp)
{
    CHECK_AND_RETURN(!activeDevices_.empty());
    uint32_t invalidClientId = fdp.ConsumeIntegral<uint32_t>();
    size_t deviceIdx = fdp.ConsumeIntegralInRange<size_t>(0, activeDevices_.size() - 1);
    int64_t deviceId = activeDevices_[deviceIdx];
    midiServiceController_->OpenDevice(invalidClientId, deviceId);
}

void CloseDeviceWithInvalidId(FuzzedDataProvider &fdp)
{
    CHECK_AND_RETURN(!activeClients_.empty());
    size_t clientIdx = fdp.ConsumeIntegralInRange<size_t>(0, activeClients_.size() - 1);
    auto &client = activeClients_[clientIdx];
    int64_t invalidDeviceId = fdp.ConsumeIntegral<int64_t>();
    midiServiceController_->CloseDevice(client.clientId, invalidDeviceId);
}

void OpenInputPortWithInvalidId(FuzzedDataProvider &fdp)
{
    CHECK_AND_RETURN(!activeClients_.empty());
    size_t clientIdx = fdp.ConsumeIntegralInRange<size_t>(0, activeClients_.size() - 1);
    auto &client = activeClients_[clientIdx];
    int64_t invalidDeviceId = fdp.ConsumeIntegral<int64_t>();
    uint32_t invalidPortIndex = fdp.ConsumeIntegral<uint32_t>();

    midiServiceController_->OpenInputPort(client.clientId, client.buffer, invalidDeviceId, invalidPortIndex);
}

void CloseInputPortWithInvalidId(FuzzedDataProvider &fdp)
{
    CHECK_AND_RETURN(!activeClients_.empty());
    size_t clientIdx = fdp.ConsumeIntegralInRange<size_t>(0, activeClients_.size() - 1);
    auto &client = activeClients_[clientIdx];

    int64_t invalidDeviceId = fdp.ConsumeIntegral<int64_t>();
    uint32_t invalidPortIndex = fdp.ConsumeIntegral<uint32_t>();

    midiServiceController_->CloseInputPort(client.clientId, invalidDeviceId, invalidPortIndex);
}

void DestroyMidiClientWithInvalidId(FuzzedDataProvider &fdp)
{
    uint32_t invalidClientId = fdp.ConsumeIntegral<uint32_t>();
    midiServiceController_->DestroyMidiClient(invalidClientId);
    midiServiceController_->CancelUnloadTask();
}

void MidiServiceControllerInit()
{
    midiServiceController_ = MidiServiceController::GetInstance();

    auto usbMockDriver = std::make_unique<MockMidiDeviceDriver>();
    usbMockDriver->AddMockDevice(TEST_CLIENT_ID1, "USB MIDI Device 1", DeviceType::DEVICE_TYPE_USB);
    usbMockDriver->AddMockDevice(TEST_CLIENT_ID2, "USB MIDI Device 2", DeviceType::DEVICE_TYPE_USB);

    midiServiceController_->GetDeviceManagerForTest()->InjectDriverForTest(
        DeviceType::DEVICE_TYPE_USB, std::move(usbMockDriver));

    auto bleMockDriver = std::make_unique<MockMidiDeviceDriver>();
    g_rawMockDriver = bleMockDriver.get();
    midiServiceController_->GetDeviceManagerForTest()->InjectDriverForTest(
        DeviceType::DEVICE_TYPE_BLE, std::move(bleMockDriver));

    midiServiceController_->GetDeviceManagerForTest()->UpdateDevices();

    RefreshActiveDevices();
}

void MidiServiceControllerTest(const uint8_t *data, size_t size)
{
    RunMidiCoreCoverageScenarios(data, size);
    CHECK_AND_RETURN_LOG(midiServiceController_ != nullptr, "midiServiceController_ is nullptr");
    FuzzedDataProvider fdp(data, size);
    ExerciseDeterministicCoverage(fdp);

    while (fdp.remaining_bytes() > 0) {
        auto func = fdp.PickValueInArray({
            CreateMidiInServer,
            GetDevicePorts,
            OpenDevice,
            OpenInputPort,
            OpenOutputPort,
            CloseDevice,
            CloseInputPort,
            CloseOutputPort,
            FlushOutputPort,
            DestroyMidiClient,
            MultipleClientsOpenSamePort,
            OpenDeviceWithInvalidId,
            OpenDeviceWithInvalidClientId,
            CloseDeviceWithInvalidId,
            OpenInputPortWithInvalidId,
            CloseInputPortWithInvalidId,
            DestroyMidiClientWithInvalidId
        });
        func(fdp);
    }

    for (auto &client : activeClients_) {
        midiServiceController_->DestroyMidiClient(client.clientId);
    }
    activeClients_.clear();
    midiServiceController_->CancelUnloadTask();
    midiServiceController_->ClearStateForTest();
}

} // namespace MIDI
} // namespace OHOS

extern "C" int LLVMFuzzerInitialize(int *argc, char ***argv)
{
    (void)argc;
    (void)argv;
    if (SetSelfTokenID(OHOS::MIDI::TEST_TOKEN_ID) < 0) {
        return -1;
    }
    OHOS::MIDI::MidiServiceControllerInit();
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    OHOS::MIDI::MidiServiceControllerTest(data, size);
    return 0;
}
