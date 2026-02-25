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
#include "midi_log.h"
#include "midi_callback_stub.h"

namespace OHOS {
namespace MIDI {
using namespace std;
namespace {
constexpr int32_t RING_BUFFER_DEFAULT_SIZE = 2048;
constexpr uint32_t MAX_FUZZ_PORTS = 2;

constexpr uint64_t TEST_TOKEN_ID = 718336240uLL | (1uLL << 32);
constexpr int64_t TEST_CLIENT_ID1 = 1001;
constexpr int64_t TEST_CLIENT_ID2 = 1005;
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

        PortInformation port1;
        port1.portId = 0;
        port1.direction = PortDirection::PORT_DIRECTION_INPUT;
        port1.name = "Input Port";
        info.portInfos.push_back(port1);

        PortInformation port2;
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

class MidiServiceCallbackFuzzer : public MidiCallbackStub {
public:
    int32_t NotifyDeviceChange(int32_t change, const std::map<int32_t, std::string> &deviceInfo) override
    {
        return 0;
    };
    int32_t NotifyError(int32_t code) override { return 0; };
};

void CreateMidiInServer(FuzzedDataProvider &fdp)
{
    sptr<MidiServiceCallbackFuzzer> callback = new MidiServiceCallbackFuzzer();
    sptr<IRemoteObject> clientObj;
    uint32_t clientId = 0;
    int32_t ret = midiServiceController_->CreateMidiInServer(callback->AsObject(), clientObj, clientId);
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
    CHECK_AND_RETURN(!activeDevices_.empty());
    size_t deviceIdx = fdp.ConsumeIntegralInRange<size_t>(0, activeDevices_.size() - 1);
    int64_t deviceId = activeDevices_[deviceIdx];
    midiServiceController_->GetDevicePorts(deviceId);
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
}

void MidiServiceControllerInit()
{
    midiServiceController_ = MidiServiceController::GetInstance();
    midiServiceController_->SetUnloadDelay(0);

    auto mockDriver = std::make_unique<MockMidiDeviceDriver>();
    mockDriver->AddMockDevice(TEST_CLIENT_ID1, "USB MIDI Device 1", DeviceType::DEVICE_TYPE_USB);
    mockDriver->AddMockDevice(TEST_CLIENT_ID2, "USB MIDI Device 2", DeviceType::DEVICE_TYPE_USB);

    midiServiceController_->GetDeviceManagerForTest()->InjectDriverForTest(
        DeviceType::DEVICE_TYPE_USB, std::move(mockDriver));

    midiServiceController_->GetDeviceManagerForTest()->UpdateDevices();

    auto devices = midiServiceController_->GetDeviceManagerForTest()->GetDevices();
    for (const auto &dev : devices) {
        activeDevices_.push_back(dev.deviceId);
    }
}

void MidiServiceControllerTest(const uint8_t *data, size_t size)
{
    CHECK_AND_RETURN_LOG(midiServiceController_ != nullptr, "midiServiceController_ is nullptr");
    FuzzedDataProvider fdp(data, size);
    while (fdp.remaining_bytes() > 0) {
        auto func = fdp.PickValueInArray({
            CreateMidiInServer,
            GetDevices,
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