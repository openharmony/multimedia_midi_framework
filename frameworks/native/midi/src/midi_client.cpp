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

#ifndef LOG_TAG
#define LOG_TAG "MidiClient"
#endif

#include <cstring>
#include <chrono>

#include "midi_log.h"
#include "midi_client_private.h"
#include "midi_service_client.h"
#include "securec.h"

namespace OHOS {
namespace MIDI {
namespace {
    constexpr uint32_t MAX_EVENTS_NUMS = 1000;
}  // namespace
class MidiClientCallback : public MidiCallbackStub {
public:
    MidiClientCallback(OH_MIDICallbacks callbacks, void *userData,
        std::function<void(OH_MIDIDeviceChangeAction change, OH_MIDIDeviceInformation info)> deviceChange);
    ~MidiClientCallback() = default;
    int32_t NotifyDeviceChange(int32_t change, const std::map<int32_t, std::string> &deviceInfo) override;
    int32_t NotifyError(int32_t code) override;
    OH_MIDICallbacks callbacks_;
    void *userData_;
    std::function<void(OH_MIDIDeviceChangeAction change, OH_MIDIDeviceInformation info)> deviceChange_;
};

static bool ConvertToDeviceInformation(
    const std::map<int32_t, std::string> &deviceInfo, OH_MIDIDeviceInformation &outInfo)
{
    // 初始化outInfo
    memset_s(&outInfo, sizeof(outInfo), 0, sizeof(outInfo));

    auto it = deviceInfo.find(DEVICE_ID);
    CHECK_AND_RETURN_RET_LOG(it != deviceInfo.end(), false, "deviceId error");
    outInfo.midiDeviceId = std::stoll(it->second);

    it = deviceInfo.find(DEVICE_TYPE);
    CHECK_AND_RETURN_RET_LOG(it != deviceInfo.end(), false, "deviceType error");
    outInfo.deviceType = static_cast<OH_MIDIDeviceType>(std::stoi(it->second));

    it = deviceInfo.find(MIDI_PROTOCOL);
    CHECK_AND_RETURN_RET_LOG(it != deviceInfo.end(), false, "protocol error");
    outInfo.nativeProtocol = static_cast<OH_MIDIProtocol>(std::stoi(it->second));

    it = deviceInfo.find(PRODUCT_NAME);
    CHECK_AND_RETURN_RET_LOG(it != deviceInfo.end(), false, "productName error");
    CHECK_AND_RETURN_RET_LOG(
        strncpy_s(outInfo.productName, sizeof(outInfo.productName), it->second.c_str(), it->second.length()) ==
            MIDI_STATUS_OK,
        false,
        "copy productName failed");

    it = deviceInfo.find(VENDOR_NAME);
    CHECK_AND_RETURN_RET_LOG(it != deviceInfo.end(), false, "vendorName error");
    CHECK_AND_RETURN_RET_LOG(
        strncpy_s(outInfo.vendorName, sizeof(outInfo.vendorName), it->second.c_str(), it->second.length()) ==
            MIDI_STATUS_OK,
        false,
        "copy vendorName failed");
    it = deviceInfo.find(ADDRESS);
    CHECK_AND_RETURN_RET_LOG(it != deviceInfo.end(), false, "deviceAddress error");
    CHECK_AND_RETURN_RET_LOG(
        strncpy_s(outInfo.deviceAddress, sizeof(outInfo.deviceAddress), it->second.c_str(), it->second.length()) ==
            MIDI_STATUS_OK,
        false,
        "copy deviceAddress failed");
    return true;
}
MidiClientDeviceOpenCallback::MidiClientDeviceOpenCallback(std::shared_ptr<MidiServiceInterface> midiServiceInterface,
    OH_MIDIOnDeviceOpened callback, void *userData)
    : ipc_(midiServiceInterface), callback_(callback), userData_(userData)
{
}

int32_t MidiClientDeviceOpenCallback::NotifyDeviceOpened(bool opened, const std::map<int32_t, std::string> &deviceInfo)
{
    CHECK_AND_RETURN_RET_LOG(callback_ != nullptr && ipc_.lock(), MIDI_STATUS_UNKNOWN_ERROR, "callback_ is nullptr");
    OH_MIDIDeviceInformation info;
    if (!opened) {
        callback_(userData_, opened, nullptr, info);
        return 0;
    }
    bool ret = ConvertToDeviceInformation(deviceInfo, info);
    CHECK_AND_RETURN_RET_LOG(ret, MIDI_STATUS_UNKNOWN_ERROR, "ConvertToDeviceInformation failed");
    auto newDevice = new MidiDevicePrivate(ipc_.lock(), info.midiDeviceId);
    callback_(userData_, opened, (OH_MIDIDevice *)newDevice, info);
    return 0;
}

static bool ConvertToPortInformation(
    const std::map<int32_t, std::string> &portInfo, int64_t deviceId, OH_MIDIPortInformation &outInfo)
{
    memset_s(&outInfo, sizeof(outInfo), 0, sizeof(outInfo));

    outInfo.deviceId = deviceId;

    auto it = portInfo.find(PORT_INDEX);
    CHECK_AND_RETURN_RET_LOG(it != portInfo.end(), false, "port index error");

    outInfo.portIndex = static_cast<uint32_t>(std::stoll(it->second));
    it = portInfo.find(DIRECTION);
    CHECK_AND_RETURN_RET_LOG(it != portInfo.end(), false, "direction error");
    outInfo.direction = static_cast<OH_MIDIPortDirection>(std::stoi(it->second));

    it = portInfo.find(PORT_NAME);
    CHECK_AND_RETURN_RET_LOG(it != portInfo.end() && !it->second.empty(), false, "port name error");

    CHECK_AND_RETURN_RET_LOG(
        strncpy_s(outInfo.name, sizeof(outInfo.name), it->second.c_str(), it->second.length()) == MIDI_STATUS_OK,
        false,
        "copy port name failed");
    return true;
}

static int32_t GetStatusCode(MidiStatusCode code)
{
    switch (code) {
        case MidiStatusCode::OK:
            return MIDI_STATUS_OK;
        case MidiStatusCode::WOULD_BLOCK:
            return MIDI_STATUS_WOULD_BLOCK;
        default:
            return MIDI_STATUS_UNKNOWN_ERROR;
    }
}

MidiClientCallback::MidiClientCallback(OH_MIDICallbacks callbacks, void *userData,
    std::function<void(OH_MIDIDeviceChangeAction change, OH_MIDIDeviceInformation info)> deviceChange)
    : callbacks_(callbacks), userData_(userData), deviceChange_(deviceChange)
{}

int32_t MidiClientCallback::NotifyDeviceChange(int32_t change, const std::map<int32_t, std::string> &deviceInfo)
{
    CHECK_AND_RETURN_RET_LOG(
        callbacks_.onDeviceChange != nullptr, MIDI_STATUS_UNKNOWN_ERROR, "callbacks_.onDeviceChange is nullptr");

    OH_MIDIDeviceInformation info;
    bool ret = ConvertToDeviceInformation(deviceInfo, info);
    CHECK_AND_RETURN_RET_LOG(ret, MIDI_STATUS_UNKNOWN_ERROR, "ConvertToDeviceInformation failed");
    deviceChange_(static_cast<OH_MIDIDeviceChangeAction>(change), info);

    callbacks_.onDeviceChange(userData_, static_cast<OH_MIDIDeviceChangeAction>(change), info);
    return 0;
}

int32_t MidiClientCallback::NotifyError(int32_t code)
{
    CHECK_AND_RETURN_RET_LOG(callbacks_.onError != nullptr, MIDI_STATUS_UNKNOWN_ERROR, "callbacks_.onError is nullptr");
    callbacks_.onError(userData_, (OH_MIDIStatusCode)code);
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

OH_MIDIStatusCode MidiDevicePrivate::CloseDevice()
{
    auto ipc = ipc_.lock();
    CHECK_AND_RETURN_RET_LOG(ipc != nullptr, MIDI_STATUS_UNKNOWN_ERROR, "ipc_ is nullptr");
    return ipc->CloseDevice(deviceId_);
}

OH_MIDIStatusCode MidiDevicePrivate::OpenInputPort(OH_MIDIPortDescriptor descriptor,
    OH_OnMIDIReceived callback, void *userData)
{
    std::lock_guard<std::mutex> lock(inputPortsMutex_);
    auto ipc = ipc_.lock();
    CHECK_AND_RETURN_RET_LOG(ipc != nullptr, MIDI_STATUS_UNKNOWN_ERROR, "ipc_ is nullptr");

    auto iter = inputPortsMap_.find(descriptor.portIndex);
    CHECK_AND_RETURN_RET(iter == inputPortsMap_.end(), MIDI_STATUS_PORT_ALREADY_OPEN);
    auto inputPort = std::make_shared<MidiInputPort>(callback, userData, descriptor.protocol);

    std::shared_ptr<MidiSharedRing> &buffer = inputPort->GetRingBuffer();
    auto ret = ipc->OpenInputPort(buffer, deviceId_, descriptor.portIndex);
    CHECK_AND_RETURN_RET_LOG(ret == MIDI_STATUS_OK, ret, "open inputport fail");

    CHECK_AND_RETURN_RET_LOG(
        inputPort->StartReceiverThread() == true, MIDI_STATUS_UNKNOWN_ERROR, "start receiver thread fail");

    inputPortsMap_.emplace(descriptor.portIndex, std::move(inputPort));
    MIDI_INFO_LOG("port[%{public}u] success", descriptor.portIndex);
    return MIDI_STATUS_OK;
}

OH_MIDIStatusCode MidiDevicePrivate::OpenOutputPort(OH_MIDIPortDescriptor descriptor)
{
    std::lock_guard<std::mutex> lock(outputPortsMutex_);
    auto ipc = ipc_.lock();
    CHECK_AND_RETURN_RET_LOG(ipc != nullptr, MIDI_STATUS_UNKNOWN_ERROR, "ipc_ is nullptr");

    auto iter = outputPortsMap_.find(descriptor.portIndex);
    CHECK_AND_RETURN_RET(iter == outputPortsMap_.end(), MIDI_STATUS_PORT_ALREADY_OPEN);

    auto outputPort = std::make_shared<MidiOutputPort>(descriptor.protocol);
    std::shared_ptr<MidiSharedRing> &buffer = outputPort->GetRingBuffer();
    auto ret = ipc->OpenOutputPort(buffer, deviceId_, descriptor.portIndex);
    CHECK_AND_RETURN_RET_LOG(ret == MIDI_STATUS_OK, ret, "open outputport fail");

    outputPortsMap_.emplace(descriptor.portIndex, std::move(outputPort));
    MIDI_INFO_LOG("port[%{public}u] success", descriptor.portIndex);
    return MIDI_STATUS_OK;
}

OH_MIDIStatusCode MidiDevicePrivate::Send(uint32_t portIndex, OH_MIDIEvent *events,
    uint32_t eventCount, uint32_t *eventsWritten)
{
    std::lock_guard<std::mutex> lock(outputPortsMutex_);
    auto iter = outputPortsMap_.find(portIndex);
    CHECK_AND_RETURN_RET_LOG(iter != outputPortsMap_.end(), MIDI_STATUS_INVALID_PORT, "invalid port");
    auto outputPort = iter->second;
    return (OH_MIDIStatusCode)outputPort->Send(events, eventCount, eventsWritten);
}

OH_MIDIStatusCode MidiDevicePrivate::FlushOutputPort(uint32_t portIndex)
{
    std::lock_guard<std::mutex> lock(outputPortsMutex_);
    auto iter = outputPortsMap_.find(portIndex);
    CHECK_AND_RETURN_RET(iter != outputPortsMap_.end(), MIDI_STATUS_INVALID_PORT);
    return MIDI_STATUS_OK;
}

OH_MIDIStatusCode MidiDevicePrivate::ClosePort(uint32_t portIndex)
{
    auto ipc = ipc_.lock();
    CHECK_AND_RETURN_RET_LOG(ipc != nullptr, MIDI_STATUS_UNKNOWN_ERROR, "ipc_ is nullptr");
    OH_MIDIStatusCode ret = MIDI_STATUS_INVALID_PORT;
    {
        std::lock_guard<std::mutex> lock(inputPortsMutex_);
        auto it = inputPortsMap_.find(portIndex);
        if (it != inputPortsMap_.end()) {
            ret = ipc->CloseInputPort(deviceId_, portIndex);
            inputPortsMap_.erase(it);
        }
    }
    {
        std::lock_guard<std::mutex> lock(outputPortsMutex_);
        auto it = outputPortsMap_.find(portIndex);
        if (it != outputPortsMap_.end()) {
            ret = ipc->CloseOutputPort(deviceId_, portIndex);
            outputPortsMap_.erase(it);
        }
    }
    CHECK_AND_RETURN_RET_LOG(ret == MIDI_STATUS_OK, ret, "close port fail");
    return MIDI_STATUS_OK;
}

MidiInputPort::MidiInputPort(OH_OnMIDIReceived callback, void *userData, OH_MIDIProtocol protocol)
    : callback_(callback), userData_(userData), protocol_(protocol)
{
    MIDI_INFO_LOG("InputPort created");
}

bool MidiInputPort::StartReceiverThread()
{
    CHECK_AND_RETURN_RET_LOG(running_.load() != true, false, "already start");
    CHECK_AND_RETURN_RET_LOG(ringBuffer_ != nullptr && callback_ != nullptr, false, "buffer or callback is nullptr");
    running_.store(true);
    receiverThread_ = std::thread(&MidiInputPort::ReceiverThreadLoop, this);
    return true;
}

bool MidiInputPort::StopReceiverThread()
{
    bool expected = true;

    CHECK_AND_RETURN_RET(running_.compare_exchange_strong(expected, false), true);

    if (ringBuffer_) {
        std::atomic<uint32_t> *futexPtr = ringBuffer_->GetFutex();
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

    std::atomic<uint32_t> *futexPtr = ringBuffer_->GetFutex();
    if (futexPtr == nullptr) {
        running_.store(false);
        return;
    }

    constexpr int64_t kWaitForever = -1;

    while (running_.load()) {
        (void)FutexTool::FutexWait(futexPtr, kWaitForever, [this]() { return ShouldWakeForReadOrExit(); });

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

    MidiSharedRing::PeekedEvent peekedEvent{};
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

    std::vector<OH_MIDIEvent> callbackEvents;
    callbackEvents.reserve(midiEvents.size());

    for (const auto &event : midiEvents) {
        OH_MIDIEvent callbackEvent{};
        callbackEvent.timestamp = event.timestamp;
        callbackEvent.length = event.length;
        callbackEvent.data = event.data;
        callbackEvents.push_back(callbackEvent);
    }
    MIDI_DEBUG_LOG("[client] receive midi events from server");
    MIDI_DEBUG_LOG("%{public}s", DumpMidiEvents(midiEvents).c_str());
    CHECK_AND_RETURN(protocol_ == MIDI_PROTOCOL_1_0 || protocol_ == MIDI_PROTOCOL_2_0);
    callback_(userData_, callbackEvents.data(), callbackEvents.size());
}

MidiInputPort::~MidiInputPort()
{
    (void)StopReceiverThread();
}

std::shared_ptr<MidiSharedRing> &MidiInputPort::GetRingBuffer()
{
    return ringBuffer_;
}

MidiOutputPort::MidiOutputPort(OH_MIDIProtocol protocol) : protocol_(protocol)
{
    MIDI_INFO_LOG("OutputPort created");
}

int32_t MidiOutputPort::Send(OH_MIDIEvent *events, uint32_t eventCount, uint32_t *eventsWritten)
{
    CHECK_AND_RETURN_RET_LOG(events && eventsWritten, MIDI_STATUS_GENERIC_INVALID_ARGUMENT,
        "parameter is nullptr");
    CHECK_AND_RETURN_RET_LOG(eventCount > 0 && eventCount <= MAX_EVENTS_NUMS, MIDI_STATUS_GENERIC_INVALID_ARGUMENT,
        "parameter is invalid");
    CHECK_AND_RETURN_RET_LOG(protocol_ == MIDI_PROTOCOL_1_0 || protocol_ == MIDI_PROTOCOL_2_0,
        MIDI_STATUS_GENERIC_INVALID_ARGUMENT, "protocol is invalid");

    thread_local std::vector<MidiEventInner> innerEvents;
    innerEvents.clear();
    innerEvents.resize(eventCount);

    for (uint32_t i = 0; i < eventCount; ++i) {
        innerEvents[i] = MidiEventInner{events[i].timestamp, events[i].length, events[i].data};
    }
    MIDI_DEBUG_LOG("[client] send midi events");
    MIDI_DEBUG_LOG("%{public}s", DumpMidiEvents(innerEvents).c_str());
    auto ret = ringBuffer_->TryWriteEvents(innerEvents.data(), eventCount, eventsWritten);
    return GetStatusCode(ret);
}

std::shared_ptr<MidiSharedRing> &MidiOutputPort::GetRingBuffer()
{
    return ringBuffer_;
}

MidiOutputPort::~MidiOutputPort()
{
    MIDI_INFO_LOG("OutputPort destroy");
}

MidiClientPrivate::MidiClientPrivate() : ipc_(std::make_shared<MidiServiceClient>())
{
    MIDI_INFO_LOG("MidiClientPrivate created");
}

MidiClientPrivate::~MidiClientPrivate()
{
    MIDI_INFO_LOG("MidiClientPrivate destroyed");
}

OH_MIDIStatusCode MidiClientPrivate::Init(OH_MIDICallbacks callbacks, void *userData)
{
    CHECK_AND_RETURN_RET_LOG(ipc_ != nullptr, MIDI_STATUS_UNKNOWN_ERROR, "ipc_ is nullptr");
    callback_ = sptr<MidiClientCallback>::MakeSptr(callbacks,
        userData,
        [this](OH_MIDIDeviceChangeAction change, OH_MIDIDeviceInformation info) { this->DeviceChange(change, info); });
    auto ret = ipc_->Init(callback_, clientId_);
    CHECK_AND_RETURN_RET(ret == MIDI_STATUS_OK, ret);
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::map<int32_t, std::string>> deviceInfos;
    ret = ipc_->GetDevices(deviceInfos);
    CHECK_AND_RETURN_RET(ret == MIDI_STATUS_OK, ret);
    for (auto deviceInfo : deviceInfos) {
        OH_MIDIDeviceInformation info;
        bool ret = ConvertToDeviceInformation(deviceInfo, info);
        CHECK_AND_CONTINUE_LOG(ret, "ConvertToDeviceInformation failed");
        deviceInfos_.push_back(info);
    }
    return MIDI_STATUS_OK;
}

void MidiClientPrivate::DeviceChange(OH_MIDIDeviceChangeAction change, OH_MIDIDeviceInformation info)
{
    std::lock_guard<std::mutex> lock(mutex_);
    MIDI_INFO_LOG("DeviceChange: %{public}d", change);
    if (change == MIDI_DEVICE_CHANGE_ACTION_CONNECTED) {
        deviceInfos_.push_back(info);
        return;
    }
    for (auto it = deviceInfos_.begin(); it != deviceInfos_.end();) {
        if (it->midiDeviceId == info.midiDeviceId) {
            it = deviceInfos_.erase(it);
            break;
        } else {
            it++;
        }
    }
}

OH_MIDIStatusCode MidiClientPrivate::GetDevices(OH_MIDIDeviceInformation *infos, size_t *numDevices)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (*numDevices < deviceInfos_.size()) {
        *numDevices = deviceInfos_.size();
        return MIDI_STATUS_INSUFFICIENT_RESULT_SPACE;
    }

    *numDevices = deviceInfos_.size();
    CHECK_AND_RETURN_RET(*numDevices != 0, MIDI_STATUS_OK);
    CHECK_AND_RETURN_RET(infos != nullptr, MIDI_STATUS_GENERIC_INVALID_ARGUMENT);
    for (size_t i = 0; i < *numDevices; i++) {
        infos[i] = deviceInfos_[i];
    }
    return MIDI_STATUS_OK;
}

OH_MIDIStatusCode MidiClientPrivate::OpenDevice(int64_t deviceId, MidiDevice **midiDevice)
{
    CHECK_AND_RETURN_RET_LOG(midiDevice != nullptr, MIDI_STATUS_UNKNOWN_ERROR, "midiDevice is nullptr");
    CHECK_AND_RETURN_RET_LOG(ipc_ != nullptr, MIDI_STATUS_UNKNOWN_ERROR, "ipc_ is nullptr");
    auto ret = ipc_->OpenDevice(deviceId);
    CHECK_AND_RETURN_RET(ret == MIDI_STATUS_OK, ret);
    auto newDevice = new MidiDevicePrivate(ipc_, deviceId);
    *midiDevice = newDevice;
    MIDI_INFO_LOG("Device opened: %{public}" PRId64, deviceId);
    return MIDI_STATUS_OK;
}

OH_MIDIStatusCode MidiClientPrivate::OpenBleDevice(std::string address, OH_MIDIOnDeviceOpened callback, void *userData)
{
    CHECK_AND_RETURN_RET_LOG(ipc_ != nullptr, MIDI_STATUS_UNKNOWN_ERROR, "ipc_ is nullptr");
    auto deivceOpenCallback = sptr<MidiClientDeviceOpenCallback>::MakeSptr(ipc_, callback, userData);
    auto ret = ipc_->OpenBleDevice(address, deivceOpenCallback);
    return ret;
}

OH_MIDIStatusCode MidiClientPrivate::GetDevicePorts(int64_t deviceId, OH_MIDIPortInformation *infos, size_t *numPorts)
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::map<int32_t, std::string>> portInfos;
    CHECK_AND_RETURN_RET_LOG(ipc_ != nullptr, MIDI_STATUS_UNKNOWN_ERROR, "ipc_ is nullptr");
    auto ret = ipc_->GetDevicePorts(deviceId, portInfos);
    CHECK_AND_RETURN_RET(ret == MIDI_STATUS_OK, ret);
    if (*numPorts < portInfos.size()) {
        *numPorts = portInfos.size();
        return MIDI_STATUS_INSUFFICIENT_RESULT_SPACE;
    }
    *numPorts = portInfos.size();
    CHECK_AND_RETURN_RET(*numPorts != 0, MIDI_STATUS_OK);
    CHECK_AND_RETURN_RET(infos != nullptr, MIDI_STATUS_GENERIC_INVALID_ARGUMENT);

    for (size_t i = 0; i < portInfos.size(); i++) {
        OH_MIDIPortInformation info;
        bool ret = ConvertToPortInformation(portInfos[i], deviceId, info);
        CHECK_AND_CONTINUE_LOG(ret, "ConvertToPortInformation failed");
        infos[i] = info;
    }

    return MIDI_STATUS_OK;
}

OH_MIDIStatusCode MidiClientPrivate::DestroyMidiClient()
{
    CHECK_AND_RETURN_RET_LOG(ipc_ != nullptr, MIDI_STATUS_UNKNOWN_ERROR, "ipc_ is nullptr");
    return ipc_->DestroyMidiClient();
}

OH_MIDIStatusCode MidiClient::CreateMidiClient(MidiClient **client, OH_MIDICallbacks callbacks, void *userData)
{
    CHECK_AND_RETURN_RET_LOG(client != nullptr, MIDI_STATUS_UNKNOWN_ERROR, "client is nullptr");
    *client = new MidiClientPrivate();
    OH_MIDIStatusCode ret = (*client)->Init(callbacks, userData);
    CHECK_AND_RETURN_RET(ret != MIDI_STATUS_OK, ret);
    delete *client;
    return ret;
}
}  // namespace MIDI
}  // namespace OHOS