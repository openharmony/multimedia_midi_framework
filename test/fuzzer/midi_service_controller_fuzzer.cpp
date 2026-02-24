/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <cstdint>
#include <fuzzer/FuzzedDataProvider.h>
#include <map>
#include <securec.h>
#include <string>
#include <vector>
#include <memory>
#include <unordered_set>

#include "accesstoken_kit.h"
#include "iremote_object.h"
#include "midi_info.h"
#include "midi_service_controller.h"
#include "midi_device_driver.h"
#include "midi_shared_ring.h"
#include "nativetoken_kit.h"
#include "token_setproc.h"

namespace OHOS {
namespace MIDI {
using namespace std;
namespace {
constexpr int32_t RING_BUFFER_DEFAULT_SIZE = 2048;
constexpr int32_t MAX_FUZZ_CLIENTS = 4;
constexpr int32_t MAX_FUZZ_DEVICES = 4;
constexpr int32_t MAX_FUZZ_PORTS = 2;
} // namespace

std::shared_ptr<MidiServiceController> midiServiceController_;

struct ClientContext {
    uint32_t clientId;
    sptr<IRemoteObject> clientObj;
    std::shared_ptr<MidiSharedRing> buffer;
};

std::vector<ClientContext> activeClients_;
std::vector<int64_t> activeDevices_;

class MockMidiDeviceDriver : public MidiDeviceDriver {
public:
    std::vector<DeviceInformation> GetRegisteredDevices() override
    {
        return mockDevices_;
    }

    int32_t OpenDevice(int64_t deviceId) override
    {
        if (openedDevices_.count(deviceId) > 0) {
            return MIDI_STATUS_DEVICE_ALREADY_OPEN;
        }
        openedDevices_.insert(deviceId);
        return MIDI_STATUS_OK;
    }

    int32_t OpenDevice(std::string deviceAddr, BleDriverCallback deviceCallback) override
    {
        return MIDI_STATUS_OK;
    }

    int32_t CloseDevice(int64_t deviceId) override
    {
        openedDevices_.erase(deviceId);
        openedInputPorts_.clear();
        openedOutputPorts_.clear();
        return MIDI_STATUS_OK;
    }

    int32_t OpenInputPort(int64_t deviceId, uint32_t portIndex, UmpInputCallback cb) override
    {
        uint64_t key = (static_cast<uint64_t>(deviceId) << 32) | portIndex;
        if (openedInputPorts_.count(key) > 0) {
            return MIDI_STATUS_PORT_ALREADY_OPEN;
        }
        openedInputPorts_.insert(key);
        return MIDI_STATUS_OK;
    }

    int32_t OpenOutputPort(int64_t deviceId, uint32_t portIndex) override
    {
        uint64_t key = (static_cast<uint64_t>(deviceId) << 32) | portIndex;
        if (openedOutputPorts_.count(key) > 0) {
            return MIDI_STATUS_PORT_ALREADY_OPEN;
        }
        openedOutputPorts_.insert(key);
        return MIDI_STATUS_OK;
    }

    int32_t CloseInputPort(int64_t deviceId, uint32_t portIndex) override
    {
        uint64_t key = (static_cast<uint64_t>(deviceId) << 32) | portIndex;
        openedInputPorts_.erase(key);
        return MIDI_STATUS_OK;
    }

    int32_t CloseOutputPort(int64_t deviceId, uint32_t portIndex) override
    {
        uint64_t key = (static_cast<uint64_t>(deviceId) << 32) | portIndex;
        openedOutputPorts_.erase(key);
        return MIDI_STATUS_OK;
    }

    int32_t HandleUmpInput(int64_t deviceId, uint32_t portIndex, std::vector<MidiEventInner> &list) override
    {
        return MIDI_STATUS_OK;
    }

    void AddMockDevice(int64_t driverId, const std::string &name, DeviceType type)
    {
        DeviceInformation info;
        info.driverDeviceId = driverId;
        info.deviceType = type;
        info.deviceName = name;
        info.productId = "1234";
        info.vendorId = "5678";
        info.transportProtocol = TransportProtocol::PROTOCOL_1_0;

        PortInformation port;
        port.portId = 0;
        port.direction = PortDirection::PORT_DIRECTION_INPUT;
        port.name = "Input Port";
        info.portInfos.push_back(port);

        PortInformation port2;
        port2.portId = 1;
        port2.direction = PortDirection::PORT_DIRECTION_OUTPUT;
        port2.name = "Output Port";
        info.portInfos.push_back(port2);

        mockDevices_.push_back(info);
    }

    void ClearMockDevices()
    {
        mockDevices_.clear();
        openedDevices_.clear();
        openedInputPorts_.clear();
        openedOutputPorts_.clear();
    }

private:
    std::vector<DeviceInformation> mockDevices_;
    std::unordered_set<int64_t> openedDevices_;
    std::unordered_set<uint64_t> openedInputPorts_;
    std::unordered_set<uint64_t> openedOutputPorts_;
};

MockMidiDeviceDriver *mockDriver_ = nullptr;

class MidiServiceCallbackFuzzer : public MidiServiceCallback {
public:
    void NotifyDeviceChange(DeviceChangeType change, std::map<int32_t, std::string> deviceInfo) override {}
    void NotifyError(int32_t code) override {}
};

void CreateMidiInServer(FuzzedDataProvider &fdp)
{
    if (activeClients_.size() >= MAX_FUZZ_CLIENTS) {
        return;
    }

    std::shared_ptr<MidiServiceCallback> callback = std::make_shared<MidiServiceCallbackFuzzer>();
    sptr<IRemoteObject> clientObj;
    uint32_t clientId = 0;
    int32_t ret = midiServiceController_->CreateMidiInServer(callback, clientObj, clientId);
    if (ret == MIDI_STATUS_OK) {
        ClientContext ctx;
        ctx.clientId = clientId;
        ctx.clientObj = clientObj;
        ctx.buffer = std::make_shared<MidiSharedRing>(RING_BUFFER_DEFAULT_SIZE);
        activeClients_.push_back(ctx);
    }
}

void GetDevices(FuzzedDataProvider &fdp)
{
    midiServiceController_->GetDevices();
}

void GetDevicePorts(FuzzedDataProvider &fdp)
{
    if (activeDevices_.empty()) {
        return;
    }
    int64_t deviceId = fdp.PickValueInArray(activeDevices_);
    midiServiceController_->GetDevicePorts(deviceId);
}

void OpenDevice(FuzzedDataProvider &fdp)
{
    if (activeClients_.empty() || activeDevices_.empty()) {
        return;
    }

    auto &client = fdp.PickValueInArray(activeClients_);
    int64_t deviceId = fdp.PickValueInArray(activeDevices_);
    midiServiceController_->OpenDevice(client.clientId, deviceId);
}

void OpenDeviceWithInvalidId(FuzzedDataProvider &fdp)
{
    if (activeClients_.empty()) {
        return;
    }

    auto &client = fdp.PickValueInArray(activeClients_);
    int64_t invalidDeviceId = fdp.ConsumeIntegral<int64_t>();
    midiServiceController_->OpenDevice(client.clientId, invalidDeviceId);
}

void CloseDevice(FuzzedDataProvider &fdp)
{
    if (activeClients_.empty() || activeDevices_.empty()) {
        return;
    }

    auto &client = fdp.PickValueInArray(activeClients_);
    int64_t deviceId = fdp.PickValueInArray(activeDevices_);
    midiServiceController_->CloseDevice(client.clientId, deviceId);
}

void CloseDeviceWithInvalidId(FuzzedDataProvider &fdp)
{
    if (activeClients_.empty()) {
        return;
    }

    auto &client = fdp.PickValueInArray(activeClients_);
    int64_t invalidDeviceId = fdp.ConsumeIntegral<int64_t>();
    midiServiceController_->CloseDevice(client.clientId, invalidDeviceId);
}

void OpenInputPort(FuzzedDataProvider &fdp)
{
    if (activeClients_.empty() || activeDevices_.empty()) {
        return;
    }

    auto &client = fdp.PickValueInArray(activeClients_);
    int64_t deviceId = fdp.PickValueInArray(activeDevices_);
    uint32_t portIndex = fdp.ConsumeIntegralInRange<uint32_t>(0, MAX_FUZZ_PORTS - 1);

    midiServiceController_->OpenInputPort(client.clientId, client.buffer, deviceId, portIndex);
}

void OpenInputPortWithInvalidId(FuzzedDataProvider &fdp)
{
    if (activeClients_.empty()) {
        return;
    }

    auto &client = fdp.PickValueInArray(activeClients_);
    int64_t invalidDeviceId = fdp.ConsumeIntegral<int64_t>();
    uint32_t portIndex = fdp.ConsumeIntegral<uint32_t>();

    midiServiceController_->OpenInputPort(client.clientId, client.buffer, invalidDeviceId, portIndex);
}

void OpenOutputPort(FuzzedDataProvider &fdp)
{
    if (activeClients_.empty() || activeDevices_.empty()) {
        return;
    }

    auto &client = fdp.PickValueInArray(activeClients_);
    int64_t deviceId = fdp.PickValueInArray(activeDevices_);
    uint32_t portIndex = fdp.ConsumeIntegralInRange<uint32_t>(0, MAX_FUZZ_PORTS - 1);

    midiServiceController_->OpenOutputPort(client.clientId, client.buffer, deviceId, portIndex);
}

void CloseInputPort(FuzzedDataProvider &fdp)
{
    if (activeClients_.empty() || activeDevices_.empty()) {
        return;
    }

    auto &client = fdp.PickValueInArray(activeClients_);
    int64_t deviceId = fdp.PickValueInArray(activeDevices_);
    uint32_t portIndex = fdp.ConsumeIntegralInRange<uint32_t>(0, MAX_FUZZ_PORTS - 1);

    midiServiceController_->CloseInputPort(client.clientId, deviceId, portIndex);
}

void CloseInputPortWithInvalidId(FuzzedDataProvider &fdp)
{
    if (activeClients_.empty()) {
        return;
    }

    auto &client = fdp.PickValueInArray(activeClients_);
    int64_t invalidDeviceId = fdp.ConsumeIntegral<int64_t>();
    uint32_t portIndex = fdp.ConsumeIntegral<uint32_t>();

    midiServiceController_->CloseInputPort(client.clientId, invalidDeviceId, portIndex);
}

void CloseOutputPort(FuzzedDataProvider &fdp)
{
    if (activeClients_.empty() || activeDevices_.empty()) {
        return;
    }

    auto &client = fdp.PickValueInArray(activeClients_);
    int64_t deviceId = fdp.PickValueInArray(activeDevices_);
    uint32_t portIndex = fdp.ConsumeIntegralInRange<uint32_t>(0, MAX_FUZZ_PORTS - 1);

    midiServiceController_->CloseOutputPort(client.clientId, deviceId, portIndex);
}

void FlushOutputPort(FuzzedDataProvider &fdp)
{
    if (activeClients_.empty() || activeDevices_.empty()) {
        return;
    }

    auto &client = fdp.PickValueInArray(activeClients_);
    int64_t deviceId = fdp.PickValueInArray(activeDevices_);
    uint32_t portIndex = fdp.ConsumeIntegralInRange<uint32_t>(0, MAX_FUZZ_PORTS - 1);

    midiServiceController_->FlushOutputPort(client.clientId, deviceId, portIndex);
}

void DestroyMidiClient(FuzzedDataProvider &fdp)
{
    if (activeClients_.empty()) {
        return;
    }

    auto &client = fdp.PickValueInArray(activeClients_);
    midiServiceController_->DestroyMidiClient(client.clientId);
}

void DestroyMidiClientWithInvalidId(FuzzedDataProvider &fdp)
{
    uint32_t invalidClientId = fdp.ConsumeIntegral<uint32_t>();
    midiServiceController_->DestroyMidiClient(invalidClientId);
}

void MultipleClientsOpenSameDevice(FuzzedDataProvider &fdp)
{
    if (activeClients_.size() < 2 || activeDevices_.empty()) {
        return;
    }

    int64_t deviceId = fdp.PickValueInArray(activeDevices_);

    for (auto &client : activeClients_) {
        midiServiceController_->OpenDevice(client.clientId, deviceId);
    }
}

void MultipleClientsOpenSamePort(FuzzedDataProvider &fdp)
{
    if (activeClients_.size() < 2 || activeDevices_.empty()) {
        return;
    }

    int64_t deviceId = fdp.PickValueInArray(activeDevices_);
    uint32_t portIndex = fdp.ConsumeIntegralInRange<uint32_t>(0, MAX_FUZZ_PORTS - 1);

    for (auto &client : activeClients_) {
        midiServiceController_->OpenDevice(client.clientId, deviceId);
        midiServiceController_->OpenInputPort(client.clientId, client.buffer, deviceId, portIndex);
    }
}

void OneClientClosesSharedDevice(FuzzedDataProvider &fdp)
{
    if (activeClients_.size() < 2 || activeDevices_.empty()) {
        return;
    }

    int64_t deviceId = fdp.PickValueInArray(activeDevices_);

    for (auto &client : activeClients_) {
        midiServiceController_->OpenDevice(client.clientId, deviceId);
    }

    midiServiceController_->CloseDevice(activeClients_[0].clientId, deviceId);
}

void RepeatOpenCloseDevice(FuzzedDataProvider &fdp)
{
    if (activeClients_.empty() || activeDevices_.empty()) {
        return;
    }

    auto &client = fdp.PickValueInArray(activeClients_);
    int64_t deviceId = fdp.PickValueInArray(activeDevices_);
    int32_t iterations = fdp.ConsumeIntegralInRange<int32_t>(1, 5);

    for (int32_t i = 0; i < iterations; i++) {
        midiServiceController_->OpenDevice(client.clientId, deviceId);
        midiServiceController_->CloseDevice(client.clientId, deviceId);
    }
}

void RepeatOpenClosePort(FuzzedDataProvider &fdp)
{
    if (activeClients_.empty() || activeDevices_.empty()) {
        return;
    }

    auto &client = fdp.PickValueInArray(activeClients_);
    int64_t deviceId = fdp.PickValueInArray(activeDevices_);
    uint32_t portIndex = fdp.ConsumeIntegralInRange<uint32_t>(0, MAX_FUZZ_PORTS - 1);
    int32_t iterations = fdp.ConsumeIntegralInRange<int32_t>(1, 5);

    midiServiceController_->OpenDevice(client.clientId, deviceId);

    for (int32_t i = 0; i < iterations; i++) {
        midiServiceController_->OpenInputPort(client.clientId, client.buffer, deviceId, portIndex);
        midiServiceController_->CloseInputPort(client.clientId, deviceId, portIndex);
    }
}

void MidiServiceControllerInit()
{
    midiServiceController_ = MidiServiceController::GetInstance();
    midiServiceController_->SetUnloadDelay(0);

    mockDriver_ = new MockMidiDeviceDriver();
    midiServiceController_->GetDeviceManagerForTest()->InjectDriverForTest(
        DeviceType::DEVICE_TYPE_USB, std::unique_ptr<MidiDeviceDriver>(mockDriver_));

    mockDriver_->AddMockDevice(1001, "USB MIDI Device 1", DeviceType::DEVICE_TYPE_USB);
    mockDriver_->AddMockDevice(1002, "USB MIDI Device 2", DeviceType::DEVICE_TYPE_USB);
    mockDriver_->AddMockDevice(1003, "USB MIDI Device 3", DeviceType::DEVICE_TYPE_USB);
    mockDriver_->AddMockDevice(1004, "USB MIDI Device 4", DeviceType::DEVICE_TYPE_USB);

    midiServiceController_->GetDeviceManagerForTest()->UpdateDevices();

    auto devices = midiServiceController_->GetDeviceManagerForTest()->GetDevices();
    for (const auto &dev : devices) {
        activeDevices_.push_back(dev.deviceId);
    }
}

void MidiServiceControllerTest(FuzzedDataProvider &fdp)
{
    CHECK_AND_RETURN_LOG(midiServiceController_ != nullptr, "midiServiceController_ is nullptr");

    auto func = fdp.PickValueInArray({
        CreateMidiInServer,
        GetDevices,
        GetDevicePorts,
        OpenDevice,
        OpenDeviceWithInvalidId,
        CloseDevice,
        CloseDeviceWithInvalidId,
        OpenInputPort,
        OpenInputPortWithInvalidId,
        OpenOutputPort,
        CloseInputPort,
        CloseInputPortWithInvalidId,
        CloseOutputPort,
        FlushOutputPort,
        DestroyMidiClient,
        DestroyMidiClientWithInvalidId,
        MultipleClientsOpenSameDevice,
        MultipleClientsOpenSamePort,
OneClientClosesSharedDevice,
        RepeatOpenCloseDevice,
        RepeatOpenClosePort
    });

    func(fdp);
}

void MidiServiceControllerCleanup()
{
    for (auto &client : activeClients_) {
        midiServiceController_->DestroyMidiClient(client.clientId);
    }
    activeClients_.clear();
    activeDevices_.clear();

    if (mockDriver_) {
        mockDriver_->ClearMockDevices();
    }

    midiServiceController_->ClearStateForTest();
}

} // namespace MIDI
} // namespace OHOS

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    FuzzedDataProvider fdp(data, size);
    OHOS::MIDI::MidiServiceControllerTest(fdp);
    return 0;
}

extern "C" int LLVMFuzzerInitialize(int *argc, char ***argv)
{
    (void)argc;
    (void)argv;
    if (SetSelfTokenID(718336240uLL | (1uLL << NUM_32)) < 0) {
        return -1;
    }
    OHOS::MIDI::MidiServiceControllerInit();
    return 0;
}

extern "C" int LLVMFuzzerTearDown()
{
    OHOS::MIDI::MidiServiceControllerCleanup();
    return 0;
}
