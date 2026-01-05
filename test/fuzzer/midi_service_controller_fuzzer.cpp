/*
 * Copyright (c) 2025 Huawei Device Co., Ltd.
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

#include <securec.h>
#include <fuzzer/FuzzedDataProvider.h>
#include <vector>
#include <map>
#include <string>

#include "midi_service_controller.h"
#include "midi_info.h"
#include "accesstoken_kit.h"
#include "nativetoken_kit.h"
#include "token_setproc.h"

namespace OHOS {
namespace MIDI {
using namespace std;

MidiServiceController* midiServiceController_ = nullptr;

// Mock Callback for CreateClientInServer
class MidiServiceCallbackFuzzer : public MidiServiceCallback {
public:
    void NotifyDeviceChange(DeviceChangeType change, std::map<int32_t, std::string> deviceInfo) override {}
    void NotifyError(int32_t code) override {}
};

void CreateClientInServer(FuzzedDataProvider& fdp)
{
    std::shared_ptr<MidiServiceCallback> callback = std::make_shared<MidiServiceCallbackFuzzer>();
    sptr<IRemoteObject> client;
    uint32_t clientId = 0;
    midiServiceController_->CreateClientInServer(callback, client, clientId);
}

void GetDevices(FuzzedDataProvider& fdp)
{
    midiServiceController_->GetDevices();
}

void GetDevicePorts(FuzzedDataProvider& fdp)
{
    int64_t deviceId = fdp.ConsumeIntegral<int64_t>();
    midiServiceController_->GetDevicePorts(deviceId);
}

void OpenDevice(FuzzedDataProvider& fdp)
{
    uint32_t clientId = fdp.ConsumeIntegral<uint32_t>();
    int64_t deviceId = fdp.ConsumeIntegral<int64_t>();
    midiServiceController_->OpenDevice(clientId, deviceId);
}

void CloseDevice(FuzzedDataProvider& fdp)
{
    uint32_t clientId = fdp.ConsumeIntegral<uint32_t>();
    int64_t deviceId = fdp.ConsumeIntegral<int64_t>();
    midiServiceController_->CloseDevice(clientId, deviceId);
}

void OpenInputPort(FuzzedDataProvider& fdp)
{
    uint32_t clientId = fdp.ConsumeIntegral<uint32_t>();
    int64_t deviceId = fdp.ConsumeIntegral<int64_t>();
    uint32_t portIndex = fdp.ConsumeIntegral<uint32_t>();
    
    // Create a dummy buffer pointer (or nullptr to test robustness)
    std::shared_ptr<SharedMidiRing> buffer = std::make_shared<SharedMidiRing>(2048); 
    midiServiceController_->OpenInputPort(clientId, buffer, deviceId, portIndex);
}

void CloseInputPort(FuzzedDataProvider& fdp)
{
    uint32_t clientId = fdp.ConsumeIntegral<uint32_t>();
    int64_t deviceId = fdp.ConsumeIntegral<int64_t>();
    uint32_t portIndex = fdp.ConsumeIntegral<uint32_t>();
    
    midiServiceController_->CloseInputPort(clientId, deviceId, portIndex);
}

void DestroyMidiClient(FuzzedDataProvider& fdp)
{
    uint32_t clientId = fdp.ConsumeIntegral<uint32_t>();
    midiServiceController_->DestroyMidiClient(clientId);
}

void MidiServiceControllerInit()
{
    midiServiceController_ = MidiServiceController::GetInstance();
    midiServiceController_->Init();
}

void MidiServiceControllerTest(FuzzedDataProvider& fdp)
{
    CHECK_AND_RETURN_LOG(midiServiceController_ != nullptr, "midiServiceController_ is nullptr");
    
    auto func = fdp.PickValueInArray({
        CreateClientInServer,
        GetDevices,
        GetDevicePorts,
        OpenDevice,
        CloseDevice,
        OpenInputPort,
        CloseInputPort,
        DestroyMidiClient
    });
    
    func(fdp);
}

} // namespace MIDI
} // namespace OHOS

/* Fuzzer entry point */
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    FuzzedDataProvider fdp(data, size);
    OHOS::MIDI::MidiServiceControllerTest(fdp);
    return 0;
}

extern "C" int LLVMFuzzerInitialize(int* argc, char*** argv)
{
    if (SetSelfTokenID(718336240uLL | (1uLL << NUM_32)) < 0) {
        return -1;
    }
    OHOS::MIDI::MidiServiceControllerInit();
    return 0;
}