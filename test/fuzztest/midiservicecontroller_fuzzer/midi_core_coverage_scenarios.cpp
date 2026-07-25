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

#include "midi_core_coverage_scenarios.h"

#include <array>
#include <chrono>
#include <fcntl.h>
#include <fuzzer/FuzzedDataProvider.h>
#include <limits>
#include <memory>
#include <sys/eventfd.h>
#include <unistd.h>
#include <utility>
#include <vector>

#include "message_parcel.h"
#include "midi_client_connection.h"
#include "midi_device_connection.h"
#include "midi_shared_memory.h"
#include "midi_shared_ring.h"
#include "midi_utils.h"
#include "native_midi_base.h"

namespace OHOS {
namespace MIDI {
namespace {
constexpr uint32_t RING_CAPACITY = 256;
constexpr uint32_t WRAP_RING_CAPACITY = 128;
constexpr uint32_t WRAP_READ_POSITION = 64;
constexpr size_t MAX_PAYLOAD_WORDS = 32;
constexpr size_t WRAP_PAYLOAD_WORDS = 19;
constexpr uint32_t INVALID_RING_CAPACITY = 0x2000;
constexpr size_t ENCRYPT_INPUT_LENGTH = 32;
constexpr size_t SHARED_MEMORY_SIZE = 128;
constexpr size_t CORRUPTED_WRITE_POSITION_HEADER_COUNT = 2;
constexpr uint32_t CORRUPTED_WRAP_SEQUENCE = 13;
constexpr uint32_t CLIENT_ID = 1;
constexpr int64_t CLIENT_DEVICE_HANDLE = 2;
constexpr uint32_t CLIENT_PORT_INDEX = 3;
constexpr uint32_t FIRST_CLIENT_ID = 11;
constexpr uint32_t SECOND_CLIENT_ID = 12;
constexpr uint32_t MISSING_CLIENT_ID = 99;
constexpr int64_t FIRST_DEVICE_HANDLE = 21;
constexpr int64_t SECOND_DEVICE_HANDLE = 22;

std::vector<uint32_t> ConsumePayload(FuzzedDataProvider &fdp)
{
    size_t count = fdp.ConsumeIntegralInRange<size_t>(1, MAX_PAYLOAD_WORDS);
    std::vector<uint32_t> payload(count);
    for (auto &word : payload) {
        word = fdp.ConsumeIntegral<uint32_t>();
    }
    return payload;
}

MidiEventInner MakeEvent(uint64_t timestamp, const std::vector<uint32_t> &payload)
{
    MidiEventInner event{};
    event.timestamp = timestamp;
    event.length = payload.size();
    event.data = payload.data();
    return event;
}

void ExerciseMidiUtils(FuzzedDataProvider &fdp)
{
    auto payload = ConsumePayload(fdp);
    uint64_t timestamp = fdp.ConsumeIntegral<uint64_t>();
    (void)ClockTime::GetCurNano();
    (void)GetEncryptStr("");
    (void)GetEncryptStr(fdp.ConsumeRandomLengthString(ENCRYPT_INPUT_LENGTH));
    (void)GetEncryptStr("AA:BB:CC:DD:EE:FF");
    (void)BytesToString(fdp.ConsumeIntegral<uint32_t>());
    (void)DumpOneEvent(timestamp, 0, nullptr);
    (void)DumpOneEvent(timestamp, payload.size(), nullptr);
    (void)DumpOneEvent(timestamp, payload.size(), payload.data());

    MidiEvent publicEvent{timestamp, payload.size(), payload.data()};
    MidiEventInner innerEvent = MakeEvent(timestamp, payload);
    (void)DumpMidiEvents(std::vector<MidiEvent>{publicEvent});
    (void)DumpMidiEvents(std::vector<MidiEventInner>{innerEvent});

    std::array<uint8_t, MAX_PACKET_BYTES> bytes{};
    for (auto &byte : bytes) {
        byte = fdp.ConsumeIntegral<uint8_t>();
    }
    (void)PackSysEx7Ump64(fdp.ConsumeIntegral<uint8_t>(), fdp.ConsumeIntegral<uint8_t>(), bytes.data(),
                          static_cast<uint8_t>(bytes.size()));

    UniqueFd first(::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC));
    UniqueFd second(std::move(first));
    UniqueFd third(-1);
    third = std::move(second);
    third.Reset();
}

void ExerciseSharedMemory()
{
    (void)MidiSharedMemory::CreateFromLocal(0, "midi_fuzz_invalid");
    (void)MidiSharedMemory::CreateFromRemote(STDERR_FILENO, SHARED_MEMORY_SIZE, "invalid_remote");

    MessageParcel emptyParcel;
    delete MidiSharedMemory::Unmarshalling(emptyParcel);

    auto memory = MidiSharedMemory::CreateFromLocal(SHARED_MEMORY_SIZE, "midi_fuzz_memory");
    if (!memory) {
        return;
    }
    (void)memory->GetBase();
    (void)memory->GetSize();
    (void)memory->GetFd();
    (void)memory->GetName();

    MessageParcel parcel;
    if (memory->Marshalling(parcel)) {
        delete MidiSharedMemory::Unmarshalling(parcel);
    }
}

void ExerciseRingParcelRoundTrip()
{
    (void)MidiSharedRing::CreateFromRemote(RING_CAPACITY, STDERR_FILENO);

    MessageParcel emptyParcel;
    delete MidiSharedRing::Unmarshalling(emptyParcel);

    auto inputRing = MidiSharedRing::CreateFromLocal(RING_CAPACITY);
    if (inputRing) {
        MessageParcel inputParcel;
        if (inputRing->Marshalling(inputParcel)) {
            delete MidiSharedRing::Unmarshalling(inputParcel);
        }
    }

    auto eventFd = std::make_shared<UniqueFd>(::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC));
    if (!eventFd->Valid()) {
        return;
    }
    auto outputRing = MidiSharedRing::CreateFromLocal(RING_CAPACITY, eventFd);
    if (outputRing) {
        (void)outputRing->GetEventFd();
        MessageParcel outputParcel;
        if (outputRing->Marshalling(outputParcel)) {
            delete MidiSharedRing::Unmarshalling(outputParcel);
        }
    }
}

void ExerciseRingControlGuards(MidiSharedRing &ring, const MidiEventInner &event)
{
    auto *control = ring.GetControlHeader();
    if (!control) {
        return;
    }
    ring.Flush();
    control->writePosition.store(ring.GetCapacity());
    (void)ring.TryWriteEvent(event, false);
    control->writePosition.store(0);
    control->readPosition.store(ring.GetCapacity());
    (void)ring.TryWriteEvent(event, false);
    ring.Flush();

    MidiSharedRing::PeekedEvent peeked{};
    control->writePosition.store(sizeof(ShmMidiEventHeader) * CORRUPTED_WRITE_POSITION_HEADER_COUNT);
    (void)ring.PeekNext(peeked);
    ring.Flush();
}

void ExerciseRingValidation(FuzzedDataProvider &fdp)
{
    MidiSharedRing zeroRing(0);
    if (zeroRing.Init(-1) == OH_MIDI_STATUS_OK) {
        std::vector<uint32_t> payload{0};
        MidiEventInner event = MakeEvent(0, payload);
        uint32_t written = 0;
        (void)zeroRing.TryWriteEvents(&event, 1, &written, false);
        MidiSharedRing::PeekedEvent peeked{};
        (void)zeroRing.PeekNext(peeked);
    }

    MidiSharedRing oversizedRing(INVALID_RING_CAPACITY);
    (void)oversizedRing.Init(-1);

    MidiSharedRing ring(RING_CAPACITY);
    if (ring.Init(-1) != OH_MIDI_STATUS_OK) {
        return;
    }
    (void)ring.GetCapacity();
    (void)ring.GetReadPosition();
    (void)ring.GetWritePosition();
    (void)ring.GetDataBase();
    (void)ring.GetFutex();
    (void)ring.GetEventFd();
    (void)ring.IsEmpty();

    uint32_t written = 1;
    (void)ring.TryWriteEvents(nullptr, 0, &written, false);
    (void)ring.TryWriteEvents(nullptr, 1, &written, false);

    MidiEventInner invalidEvent{};
    invalidEvent.length = 1;
    (void)ring.TryWriteEvents(&invalidEvent, 1, &written, false);

    uint32_t dummyWord = 0;
    MidiEventInner oversizedEvent{};
    oversizedEvent.length = std::numeric_limits<size_t>::max();
    oversizedEvent.data = &dummyWord;
    (void)ring.TryWriteEvents(&oversizedEvent, 1, &written, false);

    auto payload = ConsumePayload(fdp);
    MidiEventInner event = MakeEvent(fdp.ConsumeIntegral<uint64_t>(), payload);
    (void)ring.TryWriteEvent(event, false);
    MidiSharedRing::PeekedEvent peeked{};
    if (ring.PeekNext(peeked) == MidiStatusCode::OK) {
        std::vector<uint32_t> copiedPayload;
        (void)ring.CopyOut(peeked, copiedPayload);
    }
    ExerciseRingControlGuards(ring, event);
}

void ExerciseRingSequenceGuards()
{
    MidiSharedRing ring(RING_CAPACITY);
    if (ring.Init(-1) != OH_MIDI_STATUS_OK) {
        return;
    }

    std::vector<uint32_t> payload{0xCAFE};
    MidiEventInner event = MakeEvent(42, payload);
    uint32_t written = 0;
    if (ring.TryWriteEvents(&event, 1, &written, false) != MidiStatusCode::OK) {
        return;
    }
    auto *header = reinterpret_cast<ShmMidiEventHeader *>(ring.GetDataBase());
    auto *control = ring.GetControlHeader();
    if (!header || !control) {
        return;
    }

    uint32_t sequence = header->sequence.load(std::memory_order_relaxed);
    header->sequence.store(sequence | 1u, std::memory_order_relaxed);
    MidiSharedRing::PeekedEvent peeked{};
    (void)ring.PeekNext(peeked);
    header->sequence.store(sequence + 2u, std::memory_order_relaxed);
    if (ring.PeekNext(peeked) == MidiStatusCode::OK) {
        std::vector<uint32_t> copiedPayload;
        (void)ring.CopyOut(peeked, copiedPayload);
    }

    header->length = std::numeric_limits<uint32_t>::max();
    (void)ring.PeekNext(peeked);
    ring.Flush();

    control->writePosition.store(sizeof(ShmMidiEventHeader) * CORRUPTED_WRITE_POSITION_HEADER_COUNT);
    (void)ring.PeekNext(peeked);
    ring.Flush();
}

void ExerciseRingWrap()
{
    MidiSharedRing ring(WRAP_RING_CAPACITY);
    if (ring.Init(-1) != OH_MIDI_STATUS_OK) {
        return;
    }

    std::vector<uint32_t> firstPayload(WRAP_PAYLOAD_WORDS, 0x11);
    MidiEventInner firstEvent = MakeEvent(10, firstPayload);
    uint32_t written = 0;
    if (ring.TryWriteEvents(&firstEvent, 1, &written, false) != MidiStatusCode::OK) {
        return;
    }
    uint32_t wrapOffset = ring.GetWritePosition();
    auto *control = ring.GetControlHeader();
    auto *base = ring.GetDataBase();
    if (!control || !base) {
        return;
    }

    auto *wrapHeader = reinterpret_cast<ShmMidiEventHeader *>(base + wrapOffset);
    wrapHeader->sequence.store(CORRUPTED_WRAP_SEQUENCE, std::memory_order_relaxed);
    control->readPosition.store(WRAP_READ_POSITION);
    std::vector<uint32_t> secondPayload{0x22, 0x33};
    MidiEventInner secondEvent = MakeEvent(20, secondPayload);
    (void)ring.TryWriteEvents(&secondEvent, 1, &written, false);

    control->readPosition.store(wrapOffset);
    MidiSharedRing::PeekedEvent peeked{};
    (void)ring.PeekNext(peeked);

    wrapHeader->length = 1;
    control->readPosition.store(wrapOffset);
    (void)ring.PeekNext(peeked);
}

void ExerciseSharedRing(FuzzedDataProvider &fdp)
{
    ExerciseRingParcelRoundTrip();
    ExerciseRingValidation(fdp);
    ExerciseRingSequenceGuards();
    ExerciseRingWrap();
}

void FillRingWithoutNotification(const std::shared_ptr<MidiSharedRing> &ring, const MidiEventInner &event)
{
    if (!ring) {
        return;
    }
    for (uint32_t attempt = 0; attempt < ring->GetCapacity(); ++attempt) {
        if (ring->TryWriteEvent(event, false) != MidiStatusCode::OK) {
            break;
        }
    }
}

void ExerciseClientConnection(FuzzedDataProvider &fdp)
{
    ClientConnectionInServer client(CLIENT_ID, CLIENT_DEVICE_HANDLE, CLIENT_PORT_INDEX);
    (void)client.GetClientId();
    (void)client.GetDeviceHandle();
    (void)client.GetPortIndex();
    (void)client.GetRingBuffer();
    if (client.CreateRingBuffer() != OH_MIDI_STATUS_OK) {
        return;
    }

    auto payload = ConsumePayload(fdp);
    MidiEventInner event = MakeEvent(fdp.ConsumeIntegral<uint64_t>(), payload);
    FillRingWithoutNotification(client.GetRingBuffer(), event);
    (void)client.TrySendToClient(event);

    client.SetMaxPending(1);
    (void)client.IsPendingFull();
    (void)client.HasPending();
    auto now = std::chrono::steady_clock::now();
    (void)client.EnqueueNonRealtime(std::vector<uint32_t>(payload), now, event.timestamp);
    (void)client.EnqueueNonRealtime(std::vector<uint32_t>(payload), now, event.timestamp);
    (void)client.PeekPendingTop();
    ClientConnectionInServer::PendingEvent pending;
    (void)client.PopPendingTop(pending);
    (void)client.PopPendingTop(pending);
    client.Flush();
}

void ExerciseInputConnection(FuzzedDataProvider &fdp)
{
    DeviceConnectionInfo info{nullptr, 7, MidiPortDirection::INPUT, 0};
    DeviceConnectionForInput connection(info);
    (void)connection.GetInfo();
    (void)connection.IsEmptyClientConnections();

    std::shared_ptr<MidiSharedRing> firstRing;
    std::shared_ptr<MidiSharedRing> secondRing;
    (void)connection.AddClientConnection(FIRST_CLIENT_ID, FIRST_DEVICE_HANDLE, firstRing);
    (void)connection.AddClientConnection(SECOND_CLIENT_ID, SECOND_DEVICE_HANDLE, secondRing);
    (void)connection.HasClientConnection(FIRST_CLIENT_ID);
    (void)connection.HasClientConnection(MISSING_CLIENT_ID);
    (void)connection.GetConnectedClientIds();

    auto firstPayload = ConsumePayload(fdp);
    auto secondPayload = ConsumePayload(fdp);
    MidiEventInner firstEvent = MakeEvent(fdp.ConsumeIntegral<uint64_t>(), firstPayload);
    MidiEventInner secondEvent = MakeEvent(fdp.ConsumeIntegral<uint64_t>(), secondPayload);
    FillRingWithoutNotification(firstRing, firstEvent);
    FillRingWithoutNotification(secondRing, secondEvent);
    std::vector<MidiEventInner> events{firstEvent, secondEvent};
    connection.HandleDeviceUmpInput(events);
    (void)connection.GetStats();
    connection.ResetStats();
    (void)connection.GetStats();

    connection.RemoveClientConnection(FIRST_CLIENT_ID);
    connection.RemoveClientConnection(MISSING_CLIENT_ID);
    (void)connection.IsEmptyClientConnections();
    connection.RemoveClientConnection(SECOND_CLIENT_ID);
    (void)connection.IsEmptyClientConnections();
}
} // namespace

void RunMidiCoreCoverageScenarios(const uint8_t *data, size_t size)
{
    FuzzedDataProvider midiUtilsFdp(data, size);
    ExerciseMidiUtils(midiUtilsFdp);
    ExerciseSharedMemory();
    FuzzedDataProvider sharedRingFdp(data, size);
    ExerciseSharedRing(sharedRingFdp);
    FuzzedDataProvider clientConnectionFdp(data, size);
    ExerciseClientConnection(clientConnectionFdp);
    FuzzedDataProvider inputConnectionFdp(data, size);
    ExerciseInputConnection(inputConnectionFdp);
}
} // namespace MIDI
} // namespace OHOS
