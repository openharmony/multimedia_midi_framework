/*
 * Copyright (c) 2025 Huawei Device Co., Ltd.
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

#include "midi_client_private.h"

#include <cstring>

#include "midi_log.h"
#include "midi_callback_stub.h"

namespace OHOS {
namespace MIDI {
namespace {
    std::vector<std::unique_ptr<MidiClient>> clients;
    std::mutex clientsMutex;
}

class MidiClientCallback : public MidiCallbackStub {
public:
    MidiClientCallback(OH_MidiCallbacks callbacks, void *userData);
    ~MidiClientCallback() = default;
    int32_t NotifyDeviceChange(int32_t change, const std::map<int32_t, std::string> &deviceInfo) override;
    int32_t NotifyError(int32_t code) override;
    OH_MidiCallbacks callbacks_;
    void *userData_;
};

static bool ConvertToDeviceInformation(const std::map<int32_t, std::string> &deviceInfo, OH_MidiDeviceInformation& outInfo)
{
    // 初始化outInfo
    memset(&outInfo, 0, sizeof(outInfo));

    auto it = deviceInfo.find(DEVICE_ID);
    CHECK_AND_RETURN_RET_LOG(it != deviceInfo.end(), false, "deviceId error");
    outInfo.midiDeviceId = std::stoll(it->second);

    it = deviceInfo.find(DEVICE_TYPE);
    CHECK_AND_RETURN_RET_LOG(it != deviceInfo.end(), false, "deviceType error");
    outInfo.deviceType = static_cast<OH_MidiDeviceType>(std::stoi(it->second));

    it = deviceInfo.find(MIDI_PROTOCOL);
    CHECK_AND_RETURN_RET_LOG(it != deviceInfo.end(), false, "protocol error");
    outInfo.nativeProtocol = static_cast<OH_MidiProtocol>(std::stoi(it->second));

    it = deviceInfo.find(PRODUCT_NAME);
    CHECK_AND_RETURN_RET_LOG(it != deviceInfo.end(), false, "productName error");
    strncpy(outInfo.productName, it->second.c_str(), sizeof(outInfo.productName) - 1);
    outInfo.productName[sizeof(outInfo.productName) - 1] = '\0';

    it = deviceInfo.find(VENDOR_NAME);
    CHECK_AND_RETURN_RET_LOG(it != deviceInfo.end(), false, "vendorName error");
    strncpy(outInfo.vendorName, it->second.c_str(), sizeof(outInfo.vendorName) - 1);
    outInfo.vendorName[sizeof(outInfo.vendorName) - 1] = '\0';
    return true;
}

static bool ConvertToPortInformation(const std::map<int32_t, std::string> &portInfo, int64_t deviceId, OH_MidiPortInformation& outInfo)
{
    memset(&outInfo, 0, sizeof(outInfo));

    outInfo.deviceId = deviceId;

    auto it = portInfo.find(PORT_INDEX);
    CHECK_AND_RETURN_RET_LOG(it != portInfo.end(), false, "port index error");

    outInfo.portIndex = static_cast<uint32_t>(std::stoll(it->second));
    it = portInfo.find(DIRECTION);
    CHECK_AND_RETURN_RET_LOG(it != portInfo.end(), false, "direction error");
    outInfo.direction = static_cast<OH_MidiPortDirection>(std::stoi(it->second));
    
    it = portInfo.find(PORT_NAME);
    CHECK_AND_RETURN_RET_LOG(it != portInfo.end() && !it->second.empty(), false, "port name error");
    strncpy(outInfo.name, it->second.c_str(), sizeof(outInfo.name) - 1);
    outInfo.name[sizeof(outInfo.name) - 1] = '\0';
    return true;
}

MidiClientCallback::MidiClientCallback(OH_MidiCallbacks callbacks, void *userData)
    : callbacks_(callbacks), userData_(userData)
{

}

int32_t MidiClientCallback::NotifyDeviceChange(int32_t change, const std::map<int32_t, std::string> &deviceInfo)
{
    CHECK_AND_RETURN_RET_LOG(callbacks_.onDeviceChange != nullptr, MIDI_STATUS_UNKNOWN_ERROR, "callbacks_.onDeviceChange is nullptr");
    
    OH_MidiDeviceInformation info;
    bool ret = ConvertToDeviceInformation(deviceInfo, info);
    // todo 改变midiClient中的设备信息
    CHECK_AND_RETURN_RET_LOG(ret, MIDI_STATUS_UNKNOWN_ERROR, "ConvertToDeviceInformation failed");
    
    callbacks_.onDeviceChange(userData_, static_cast<OH_MidiDeviceChangeAction>(change), info);
    return 0;
}

int32_t MidiClientCallback::NotifyError(int32_t code)
{
    CHECK_AND_RETURN_RET_LOG(callbacks_.onError != nullptr, MIDI_STATUS_UNKNOWN_ERROR, "callbacks_.onError is nullptr");
    callbacks_.onError(userData_, (OH_MidiStatusCode)code);
    return 0;
}

MidiDevicePrivate::MidiDevicePrivate(std::shared_ptr<MidiServiceClient> midiServiceClient, int64_t deviceId)
    : ipc_(midiServiceClient), deviceId_(deviceId)
{
    MIDI_INFO_LOG("MidiDevicePrivate created");
}

MidiDevicePrivate::~MidiDevicePrivate()
{
    MIDI_INFO_LOG("MidiDevicePrivate destroyed");
}

OH_MidiStatusCode MidiDevicePrivate::CloseDevice()
{
    return ipc_->CloseDevice(deviceId_);
}

MidiClientPrivate::MidiClientPrivate() 
    : ipc_(std::make_shared<MidiServiceClient>()) 
{
    MIDI_INFO_LOG("MidiClientPrivate created");
}

MidiClientPrivate::~MidiClientPrivate()
{
    MIDI_INFO_LOG("MidiClientPrivate destroyed");
}

OH_MidiStatusCode MidiClientPrivate::Init(OH_MidiCallbacks callbacks, void *userData)
{
    callback_ = sptr<MidiClientCallback>::MakeSptr(callbacks, userData);
    auto ret = ipc_->Init(callback_, clientId_);
    CHECK_AND_RETURN_RET(ret == MIDI_STATUS_OK, ret);

    std::vector<std::map<int32_t, std::string>> deviceInfos;
    ret = ipc_->GetDevices(deviceInfos);
    deviceInfos_.clear();
    for (auto deviceInfo : deviceInfos) {
        OH_MidiDeviceInformation info;
        bool ret = ConvertToDeviceInformation(deviceInfo, info);
        CHECK_AND_RETURN_RET_LOG(ret, MIDI_STATUS_UNKNOWN_ERROR, "ConvertToDeviceInformation failed");
        deviceInfos_.push_back(info);
    }
    return MIDI_STATUS_OK;
}

OH_MidiStatusCode MidiClientPrivate::GetDevices(OH_MidiDeviceInformation *infos, size_t *numDevices)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (*numDevices < deviceInfos_.size()) {
        *numDevices = deviceInfos_.size();
        return MIDI_STATUS_INSUFFICIENT_RESULT_SPACE;
    }
    
    *numDevices = deviceInfos_.size();
    CHECK_AND_RETURN_RET(*numDevices != 0, MIDI_STATUS_OK);
    for (size_t i = 0; i < *numDevices; i++) {
        infos[i] = deviceInfos_[i];
    }
    return MIDI_STATUS_OK;
}

OH_MidiStatusCode MidiClientPrivate::OpenDevice(int64_t deviceId, MidiDevice **midiDevice)
{
    CHECK_AND_RETURN_RET_LOG(midiDevice != nullptr, MIDI_STATUS_UNKNOWN_ERROR, "midiDevice is nullptr");
    std::lock_guard<std::mutex> lock(mutex_);
    auto ret = ipc_->OpenDevice(deviceId);
    CHECK_AND_RETURN_RET(ret == MIDI_STATUS_OK, ret);
    auto newDevice = new MidiDevicePrivate(ipc_, deviceId);
    *midiDevice = newDevice;
    MIDI_INFO_LOG("Device opened: %{public}" PRId64, deviceId);
    return MIDI_STATUS_OK;
}

OH_MidiStatusCode MidiClientPrivate::GetDevicePorts(int64_t deviceId, OH_MidiPortInformation *infos, size_t *numPorts)
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::map<int32_t, std::string>> portInfos;
    auto ret = ipc_->GetDevicePorts(deviceId, portInfos);
    CHECK_AND_RETURN_RET(ret == MIDI_STATUS_OK, ret);
    if (*numPorts < portInfos.size()) {
        *numPorts = portInfos.size();
        return MIDI_STATUS_INSUFFICIENT_RESULT_SPACE;
    }
    
    *numPorts = portInfos.size();
    size_t i = 0;
    for (auto portInfo : portInfos) {
        OH_MidiPortInformation info;
        bool ret = ConvertToPortInformation(portInfo, deviceId, info);
        CHECK_AND_RETURN_RET_LOG(ret, MIDI_STATUS_UNKNOWN_ERROR, "ConvertToPortInformation failed");
        infos[i] = info;
        i++;
    }
    return MIDI_STATUS_OK;
}

OH_MidiStatusCode MidiClientPrivate::DestroyMidiClient()
{
    return ipc_->DestroyMidiClient();
}

OH_MidiStatusCode MidiClient::CreateMidiClient(MidiClient **client, OH_MidiCallbacks callbacks, void *userData)
{
    CHECK_AND_RETURN_RET_LOG(client != nullptr, MIDI_STATUS_UNKNOWN_ERROR, "client is nullptr");
    std::lock_guard<std::mutex> lock(clientsMutex);
    auto midiClient = std::make_unique<MidiClientPrivate>();
    OH_MidiStatusCode ret = midiClient->Init(callbacks, userData);
    CHECK_AND_RETURN_RET(ret == MIDI_STATUS_OK, ret);
    clients.push_back(std::move(midiClient));
    *client = clients.back().get();
    return MIDI_STATUS_OK;
}
} // namespace MIDI
} // namespace OHOS