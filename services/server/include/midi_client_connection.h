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

#ifndef MIDI_CLIENT_CONNECTION_H
#define MIDI_CLIENT_CONNECTION_H

#include <vector>
#include <chrono>
#include <queue>

#include "midi_shared_ring.h"
namespace OHOS {
namespace MIDI {

namespace {
    const uint32_t DEFAULT_RING_BUFFER_SIZE = 2048;
}


class ClientConnectionInServer {
public:
    struct PendingEvent {
        std::chrono::steady_clock::time_point due;
        std::vector<uint8_t> data;
        uint64_t timestamp = 0;
    };
    struct PendingGreater {
        bool operator()(const PendingEvent& a, const PendingEvent& b) const
        {
            return a.due > b.due;
        }
    };

public:
    ClientConnectionInServer(uint32_t clientId, int64_t handle, uint32_t portIndex)
        : clientId_(clientId), deviceHandle_(handle), portIndex_(portIndex) {}
    ~ClientConnectionInServer() = default;

    int32_t CreateRingBuffer();

    int64_t GetDeviceHandle() const { return deviceHandle_; }
    uint32_t GetClientId() const { return clientId_; }
    int64_t GetPortIndex() const { return portIndex_; }

    std::shared_ptr<MidiSharedRing> GetRingBuffer();

    int32_t TrySendToClient(const MidiEventInner& event);

    void SetMaxPending(size_t maxPending) { maxPending_ = maxPending; }
    bool IsPendingFull() const { return pending_.size() >= maxPending_; }
    bool HasPending() const { return !pending_.empty(); }
    bool EnqueueNonRealtime(const uint8_t* payload, size_t len,
                            const std::chrono::steady_clock::time_point& due,
                            uint64_t ts);
    const PendingEvent* PeekPendingTop() const;
    bool PopPendingTop(PendingEvent& out);

private:
    uint32_t clientId_ = 0;
    int64_t deviceHandle_ = -1;
    uint32_t portIndex_ = -1;

    std::shared_ptr<MidiSharedRing> sharedRingBuffer_ = nullptr;

    size_t maxPending_ = 1024;
    std::priority_queue<PendingEvent, std::vector<PendingEvent>, PendingGreater> pending_;
};
} // namespace MIDI
} // namespace OHOS
#endif