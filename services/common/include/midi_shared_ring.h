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

#ifndef MIDI_SHARED_RING_H
#define MIDI_SHARED_RING_H

#include <cstdint>
#include <cstddef>
#include <atomic>
#include <vector>
#include <new>

#include "midi_info.h"
#include "futex_tool.h"
#include "midi_shared_memory.h"

namespace OHOS {
namespace MIDI {

enum class MidiStatusCode : int32_t { OK = 0, WOULD_BLOCK, INVALID_ARGUMENT, SHM_BROKEN, INTERNAL_ERROR };

struct alignas(64) ControlHeader {        // 64 bytes for cache line
    std::atomic<uint32_t> readPosition;   // read index range: (0..capacity-1)
    std::atomic<uint32_t> writePosition;  // write index range: (0..capacity-1)
    uint32_t capacity;                    // ring data capacity
    std::atomic<uint32_t> futexObj;       // for futex
    uint32_t flags;                       // for expand
};

enum ShmEventFlags : uint32_t {
    SHM_EVENT_FLAG_NONE = 0,
    SHM_EVENT_FLAG_WRAP = 1u << 0,  // indicate wrap, length must be 0
};

struct ShmMidiEventHeader {
    uint64_t timestamp;
    uint32_t length;
    uint32_t flags;
};

class SharedMidiRing : public Parcelable {
public:
    explicit SharedMidiRing(uint32_t ringCapacityBytes);

    // creat SharedMidiRing locally or remotely
    static std::shared_ptr<SharedMidiRing> CreateFromLocal(
        size_t ringCapacityBytes);  // todo 客户端持有，mididevice持有
    static std::shared_ptr<SharedMidiRing> CreateFromRemote(size_t ringCapacityBytes, int dataFd);

    // idl
    bool Marshalling(Parcel &parcel) const override;
    static SharedMidiRing *Unmarshalling(Parcel &parcel);

    int32_t Init(int dataFd);
    SharedMidiRing(const SharedMidiRing &) = delete;
    SharedMidiRing &operator=(const SharedMidiRing &) = delete;
    ~SharedMidiRing() = default;

    uint32_t GetCapacity() const;
    uint32_t GetReadPosition() const;
    uint32_t GetWritePosition() const;
    uint8_t *GetDataBase();
    std::atomic<uint32_t> *GetFutex();

    FutexCode WaitFor(int64_t timeoutInNs, const std::function<bool(void)> &pred);
    void NotifyConsumer(uint32_t wakeVal = IS_READY);
    bool IsEmpty() const;
    ControlHeader *GetControlHeader();
    MidiStatusCode TryWriteEvents(
        const MidiEventInner *events, uint32_t eventCount, uint32_t *eventsWritten, bool notify = true);
    MidiStatusCode TryWriteEvent(const MidiEventInner &event, bool notify = true);

    struct PeekedEvent {
        const ShmMidiEventHeader *headerPtr = nullptr;
        const uint8_t *payloadPtr = nullptr;

        uint64_t timestamp = 0;
        uint32_t length = 0;
        uint32_t beginOffset = 0;  // header
        uint32_t endOffset = 0;    // header + payload range[0, capacity]
    };

    MidiStatusCode PeekNext(PeekedEvent &outEvent);

    void CommitRead(const PeekedEvent &event);
    void DrainToBatch(std::vector<MidiEvent> &outEvents, std::vector<std::vector<uint32_t>> &outPayloadBuffers,
        uint32_t maxEvents = 0);

private:
    bool ValidateOneEvent(const MidiEventInner &event) const;
    void WakeFutex(uint32_t wakeVal = IS_READY);
    void WriteEvent(uint32_t writeIndex, const MidiEventInner &event);
    MidiStatusCode ValidateWriteArgs(const MidiEventInner *events, uint32_t eventCount) const;
    MidiStatusCode TryWriteOneEvent(
        const MidiEventInner &event, uint32_t length, uint32_t readIndex, uint32_t &writeIndex);
    bool UpdateWriteIndexIfNeed(uint32_t &writeIndex, uint32_t needed);
    MidiStatusCode UpdateReadIndexIfNeed(uint32_t &readIndex, uint32_t writeIndex);
    MidiStatusCode HandleWrapIfNeeded(const ShmMidiEventHeader &hdr, uint32_t &r);
    MidiStatusCode BuildPeekedEvent(const ShmMidiEventHeader &hdr, uint32_t readIndex, PeekedEvent &outEvent);
    MidiEvent CopyOut(const PeekedEvent &peekedEvent, std::vector<uint32_t> &outPayloadBuffer) const;

    uint8_t *base_{nullptr};
    ControlHeader *controler_{nullptr};
    uint8_t *ringBase_{nullptr};
    uint32_t capacity_{0};
    uint32_t totalMemorySize_{0};
    mutable std::shared_ptr<MidiSharedMemory> dataMem_ = nullptr;
};
}  // namespace MIDI
}  // namespace OHOS
#endif