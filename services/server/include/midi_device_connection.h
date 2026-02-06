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

#ifndef MIDI_DEVICE_CONNECTION_H
#define MIDI_DEVICE_CONNECTION_H

#include <vector>
#include <memory>
#include <thread>

#include "midi_device_driver.h"
#include "midi_client_connection.h"

namespace OHOS {
namespace MIDI {
namespace {
static constexpr size_t MAX_PENDING_EVENTS = 4096;
}

enum class MidiPortDirection : uint32_t { INPUT = 0, OUTPUT = 1 };

struct DeviceConnectionInfo {
    MidiDeviceDriver *driver = nullptr;
    int64_t deviceId = 0;
    MidiPortDirection direction;
    uint32_t portIndex;
};

class DeviceConnectionBase {
public:
    explicit DeviceConnectionBase(DeviceConnectionInfo info);
    virtual ~DeviceConnectionBase() = default;

    DeviceConnectionBase(const DeviceConnectionBase &) = delete;
    DeviceConnectionBase &operator=(const DeviceConnectionBase &) = delete;

    const DeviceConnectionInfo &GetInfo() const { return info_; }

    virtual int32_t AddClientConnection(uint32_t clientId, int64_t deviceHandle,
                                        std::shared_ptr<MidiSharedRing> &buffer);
    virtual void RemoveClientConnection(uint32_t clientId);
    virtual bool IsEmptyClientConnections();
    virtual bool HasClientConnection(uint32_t clientId) const;

protected:
    std::vector<std::shared_ptr<ClientConnectionInServer>> SnapshotClients() const;

protected:
    DeviceConnectionInfo info_;
    mutable std::mutex clientsMutex_;
    std::vector<std::shared_ptr<ClientConnectionInServer>> clients_;
};

class DeviceConnectionForInput final : public DeviceConnectionBase {
public:
    explicit DeviceConnectionForInput(DeviceConnectionInfo info);
    ~DeviceConnectionForInput() override = default;

    void HandleDeviceUmpInput(std::vector<MidiEventInner> &events);

private:
    void BroadcastToClients(const MidiEventInner &ev);
};

class DeviceConnectionForOutput final : public DeviceConnectionBase {
public:
    explicit DeviceConnectionForOutput(DeviceConnectionInfo info);
    ~DeviceConnectionForOutput() override;

    // to manage the thread
    int32_t Start();
    int32_t Stop();

    int GetNotifyEventFdForClients() const;
    int32_t AddClientConnection(uint32_t clientId, int64_t deviceHandle,
                                        std::shared_ptr<MidiSharedRing> &buffer) override;

    // todo: maybe not needed
    void SetPerClientMaxPendingEvents(size_t maxPendingEvents);
    void SetMaxSendCacheBytes(size_t maxSendCacheBytes);
    
private:
    // worker loop
    void ThreadMain();
    void HandleWakeupOnce();

    void DrainAllClientsRings();
    void DrainSingleClientRing(ClientConnectionInServer &clientConnection);
    bool ConsumeRealtimeEvent(MidiSharedRing &clientRing, const MidiSharedRing::PeekedEvent &ringEvent);
    bool ConsumeNonRealtimeEvent(ClientConnectionInServer &clientConnection, MidiSharedRing &clientRing,
                                 const MidiSharedRing::PeekedEvent &ringEvent);

    // todo: due + (1 or 2)ms <= now
    void CollectDueEventsFromClientHeaps();

    // Step3：flush cache -> driver
    void FlushSendCacheToDriver();

    // Step4：timerfd set earliest due
    void UpdateNextTimer();

    std::shared_ptr<ClientConnectionInServer>
    FindClientWithEarliestDue(const std::vector<std::shared_ptr<ClientConnectionInServer>> &clientsSnapshot,
                              std::chrono::steady_clock::time_point &outEarliestDueTime);

    // send cache helper
    bool TryAppendToSendCache(uint64_t timestamp,
                              const uint32_t* payloadWords,
                              size_t payloadWordCount);
    void SendToDriver(MidiEventInner event);

    // fd/epoll helper
    int32_t InitEpollAndFds();
    void DrainEventFd();
    void DrainTimerFd();
    void WakeWorkerByEventFd();

    struct SendItem {
        std::vector<uint8_t> data;
    };

    std::atomic<bool> running_{false};
    std::thread worker_;

    UniqueFd notifyEventFd_; // eventfd: clients -> server notify
    UniqueFd epollFd_;       // epoll: wait eventfd + timerfd
    UniqueFd timerFd_;       // timerfd: pending earliest due

    size_t maxSendCacheBytes_ = 64 * 1024;
    size_t currentSendCacheBytes_ = 0;
    std::vector<MidiEventInner> sendCache_;
    std::vector<std::vector<uint32_t>> sendCachePayloadBuffers_; // for payload

    size_t perClientMaxPendingEvents_ = 1024;

    static constexpr uint64_t kEpollTagNotifyEventFd = 1;
    static constexpr uint64_t kEpollTagTimerFd = 2;
};
} // namespace MIDI
} // namespace OHOS
#endif