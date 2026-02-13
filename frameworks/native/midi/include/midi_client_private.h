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
#ifndef MIDI_CLIENT_PRIVATE_H
#define MIDI_CLIENT_PRIVATE_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include "midi_client.h"
#include "midi_service_interface.h"
#include "midi_shared_ring.h"
#include "midi_callback_stub.h"
#include "midi_device_open_callback_stub.h"
namespace OHOS {
namespace MIDI {

struct SysExPacketData {
    std::vector<MidiEventInner> innerEvents;
    std::vector<std::array<uint32_t, SYSEX7_WORD_COUNT >> payloadWords;
};

class MidiClientCallback;
class MidiClientDeviceOpenCallback : public MidiDeviceOpenCallbackStub {
public:
    MidiClientDeviceOpenCallback(std::shared_ptr<MidiServiceInterface> midiServiceInterface,
        OH_MIDIClient_OnDeviceOpened callback, void *userData);
    ~MidiClientDeviceOpenCallback() = default;
    int32_t NotifyDeviceOpened(bool opened, const std::map<int32_t, std::string> &deviceInfo) override;
private:
    std::weak_ptr<MidiServiceInterface> ipc_;
    OH_MIDIClient_OnDeviceOpened callback_;
    void *userData_;
};
    
class MidiInputPort {
public:
    MidiInputPort(OH_MIDIDevice_OnReceived callback, void *userData, OH_MIDIProtocol protocol);
    ~MidiInputPort();
    std::shared_ptr<MidiSharedRing> &GetRingBuffer();

    bool StartReceiverThread();
    bool StopReceiverThread();

private:
    void ReceiverThreadLoop();

    void DrainRingAndDispatch();

    bool ShouldWakeForReadOrExit() const;

    std::atomic<bool> running_ = false;
    OH_MIDIDevice_OnReceived callback_ = nullptr;
    std::shared_ptr<MidiSharedRing> ringBuffer_ = nullptr;
    std::thread receiverThread_;
    void *userData_ = nullptr;
    OH_MIDIProtocol protocol_;
};

class MidiOutputPort {
public:
    MidiOutputPort(OH_MIDIProtocol protocol);
    ~MidiOutputPort();
    int32_t Send(OH_MIDIEvent *events, uint32_t eventCount, uint32_t *eventsWritten);
    int32_t SendSysEx(uint32_t portIndex, uint8_t *data, uint32_t byteSize);
    std::shared_ptr<MidiSharedRing> &GetRingBuffer();
private:
    void PrepareSysExPackets(uint8_t group, uint8_t *data, uint32_t byteSize, uint32_t totalPkts,
            SysExPacketData &packetData);
    int32_t SendSysExPackets(const std::vector<MidiEventInner> &innerEvents, uint32_t pktCount,
            const std::chrono::steady_clock::time_point &start);

    std::shared_ptr<MidiSharedRing> ringBuffer_ = nullptr;
    OH_MIDIProtocol protocol_;
};

class MidiDevicePrivate : public MidiDevice {
public:
    MidiDevicePrivate(std::shared_ptr<MidiServiceInterface> midiServiceInterface, int64_t deviceId);
    virtual ~MidiDevicePrivate();
    OH_MIDIStatusCode CloseDevice() override;
    OH_MIDIStatusCode OpenInputPort(OH_MIDIPortDescriptor descriptor,
                                    OH_MIDIDevice_OnReceived callback, void *userData) override;
    OH_MIDIStatusCode OpenOutputPort(OH_MIDIPortDescriptor descriptor) override;
    OH_MIDIStatusCode ClosePort(uint32_t portIndex) override;
    OH_MIDIStatusCode Send(uint32_t portIndex, OH_MIDIEvent *events,
                            uint32_t eventCount, uint32_t *eventsWritten) override;
    OH_MIDIStatusCode SendSysEx(uint32_t portIndex, uint8_t *data, uint32_t byteSize) override;
    OH_MIDIStatusCode FlushOutputPort(uint32_t portIndex) override;

private:
    std::weak_ptr<MidiServiceInterface> ipc_;
    int64_t deviceId_;
    std::mutex inputPortsMutex_;
    std::mutex outputPortsMutex_;
    std::unordered_map<uint32_t, std::shared_ptr<MidiInputPort>> inputPortsMap_;
    std::unordered_map<uint32_t, std::shared_ptr<MidiOutputPort>> outputPortsMap_;
};

class MidiClientPrivate : public MidiClient {
public:
    MidiClientPrivate();
    virtual ~MidiClientPrivate();
    OH_MIDIStatusCode Init(OH_MIDICallbacks callbacks, void *userData) override;
    OH_MIDIStatusCode GetDevices(OH_MIDIDeviceInformation *infos, size_t *numDevices) override;
    OH_MIDIStatusCode OpenDevice(int64_t deviceId, MidiDevice **midiDevice) override;
    OH_MIDIStatusCode OpenBleDevice(std::string address, OH_MIDIClient_OnDeviceOpened callback, void *userData) override;
    OH_MIDIStatusCode GetDevicePorts(int64_t deviceId, OH_MIDIPortInformation *infos, size_t *numPorts) override;
    OH_MIDIStatusCode DestroyMidiClient() override;
private:
    void DeviceChange(OH_MIDIDeviceChangeAction change, OH_MIDIDeviceInformation info);
    std::shared_ptr<MidiServiceInterface> ipc_;
    uint32_t clientId_;
    std::vector<OH_MIDIDeviceInformation> deviceInfos_;
    sptr<MidiClientCallback> callback_;
    std::mutex mutex_;
};
} // namespace MIDI
} // namespace OHOS
#endif
