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
    constexpr uint32_t PORT_GROUP_RANGE = 16;
}  // namespace
class MidiClientCallback : public MidiCallbackStub {
public:
    MidiClientCallback(OH_MIDICallbacks callbacks, void *userData);
    ~MidiClientCallback() = default;
    int32_t NotifyDeviceChange(int32_t change, const std::map<int32_t, std::string> &deviceInfo) override;
    int32_t NotifyError(int32_t code) override;
    OH_MIDICallbacks callbacks_;
    void *userData_;
};

static bool ConvertToDeviceInformation(
    const std::map<int32_t, std::string> &deviceInfo, OH_MIDIDeviceInformation &outInfo)
{
    // 初始化outInfo
    memset_s(&outInfo, sizeof(outInfo), 0, sizeof(outInfo));

    auto it = deviceInfo.find(DEVICE_ID);
    CHECK_AND_RETURN_RET_LOG(it != deviceInfo.end(), false, "deviceId error");
    outInfo.midiDeviceId = StringToNum(it->second);

    it = deviceInfo.find(DEVICE_TYPE);
    CHECK_AND_RETURN_RET_LOG(it != deviceInfo.end(), false, "deviceType error");
    outInfo.deviceType = static_cast<OH_MIDIDeviceType>(StringToNum(it->second));

    it = deviceInfo.find(MIDI_PROTOCOL);
    CHECK_AND_RETURN_RET_LOG(it != deviceInfo.end(), false, "protocol error");
    outInfo.nativeProtocol = static_cast<OH_MIDIProtocol>(StringToNum(it->second));

    it = deviceInfo.find(DEVICE_NAME);
    CHECK_AND_RETURN_RET_LOG(it != deviceInfo.end(), false, "deviceName error");
    CHECK_AND_RETURN_RET_LOG(
        strncpy_s(outInfo.deviceName, sizeof(outInfo.deviceName), it->second.c_str(), it->second.length()) ==
            OH_MIDI_STATUS_OK,
        false,
        "copy deviceName failed");

    it = deviceInfo.find(PRODUCT_ID);
    CHECK_AND_RETURN_RET_LOG(it != deviceInfo.end(), false, "productId error");
    outInfo.productId = StringToNum(it->second);

    it = deviceInfo.find(VENDOR_ID);
    CHECK_AND_RETURN_RET_LOG(it != deviceInfo.end(), false, "vendorId error");
    outInfo.vendorId = StringToNum(it->second);

    it = deviceInfo.find(ADDRESS);
    CHECK_AND_RETURN_RET_LOG(it != deviceInfo.end(), false, "deviceAddress error");
    CHECK_AND_RETURN_RET_LOG(
        strncpy_s(outInfo.deviceAddress, sizeof(outInfo.deviceAddress), it->second.c_str(), it->second.length()) ==
            OH_MIDI_STATUS_OK,
        false,
        "copy deviceAddress failed");

    return true;
}
MidiClientDeviceOpenCallback::MidiClientDeviceOpenCallback(std::shared_ptr<MidiServiceInterface> midiServiceInterface,
    OH_MIDIClient_OnDeviceOpened callback, void *userData)
    : ipc_(midiServiceInterface), callback_(callback), userData_(userData)
{
}

int32_t MidiClientDeviceOpenCallback::NotifyDeviceOpened(bool opened, const std::map<int32_t, std::string> &deviceInfo)
{
    CHECK_AND_RETURN_RET_LOG(callback_ != nullptr && ipc_.lock(), MIDI_STATUS_SYSTEM_ERROR, "callback_ is nullptr");
    OH_MIDIDeviceInformation info;
    if (!opened) {
        callback_(userData_, opened, nullptr, info);
        return 0;
    }
    bool ret = ConvertToDeviceInformation(deviceInfo, info);
    CHECK_AND_RETURN_RET_LOG(ret, MIDI_STATUS_SYSTEM_ERROR, "ConvertToDeviceInformation failed");
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

    outInfo.portIndex = static_cast<uint32_t>(StringToNum(it->second));
    it = portInfo.find(DIRECTION);
    CHECK_AND_RETURN_RET_LOG(it != portInfo.end(), false, "direction error");
    outInfo.direction = static_cast<OH_MIDIPortDirection>(StringToNum(it->second));

    it = portInfo.find(PORT_NAME);
    CHECK_AND_RETURN_RET_LOG(it != portInfo.end() && !it->second.empty(), false, "port name error");

    CHECK_AND_RETURN_RET_LOG(
        strncpy_s(outInfo.name, sizeof(outInfo.name), it->second.c_str(), it->second.length()) == OH_MIDI_STATUS_OK,
        false,
        "copy port name failed");
    return true;
}

static int32_t GetStatusCode(MidiStatusCode code)
{
    switch (code) {
        case MidiStatusCode::OK:
            return OH_MIDI_STATUS_OK;
        case MidiStatusCode::WOULD_BLOCK:
            return MIDI_STATUS_WOULD_BLOCK;
        default:
            return MIDI_STATUS_SYSTEM_ERROR;
    }
}

MidiClientCallback::MidiClientCallback(OH_MIDICallbacks callbacks, void *userData)
    : callbacks_(callbacks), userData_(userData)
{}

int32_t MidiClientCallback::NotifyDeviceChange(int32_t change, const std::map<int32_t, std::string> &deviceInfo)
{
    CHECK_AND_RETURN_RET_LOG(
        callbacks_.onDeviceChange != nullptr, MIDI_STATUS_SYSTEM_ERROR, "callbacks_.onDeviceChange is nullptr");

    OH_MIDIDeviceInformation info;
    bool ret = ConvertToDeviceInformation(deviceInfo, info);
    CHECK_AND_RETURN_RET_LOG(ret, MIDI_STATUS_SYSTEM_ERROR, "ConvertToDeviceInformation failed");

    callbacks_.onDeviceChange(userData_, static_cast<OH_MIDIDeviceChangeAction>(change), info);
    return 0;
}

int32_t MidiClientCallback::NotifyError(int32_t code)
{
    CHECK_AND_RETURN_RET_LOG(callbacks_.onError != nullptr, MIDI_STATUS_SYSTEM_ERROR, "callbacks_.onError is nullptr");
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
    CHECK_AND_RETURN_RET_LOG(ipc != nullptr, MIDI_STATUS_SYSTEM_ERROR, "ipc_ is nullptr");
    return ipc->CloseDevice(deviceId_);
}

OH_MIDIStatusCode MidiDevicePrivate::OpenInputPort(OH_MIDIPortDescriptor descriptor,
    OH_MIDIDevice_OnReceived callback, void *userData)
{
    std::lock_guard<std::mutex> lock(inputPortsMutex_);
    auto ipc = ipc_.lock();
    CHECK_AND_RETURN_RET_LOG(ipc != nullptr, MIDI_STATUS_SYSTEM_ERROR, "ipc_ is nullptr");

    auto iter = inputPortsMap_.find(descriptor.portIndex);
    CHECK_AND_RETURN_RET(iter == inputPortsMap_.end(), OH_MIDI_STATUS_PORT_ALREADY_OPEN);
    auto inputPort = std::make_shared<MidiInputPort>(callback, userData, descriptor.protocol);

    std::shared_ptr<MidiSharedRing> &buffer = inputPort->GetRingBuffer();
    auto ret = ipc->OpenInputPort(buffer, deviceId_, descriptor.portIndex);
    CHECK_AND_RETURN_RET_LOG(ret == OH_MIDI_STATUS_OK, ret, "open inputport fail");

    CHECK_AND_RETURN_RET_LOG(
        inputPort->StartReceiverThread() == true, MIDI_STATUS_SYSTEM_ERROR, "start receiver thread fail");

    inputPortsMap_.emplace(descriptor.portIndex, std::move(inputPort));
    MIDI_INFO_LOG("port[%{public}u] success", descriptor.portIndex);
    return OH_MIDI_STATUS_OK;
}

OH_MIDIStatusCode MidiDevicePrivate::OpenOutputPort(OH_MIDIPortDescriptor descriptor)
{
    std::lock_guard<std::mutex> lock(outputPortsMutex_);
    auto ipc = ipc_.lock();
    CHECK_AND_RETURN_RET_LOG(ipc != nullptr, MIDI_STATUS_SYSTEM_ERROR, "ipc_ is nullptr");

    auto iter = outputPortsMap_.find(descriptor.portIndex);
    CHECK_AND_RETURN_RET(iter == outputPortsMap_.end(), MIDI_STATUS_PORT_ALREADY_OPEN);

    auto outputPort = std::make_shared<MidiOutputPort>(descriptor.protocol);
    std::shared_ptr<MidiSharedRing> &buffer = outputPort->GetRingBuffer();
    auto ret = ipc->OpenOutputPort(buffer, deviceId_, descriptor.portIndex);
    CHECK_AND_RETURN_RET_LOG(ret == OH_MIDI_STATUS_OK, ret, "open outputport fail");

    outputPortsMap_.emplace(descriptor.portIndex, std::move(outputPort));
    MIDI_INFO_LOG("port[%{public}u] success", descriptor.portIndex);
    return OH_MIDI_STATUS_OK;
}

OH_MIDIStatusCode MidiDevicePrivate::Send(uint32_t portIndex, OH_MIDIEvent *events,
    uint32_t eventCount, uint32_t *eventsWritten)
{
    std::lock_guard<std::mutex> lock(outputPortsMutex_);
    auto iter = outputPortsMap_.find(portIndex);
    CHECK_AND_RETURN_RET_LOG(iter != outputPortsMap_.end(), OH_MIDI_STATUS_INVALID_PORT, "invalid port");
    auto outputPort = iter->second;
    return (OH_MIDIStatusCode)outputPort->Send(events, eventCount, eventsWritten);
}

OH_MIDIStatusCode MidiDevicePrivate::SendSysEx(uint32_t portIndex, uint8_t *data, uint32_t byteSize)
{
    std::lock_guard<std::mutex> lock(outputPortsMutex_);
    auto iter = outputPortsMap_.find(portIndex);
    CHECK_AND_RETURN_RET_LOG(iter != outputPortsMap_.end(), OH_MIDI_STATUS_INVALID_PORT, "invalid port");
    auto outputPort = iter->second;
    return (OH_MIDIStatusCode)outputPort->SendSysEx(portIndex, data, byteSize);
}

OH_MIDIStatusCode MidiDevicePrivate::FlushOutputPort(uint32_t portIndex)
{
    std::lock_guard<std::mutex> lock(outputPortsMutex_);
    auto ipc = ipc_.lock();
    auto iter = outputPortsMap_.find(portIndex);
    CHECK_AND_RETURN_RET_LOG(ipc != nullptr, MIDI_STATUS_SYSTEM_ERROR, "ipc_ is nullptr");
    CHECK_AND_RETURN_RET_LOG(iter != outputPortsMap_.end(), OH_MIDI_STATUS_INVALID_PORT, "invalid port");
    auto ret = ipc->FlushOutputPort(deviceId_, portIndex);
    CHECK_AND_RETURN_RET_LOG(ret == OH_MIDI_STATUS_OK, ret, "open outputport fail");
    return OH_MIDI_STATUS_OK;
}

OH_MIDIStatusCode MidiDevicePrivate::CloseInputPort(uint32_t portIndex)
{
    auto ipc = ipc_.lock();
    CHECK_AND_RETURN_RET_LOG(ipc != nullptr, MIDI_STATUS_SYSTEM_ERROR, "ipc_ is nullptr");

    std::lock_guard<std::mutex> lock(inputPortsMutex_);
    auto it = inputPortsMap_.find(portIndex);
    CHECK_AND_RETURN_RET_LOG(it != inputPortsMap_.end(), OH_MIDI_STATUS_INVALID_PORT, "invalid input port");

    auto ret = ipc->CloseInputPort(deviceId_, portIndex);
    CHECK_AND_RETURN_RET_LOG(ret == OH_MIDI_STATUS_OK, ret, "close input port fail");
    inputPortsMap_.erase(it);
    return OH_MIDI_STATUS_OK;
}

OH_MIDIStatusCode MidiDevicePrivate::CloseOutputPort(uint32_t portIndex)
{
    auto ipc = ipc_.lock();
    CHECK_AND_RETURN_RET_LOG(ipc != nullptr, MIDI_STATUS_SYSTEM_ERROR, "ipc_ is nullptr");

    std::lock_guard<std::mutex> lock(outputPortsMutex_);
    auto it = outputPortsMap_.find(portIndex);
    CHECK_AND_RETURN_RET_LOG(it != outputPortsMap_.end(), OH_MIDI_STATUS_INVALID_PORT, "invalid output port");

    auto ret = ipc->CloseOutputPort(deviceId_, portIndex);
    CHECK_AND_RETURN_RET_LOG(ret == OH_MIDI_STATUS_OK, ret, "close output port fail");
    outputPortsMap_.erase(it);
    return OH_MIDI_STATUS_OK;
}

MidiInputPort::MidiInputPort(OH_MIDIDevice_OnReceived callback, void *userData, OH_MIDIProtocol protocol)
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
    CHECK_AND_RETURN_RET_LOG(events && eventsWritten, OH_MIDI_STATUS_GENERIC_INVALID_ARGUMENT,
        "parameter is nullptr");
    CHECK_AND_RETURN_RET_LOG(eventCount > 0 && eventCount <= MAX_EVENTS_NUMS, OH_MIDI_STATUS_GENERIC_INVALID_ARGUMENT,
        "parameter is invalid");
    CHECK_AND_RETURN_RET_LOG(protocol_ == MIDI_PROTOCOL_1_0 || protocol_ == MIDI_PROTOCOL_2_0,
        OH_MIDI_STATUS_GENERIC_INVALID_ARGUMENT, "protocol is invalid");

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

void MidiOutputPort::PrepareSysExPackets(
    uint8_t group, uint8_t *data, uint32_t byteSize, uint32_t totalPkts,
    SysExPacketData &packetData)
{
    packetData.innerEvents.clear();
    packetData.payloadWords.clear();
    packetData.innerEvents.resize(totalPkts);
    packetData.payloadWords.resize(totalPkts);

    for (uint32_t i = 0; i < totalPkts; ++i) {
        const uint32_t offset = i * MAX_PACKET_BYTES;
        const uint32_t remain = byteSize - offset;
        const uint8_t nbytes = static_cast<uint8_t>(std::min<uint32_t>(MAX_PACKET_BYTES, remain));
        const uint8_t status = GetSysexStatus(i, totalPkts);

        packetData.payloadWords[i] = PackSysEx7Ump64(group, status, data + offset, nbytes);

        packetData.innerEvents[i] = MidiEventInner{0, 2, packetData.payloadWords[i].data()}; // 2 words
    }
}

int32_t MidiOutputPort::SendSysExPackets(const std::vector<MidiEventInner> &innerEvents,
    uint32_t pktCount, const std::chrono::steady_clock::time_point &start)
{
    uint32_t writtenInBatch = 0;

    while (writtenInBatch < pktCount) {
        if (std::chrono::steady_clock::now() - start > MAX_TIMEOUT_MS) {
            return MIDI_STATUS_TIMEOUT;
        }

        uint32_t writtenThis = 0;
        MidiStatusCode ret = ringBuffer_->TryWriteEvents(innerEvents.data() + writtenInBatch,
            pktCount - writtenInBatch, &writtenThis);

        writtenInBatch += writtenThis;

        if (writtenInBatch == pktCount) {
            break;
        }

        CHECK_AND_RETURN_RET(ret == MidiStatusCode::WOULD_BLOCK, GetStatusCode(ret));

        if (writtenThis == 0) {
            FutexCode wret = ringBuffer_->WaitForSpace(WAIT_SLICE_NS,
                sizeof(ShmMidiEventHeader) + SYSEX7_WORD_COUNT * sizeof(uint32_t));
            if (wret == FUTEX_TIMEOUT) {
                continue;
            }
            if (wret != FUTEX_SUCCESS) {
                return MIDI_STATUS_SYSTEM_ERROR;
            }
        }
        continue;
    }

    return OH_MIDI_STATUS_OK;
}

int32_t MidiOutputPort::SendSysEx(uint32_t portIndex, uint8_t *data, uint32_t byteSize)
{
    CHECK_AND_RETURN_RET_LOG(data, OH_MIDI_STATUS_GENERIC_INVALID_ARGUMENT, "parameter is nullptr");
    CHECK_AND_RETURN_RET_LOG(byteSize > 0, OH_MIDI_STATUS_GENERIC_INVALID_ARGUMENT, "byteSize is invalid");
    CHECK_AND_RETURN_RET_LOG(protocol_ == MIDI_PROTOCOL_1_0 || protocol_ == MIDI_PROTOCOL_2_0,
        OH_MIDI_STATUS_GENERIC_INVALID_ARGUMENT, "protocol is invalid");
    CHECK_AND_RETURN_RET_LOG(ringBuffer_ != nullptr, MIDI_STATUS_GENERIC_INVALID_ARGUMENT, "ringBuffer_ is nullptr");
    CHECK_AND_RETURN_RET_LOG(portIndex < PORT_GROUP_RANGE, MIDI_STATUS_INVALID_PORT, "portIndex out of range");

    const uint8_t group = static_cast<uint8_t>(portIndex & 0x0F);
    const uint32_t totalPkts = (byteSize + (MAX_PACKET_BYTES - 1)) / MAX_PACKET_BYTES;

    if (totalPkts == 0) {
        return OH_MIDI_STATUS_OK; // No data to send
    }

    SysExPacketData packetData;

    PrepareSysExPackets(group, data, byteSize, totalPkts, packetData);

    const auto start = std::chrono::steady_clock::now();

    return SendSysExPackets(packetData.innerEvents, totalPkts, start);
}

std::shared_ptr<MidiSharedRing> &MidiOutputPort::GetRingBuffer()
{
    return ringBuffer_;
}

MidiOutputPort::~MidiOutputPort()
{
    MIDI_INFO_LOG("OutputPort destroy");
}

MidiClientPrivate::MidiClientPrivate() : ipc_(std::make_shared<MidiServiceClient>()), clientId_(0)
{
    MIDI_INFO_LOG("MidiClientPrivate created");
}

MidiClientPrivate::~MidiClientPrivate()
{
    MIDI_INFO_LOG("MidiClientPrivate destroyed");
}

OH_MIDIStatusCode MidiClientPrivate::Init(OH_MIDICallbacks callbacks, void *userData)
{
    CHECK_AND_RETURN_RET_LOG(ipc_ != nullptr, MIDI_STATUS_SYSTEM_ERROR, "ipc_ is nullptr");
    callback_ = sptr<MidiClientCallback>::MakeSptr(callbacks, userData);
    auto ret = ipc_->Init(callback_, clientId_);
    CHECK_AND_RETURN_RET(ret == OH_MIDI_STATUS_OK, ret);
    return OH_MIDI_STATUS_OK;
}

OH_MIDIStatusCode MidiClientPrivate::GetDevices(OH_MIDIDeviceInformation *infos, size_t *numDevices)
{
    CHECK_AND_RETURN_RET_LOG(ipc_ != nullptr, MIDI_STATUS_SYSTEM_ERROR, "ipc_ is nullptr");

    std::vector<std::map<int32_t, std::string>> deviceInfos;
    auto ret = ipc_->GetDevices(deviceInfos);
    CHECK_AND_RETURN_RET(ret == OH_MIDI_STATUS_OK, ret);
    // Count query: return actual count
    if (infos == nullptr) {
        *numDevices = deviceInfos.size();
        return OH_MIDI_STATUS_OK;
    }
    // Silent fill mode for GetDeviceInfos
    size_t actualCount = std::min(*numDevices, deviceInfos.size());
    *numDevices = actualCount;
    CHECK_AND_RETURN_RET(actualCount != 0, OH_MIDI_STATUS_OK);
    for (size_t i = 0; i < actualCount; i++) {
        bool convRet = ConvertToDeviceInformation(deviceInfos[i], infos[i]);
        CHECK_AND_CONTINUE_LOG(convRet, "ConvertToDeviceInformation failed");
    }
    return OH_MIDI_STATUS_OK;
}

OH_MIDIStatusCode MidiClientPrivate::OpenDevice(int64_t deviceId, MidiDevice **midiDevice)
{
    CHECK_AND_RETURN_RET_LOG(midiDevice != nullptr, MIDI_STATUS_SYSTEM_ERROR, "midiDevice is nullptr");
    CHECK_AND_RETURN_RET_LOG(ipc_ != nullptr, MIDI_STATUS_SYSTEM_ERROR, "ipc_ is nullptr");
    auto ret = ipc_->OpenDevice(deviceId);
    CHECK_AND_RETURN_RET(ret == OH_MIDI_STATUS_OK, ret);
    auto newDevice = new MidiDevicePrivate(ipc_, deviceId);
    *midiDevice = newDevice;
    MIDI_INFO_LOG("Device opened: %{public}" PRId64, deviceId);
    return OH_MIDI_STATUS_OK;
}

OH_MIDIStatusCode MidiClientPrivate::OpenBleDevice(std::string address,
    OH_MIDIClient_OnDeviceOpened callback, void *userData)
{
    CHECK_AND_RETURN_RET_LOG(ipc_ != nullptr, MIDI_STATUS_SYSTEM_ERROR, "ipc_ is nullptr");
    auto deivceOpenCallback = sptr<MidiClientDeviceOpenCallback>::MakeSptr(ipc_, callback, userData);
    auto ret = ipc_->OpenBleDevice(address, deivceOpenCallback);
    return ret;
}

OH_MIDIStatusCode MidiClientPrivate::GetDevicePorts(int64_t deviceId, OH_MIDIPortInformation *infos, size_t *numPorts)
{
    std::vector<std::map<int32_t, std::string>> portInfos;
    CHECK_AND_RETURN_RET_LOG(ipc_ != nullptr, MIDI_STATUS_SYSTEM_ERROR, "ipc_ is nullptr");
    auto ret = ipc_->GetDevicePorts(deviceId, portInfos);
    CHECK_AND_RETURN_RET(ret == OH_MIDI_STATUS_OK, ret);
    // Count query: return actual count
    if (infos == nullptr) {
        *numPorts = portInfos.size();
        return OH_MIDI_STATUS_OK;
    }
    // Silent fill mode for GetPortInfos
    size_t actualCount = std::min(*numPorts, portInfos.size());
    *numPorts = actualCount;
    CHECK_AND_RETURN_RET(actualCount != 0, OH_MIDI_STATUS_OK);

    for (size_t i = 0; i < actualCount; i++) {
        OH_MIDIPortInformation info;
        bool ret = ConvertToPortInformation(portInfos[i], deviceId, info);
        CHECK_AND_CONTINUE_LOG(ret, "ConvertToPortInformation failed");
        infos[i] = info;
    }

    return OH_MIDI_STATUS_OK;
}

OH_MIDIStatusCode MidiClientPrivate::DestroyMidiClient()
{
    CHECK_AND_RETURN_RET_LOG(ipc_ != nullptr, MIDI_STATUS_SYSTEM_ERROR, "ipc_ is nullptr");
    return ipc_->DestroyMidiClient();
}

OH_MIDIStatusCode MidiClient::CreateMidiClient(MidiClient **client, OH_MIDICallbacks callbacks, void *userData)
{
    CHECK_AND_RETURN_RET_LOG(client != nullptr, MIDI_STATUS_SYSTEM_ERROR, "client is nullptr");
    *client = new MidiClientPrivate();
    OH_MIDIStatusCode ret = (*client)->Init(callbacks, userData);
    CHECK_AND_RETURN_RET(ret != OH_MIDI_STATUS_OK, ret);
    delete *client;
    return ret;
}
}  // namespace MIDI
}  // namespace OHOS