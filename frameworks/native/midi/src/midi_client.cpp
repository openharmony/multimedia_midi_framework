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

#ifndef LOG_TAG
#define LOG_TAG "MidiClient"
#endif

#include <cstring>
#include <chrono>

#include "midi_log.h"
#include "midi_client_private.h"
#include "midi_callback_stub.h"
#include "midi_service_client.h"

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

MidiDevicePrivate::MidiDevicePrivate(std::shared_ptr<MidiServiceInterface> midiServiceInterface, int64_t deviceId)
    : ipc_(midiServiceInterface), deviceId_(deviceId)
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

OH_MidiStatusCode MidiDevicePrivate::OpenInputPort(uint32_t portIndex, OH_OnMidiReceived callback,
                                            void *userData)
{
    {
        std::lock_guard<std::mutex> lock(inputPortsMutex_);
        auto iter = inputPortsMap_.find(portIndex);
        CHECK_AND_RETURN_RET(iter == inputPortsMap_.end(), MIDI_STATUS_OK);
    }
    auto inputPort = std::make_shared<MidiInputPort>(callback, userData);
    std::shared_ptr<SharedMidiRing>& buffer = inputPort->GetRingBuffer();
    auto ret = ipc_->OpenInputPort(buffer, deviceId_, portIndex);
    CHECK_AND_RETURN_RET_LOG(ret == MIDI_STATUS_OK, ret, "open inputport fail");

    CHECK_AND_RETURN_RET_LOG(inputPort->StartReceiverThread() == true,
                             MIDI_STATUS_UNKNOWN_ERROR,
                             "start receiver thread fail");

    {
        std::lock_guard<std::mutex> lock(inputPortsMutex_);
        inputPortsMap_.emplace(portIndex, std::move(inputPort));
    }
    MIDI_INFO_LOG("port[%{public}u] success", portIndex);
    return MIDI_STATUS_OK;
}

OH_MidiStatusCode MidiDevicePrivate::ClosePort(uint32_t portIndex)
{
    OH_MidiStatusCode ret = MIDI_STATUS_OK;
    {
        std::lock_guard<std::mutex> lock(inputPortsMutex_);
        auto it = inputPortsMap_.find(portIndex);
        CHECK_AND_RETURN_RET(it != inputPortsMap_.end(), MIDI_STATUS_OK);
        ret = ipc_->CloseInputPort(deviceId_, portIndex);
        inputPortsMap_.erase(it);
    }
    CHECK_AND_RETURN_RET_LOG(ret == MIDI_STATUS_OK, ret, "close inputport fail");
    return MIDI_STATUS_OK;
}

MidiInputPort::MidiInputPort(OH_OnMidiReceived callback, void *userData)
    : callback_(callback), userData_(userData)
{
    MIDI_INFO_LOG("InputPort created");
}

bool MidiInputPort::StartReceiverThread()
{
    CHECK_AND_RETURN_RET_LOG(running_.load() != true, false, "already start");
    CHECK_AND_RETURN_RET_LOG(ringBuffer_ != nullptr && callback_ != nullptr,
        false, "buffer or callback is nullptr");
    running_.store(true);
    receiverThread_ = std::thread(&MidiInputPort::ReceiverThreadLoop, this);
    return true;
}


bool MidiInputPort::StopReceiverThread()
{
    bool expected = true;
    
    CHECK_AND_RETURN_RET(running_.compare_exchange_strong(expected, false), true);

    if (ringBuffer_) {
        std::atomic<uint32_t>* futexPtr = ringBuffer_->GetFutex();
        if (futexPtr != nullptr) {
            (void)FutexTool::FutexWake(futexPtr, IS_PRE_EXIT);
        }
    }
    if (receiverThread_.joinable()) {
        receiverThread_.join();
    }
    return true;
}

void MidiInputPort::ReceiverThreadLoop()
{
    if (!ringBuffer_) {
        running_.store(false);
        return;
    }

    std::atomic<uint32_t>* futexPtr = ringBuffer_->GetFutex();
    if (futexPtr == nullptr) {
        running_.store(false);
        return;
    }

    constexpr int64_t kWaitForever = -1;

    while (running_.load()) {
        (void)FutexTool::FutexWait(
            futexPtr,
            kWaitForever,
            [this]() { return ShouldWakeForReadOrExit(); });

        if (!running_.load()) {
            break;
        }

        DrainRingAndDispatch();
    }
}


bool MidiInputPort::ShouldWakeForReadOrExit() const
{
    if (!running_.load()) {
        return true;
    }
    if (!ringBuffer_) {
        return true;
    }

    SharedMidiRing::PeekedEvent peekedEvent {};
    MidiStatusCode status = ringBuffer_->PeekNext(peekedEvent);
    return (status == MidiStatusCode::OK);
}

void MidiInputPort::DrainRingAndDispatch()
{
    if (!ringBuffer_ || callback_ == nullptr) {
        return;
    }

    std::vector<MidiEvent> midiEvents;
    std::vector<std::vector<uint32_t>> payloadBuffers;

    ringBuffer_->DrainToBatch(midiEvents, payloadBuffers, 0);

    if (midiEvents.empty()) {
        return;
    }

    std::vector<OH_MidiEvent> callbackEvents;
    callbackEvents.reserve(midiEvents.size());

    for (const auto& event : midiEvents) {
        OH_MidiEvent callbackEvent {};
        callbackEvent.timestamp = event.timestamp;
        callbackEvent.length = event.length;
        callbackEvent.data = event.data;       
        callbackEvents.push_back(callbackEvent);
    }

    callback_(userData_, callbackEvents.data(), callbackEvents.size());
}



MidiInputPort::~MidiInputPort()
{
    (void)StopReceiverThread();
}

std::shared_ptr<SharedMidiRing> &MidiInputPort::GetRingBuffer()
{
    return ringBuffer_;
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