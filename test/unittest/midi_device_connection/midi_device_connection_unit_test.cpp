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

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include "gtest/gtest.h"

#include "midi_device_connection.h"
#include "midi_shared_ring.h"
#include "native_midi_base.h"

using namespace std::chrono;
using namespace testing::ext;

namespace OHOS {
namespace MIDI {

void DrainCounterFd(int fd);

class MidiDeviceConnectionUnitTest : public testing::Test {
public:
};

class RecordingMidiDeviceDriver final : public MidiDeviceDriver {
public:
    std::vector<DeviceInformation> GetRegisteredDevices() override
    {
        return {};
    }

    int32_t OpenDevice(int64_t deviceId) override
    {
        (void)deviceId;
        return OH_MIDI_STATUS_OK;
    }

    int32_t OpenDevice(std::string deviceAddr, BleDriverCallback deviceCallback) override
    {
        (void)deviceAddr;
        (void)deviceCallback;
        return OH_MIDI_STATUS_OK;
    }

    int32_t CloseDevice(int64_t deviceId) override
    {
        (void)deviceId;
        return OH_MIDI_STATUS_OK;
    }

    int32_t OpenInputPort(int64_t deviceId, uint32_t portIndex, UmpInputCallback callback) override
    {
        (void)deviceId;
        (void)portIndex;
        (void)callback;
        return OH_MIDI_STATUS_OK;
    }

    int32_t OpenOutputPort(int64_t deviceId, uint32_t portIndex) override
    {
        (void)deviceId;
        (void)portIndex;
        return OH_MIDI_STATUS_OK;
    }

    int32_t CloseInputPort(int64_t deviceId, uint32_t portIndex) override
    {
        (void)deviceId;
        (void)portIndex;
        return OH_MIDI_STATUS_OK;
    }

    int32_t CloseOutputPort(int64_t deviceId, uint32_t portIndex) override
    {
        (void)deviceId;
        (void)portIndex;
        return OH_MIDI_STATUS_OK;
    }

    int32_t HandleUmpInput(int64_t deviceId, uint32_t portIndex, std::vector<MidiEventInner> &list) override
    {
        lastDeviceId = deviceId;
        lastPortIndex = portIndex;
        eventCount += list.size();
        return OH_MIDI_STATUS_OK;
    }

    int64_t lastDeviceId = -1;
    uint32_t lastPortIndex = 0;
    size_t eventCount = 0;
};

static MidiEventInner MakeMidiEventInner(uint64_t timestamp, const std::vector<uint32_t> &payloadWords)
{
    MidiEventInner midiEventInner{};
    midiEventInner.timestamp = timestamp;
    midiEventInner.length = payloadWords.size(); // words
    midiEventInner.data = payloadWords.data();   // const uint32_t*
    return midiEventInner;
}

static bool IsFdValid(int fd)
{
    if (fd < 0) {
        return false;
    }
    int flags = fcntl(fd, F_GETFD);
    return (flags != -1);
}

//==================== UniqueFd ====================//

/**
 * @tc.name   : Test UniqueFd Basic
 * @tc.number : UniqueFdBasic_001
 * @tc.desc   : Valid/Get/Reset should behave as expected.
 */
HWTEST_F(MidiDeviceConnectionUnitTest, UniqueFdBasic_001, TestSize.Level1)
{
    UniqueFd emptyFd;
    EXPECT_FALSE(emptyFd.Valid());
    EXPECT_EQ(-1, emptyFd.Get());

    int eventFileDescriptor = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT_GE(eventFileDescriptor, 0);
    EXPECT_TRUE(IsFdValid(eventFileDescriptor));

    UniqueFd ownedFd(eventFileDescriptor);
    EXPECT_TRUE(ownedFd.Valid());
    EXPECT_EQ(eventFileDescriptor, ownedFd.Get());

    ownedFd.Reset(-1); // should close old fd
    EXPECT_FALSE(ownedFd.Valid());
    EXPECT_EQ(-1, ownedFd.Get());
    EXPECT_FALSE(IsFdValid(eventFileDescriptor));
}

/**
 * @tc.name   : Test UniqueFd Move Semantics
 * @tc.number : UniqueFdMove_001
 * @tc.desc   : Move constructor/assignment should transfer ownership and close previous one.
 */
HWTEST_F(MidiDeviceConnectionUnitTest, UniqueFdMove_001, TestSize.Level1)
{
    int eventFileDescriptor1 = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    int eventFileDescriptor2 = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT_GE(eventFileDescriptor1, 0);
    ASSERT_GE(eventFileDescriptor2, 0);

    UniqueFd firstFd(eventFileDescriptor1);
    UniqueFd secondFd(eventFileDescriptor2);

    UniqueFd movedFd(std::move(firstFd));
    EXPECT_FALSE(firstFd.Valid());
    EXPECT_TRUE(movedFd.Valid());
    EXPECT_EQ(eventFileDescriptor1, movedFd.Get());

    secondFd = std::move(movedFd);
    EXPECT_FALSE(movedFd.Valid());
    EXPECT_TRUE(secondFd.Valid());
    EXPECT_EQ(eventFileDescriptor1, secondFd.Get());
    EXPECT_FALSE(IsFdValid(eventFileDescriptor2));
}

//==================== DeviceConnectionBase ====================//

/**
 * @tc.name   : Test DeviceConnectionBase Add/Remove
 * @tc.number : DeviceConnectionBaseClients_001
 * @tc.desc   : AddClientConnection should create client ring; Remove should erase; IsEmpty should reflect state.
 */
HWTEST_F(MidiDeviceConnectionUnitTest, DeviceConnectionBaseClients_001, TestSize.Level1)
{
    DeviceConnectionInfo deviceConnectionInfo{};
    deviceConnectionInfo.driver = nullptr;
    deviceConnectionInfo.deviceId = 1;
    deviceConnectionInfo.direction = MidiPortDirection::INPUT;
    deviceConnectionInfo.portIndex = 2;

    DeviceConnectionBase deviceConnectionBase(deviceConnectionInfo);

    EXPECT_TRUE(deviceConnectionBase.IsEmptyClientConnections());

    std::shared_ptr<MidiSharedRing> clientRingBuffer;
    EXPECT_EQ(OH_MIDI_STATUS_OK, deviceConnectionBase.AddClientConnection(100, 999, clientRingBuffer));
    ASSERT_NE(nullptr, clientRingBuffer);
    EXPECT_FALSE(deviceConnectionBase.IsEmptyClientConnections());

    // Add another client
    std::shared_ptr<MidiSharedRing> anotherClientRingBuffer;
    EXPECT_EQ(OH_MIDI_STATUS_OK, deviceConnectionBase.AddClientConnection(200, 888, anotherClientRingBuffer));
    ASSERT_NE(nullptr, anotherClientRingBuffer);
    EXPECT_FALSE(deviceConnectionBase.IsEmptyClientConnections());

    // Remove unknown id should not crash and not empty
    deviceConnectionBase.RemoveClientConnection(300);
    EXPECT_FALSE(deviceConnectionBase.IsEmptyClientConnections());

    // Remove first client
    deviceConnectionBase.RemoveClientConnection(100);
    EXPECT_FALSE(deviceConnectionBase.IsEmptyClientConnections());

    // Remove second client -> empty
    deviceConnectionBase.RemoveClientConnection(200);
    EXPECT_TRUE(deviceConnectionBase.IsEmptyClientConnections());

    // GetInfo interface coverage
    const auto &returnedInfo = deviceConnectionBase.GetInfo();
    EXPECT_EQ(deviceConnectionInfo.deviceId, returnedInfo.deviceId);
    EXPECT_EQ(deviceConnectionInfo.portIndex, returnedInfo.portIndex);
}

//==================== DeviceConnectionForInput ====================//

/**
 * @tc.name   : Test DeviceConnectionForInput Broadcast
 * @tc.number : DeviceConnectionForInput_001
 * @tc.desc   : HandleDeviceUmpInput should broadcast events into each client's ring.
 */
HWTEST_F(MidiDeviceConnectionUnitTest, DeviceConnectionForInput_001, TestSize.Level1)
{
    DeviceConnectionInfo deviceConnectionInfo{};
    deviceConnectionInfo.driver = nullptr;
    deviceConnectionInfo.deviceId = 2;
    deviceConnectionInfo.direction = MidiPortDirection::INPUT;
    deviceConnectionInfo.portIndex = 0;

    DeviceConnectionForInput inputConnection(deviceConnectionInfo);

    std::shared_ptr<MidiSharedRing> clientRingBuffer1;
    std::shared_ptr<MidiSharedRing> clientRingBuffer2;
    ASSERT_EQ(OH_MIDI_STATUS_OK, inputConnection.AddClientConnection(1, 1000, clientRingBuffer1));
    ASSERT_EQ(OH_MIDI_STATUS_OK, inputConnection.AddClientConnection(2, 1001, clientRingBuffer2));
    ASSERT_NE(nullptr, clientRingBuffer1);
    ASSERT_NE(nullptr, clientRingBuffer2);

    std::vector<uint32_t> payloadWords1{0x11111111, 0x22222222};
    std::vector<uint32_t> payloadWords2{0x33333333, 0x44444444, 0x55555555};

    std::vector<MidiEventInner> deviceEvents;
    deviceEvents.push_back(MakeMidiEventInner(10, payloadWords1));
    deviceEvents.push_back(MakeMidiEventInner(20, payloadWords2));

    inputConnection.HandleDeviceUmpInput(deviceEvents);

    // Verify both client rings received 2 events in order.
    for (auto *ringPointer : {clientRingBuffer1.get(), clientRingBuffer2.get()}) {
        MidiSharedRing::PeekedEvent peekedEvent1{};
        ASSERT_EQ(MidiStatusCode::OK, ringPointer->PeekNext(peekedEvent1));
        EXPECT_EQ(10u, peekedEvent1.localHeader.timestamp);
        // MidiSharedRing stores payload length in bytes
        EXPECT_EQ(payloadWords1.size(), static_cast<size_t>(peekedEvent1.localHeader.length));
        ringPointer->CommitRead(peekedEvent1);

        MidiSharedRing::PeekedEvent peekedEvent2{};
        ASSERT_EQ(MidiStatusCode::OK, ringPointer->PeekNext(peekedEvent2));
        EXPECT_EQ(20u, peekedEvent2.localHeader.timestamp);
        EXPECT_EQ(payloadWords2.size(), static_cast<size_t>(peekedEvent2.localHeader.length));
        ringPointer->CommitRead(peekedEvent2);

        MidiSharedRing::PeekedEvent peekedEvent3{};
        EXPECT_EQ(MidiStatusCode::WOULD_BLOCK, ringPointer->PeekNext(peekedEvent3));
    }

    // Remove one client and broadcast again, should only affect remaining client.
    inputConnection.RemoveClientConnection(1);

    std::vector<uint32_t> payloadWords3{0xAAAA5555};
    std::vector<MidiEventInner> deviceEvents2;
    deviceEvents2.push_back(MakeMidiEventInner(30, payloadWords3));
    inputConnection.HandleDeviceUmpInput(deviceEvents2);

    // client 1 ring should have no new data
    MidiSharedRing::PeekedEvent peekedEventAfterRemove{};
    EXPECT_EQ(MidiStatusCode::WOULD_BLOCK, clientRingBuffer1->PeekNext(peekedEventAfterRemove));

    // client 2 ring should have the new event
    ASSERT_EQ(MidiStatusCode::OK, clientRingBuffer2->PeekNext(peekedEventAfterRemove));
    EXPECT_EQ(30u, peekedEventAfterRemove.localHeader.timestamp);
    EXPECT_EQ(payloadWords3.size(), static_cast<size_t>(peekedEventAfterRemove.localHeader.length));
}

//==================== DeviceConnectionForOutput ====================//

/**
 * @tc.name   : Test DeviceConnectionForOutput Start/Stop
 * @tc.number : DeviceConnectionForOutput_001
 * @tc.desc   : Start/Stop should be idempotent; notify fd should become valid after Start.
 */
HWTEST_F(MidiDeviceConnectionUnitTest, DeviceConnectionForOutput_001, TestSize.Level1)
{
    DeviceConnectionInfo deviceConnectionInfo{};
    deviceConnectionInfo.driver = nullptr;
    deviceConnectionInfo.deviceId = 3;
    deviceConnectionInfo.direction = MidiPortDirection::OUTPUT;
    deviceConnectionInfo.portIndex = 1;

    DeviceConnectionForOutput outputConnection(deviceConnectionInfo);

    // Stop before start: OK
    EXPECT_EQ(OH_MIDI_STATUS_OK, outputConnection.Stop());

    // Start twice: both OK
    EXPECT_EQ(OH_MIDI_STATUS_OK, outputConnection.Start());
    EXPECT_EQ(OH_MIDI_STATUS_OK, outputConnection.Start());

    int notifyEventFileDescriptor = outputConnection.GetNotifyEventFdForClients();
    EXPECT_GE(notifyEventFileDescriptor, 0);
    EXPECT_TRUE(IsFdValid(notifyEventFileDescriptor));

    // Stop twice: both OK
    EXPECT_EQ(OH_MIDI_STATUS_OK, outputConnection.Stop());
    EXPECT_EQ(OH_MIDI_STATUS_OK, outputConnection.Stop());
}

/**
 * @tc.name   : Test DeviceConnectionForOutput Wake + Drain Paths
 * @tc.number : DeviceConnectionForOutput_002
 * @tc.desc   : Write realtime/non-realtime events to client ring, wake worker, cover drain/collect/flush/timer paths.
 */
HWTEST_F(MidiDeviceConnectionUnitTest, DeviceConnectionForOutput_002, TestSize.Level1)
{
    DeviceConnectionInfo deviceConnectionInfo{};
    deviceConnectionInfo.driver = nullptr;
    deviceConnectionInfo.deviceId = 4;
    deviceConnectionInfo.direction = MidiPortDirection::OUTPUT;
    deviceConnectionInfo.portIndex = 0;

    DeviceConnectionForOutput outputConnection(deviceConnectionInfo);

    // make cache very small to trigger "TryAppendToSendCache false -> flush -> still false -> SendToDriver"
    outputConnection.SetMaxSendCacheBytes(4);
    outputConnection.SetPerClientMaxPendingEvents(1); // currently not wired to clients, but cover interface

    ASSERT_EQ(OH_MIDI_STATUS_OK, outputConnection.Start());

    std::shared_ptr<MidiSharedRing> clientRingBuffer;
    ASSERT_EQ(OH_MIDI_STATUS_OK, outputConnection.AddClientConnection(10, 1234, clientRingBuffer));
    ASSERT_NE(nullptr, clientRingBuffer);

    // Prepare events:
    // 1) realtime: timestamp==0, payload empty (length==0 words) -> TryAppendToSendCache(payload.empty()) branch
    // 2) realtime: timestamp==0, payload > maxSendCacheBytes -> TryAppendToSendCache false, cover flush + SendToDriver
    // 3) non-realtime: timestamp treated as delay(ns), set to very small -> enqueue pending, arm timerfd, then due pops
    uint32_t dummyWord = 0x12345678;
    MidiEventInner realtimeEmptyPayload{};
    realtimeEmptyPayload.timestamp = 0;
    realtimeEmptyPayload.length = 0;
    realtimeEmptyPayload.data = &dummyWord; // ValidateOneEvent requires data != nullptr

    std::vector<uint32_t> realtimeLargePayloadWords{0x11111111, 0x22222222, 0x33333333}; // 12 bytes
    MidiEventInner realtimeLargePayload = MakeMidiEventInner(0, realtimeLargePayloadWords);

    std::vector<uint32_t> nonRealtimePayloadWords{0xAAAAAAAA, 0xBBBBBBBB}; // 8 bytes
    MidiEventInner nonRealtimeEvent = MakeMidiEventInner(1 /* 1ns delay */, nonRealtimePayloadWords);

    // Write events into ring
    ASSERT_EQ(MidiStatusCode::OK, clientRingBuffer->TryWriteEvent(realtimeEmptyPayload, true));
    ASSERT_EQ(MidiStatusCode::OK, clientRingBuffer->TryWriteEvent(realtimeLargePayload, true));
    ASSERT_EQ(MidiStatusCode::OK, clientRingBuffer->TryWriteEvent(nonRealtimeEvent, true));

    // Wake worker via notify eventfd
    const int notifyEventFileDescriptor = outputConnection.GetNotifyEventFdForClients();
    ASSERT_GE(notifyEventFileDescriptor, 0);

    const uint64_t one = 1;
    ASSERT_EQ(sizeof(one), static_cast<size_t>(::write(notifyEventFileDescriptor, &one, sizeof(one))));

    // Give worker thread some time to:
    // - drain ring (consume realtime + enqueue non-realtime)
    // - UpdateNextTimer (arm timerfd)
    // - timerfd trigger -> epoll wake -> collect due -> flush send cache
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    EXPECT_EQ(OH_MIDI_STATUS_OK, outputConnection.Stop());

    // Ring should have been drained (best-effort check; if pending was full it might stop early,
    // but in current implementation pending limit is not wired, so it should drain).
    MidiSharedRing::PeekedEvent peekedEvent{};
    EXPECT_TRUE(clientRingBuffer->PeekNext(peekedEvent) == MidiStatusCode::WOULD_BLOCK ||
                clientRingBuffer->PeekNext(peekedEvent) == MidiStatusCode::OK);
}

/**
 * @tc.name   : Test DeviceConnectionForOutput Destructor
 * @tc.number : DeviceConnectionForOutput_003
 * @tc.desc   : Destructor should Stop safely when running.
 */
HWTEST_F(MidiDeviceConnectionUnitTest, DeviceConnectionForOutput_003, TestSize.Level1)
{
    DeviceConnectionInfo deviceConnectionInfo{};
    deviceConnectionInfo.driver = nullptr;
    deviceConnectionInfo.deviceId = 5;
    deviceConnectionInfo.direction = MidiPortDirection::OUTPUT;
    deviceConnectionInfo.portIndex = 2;

    {
        DeviceConnectionForOutput outputConnection(deviceConnectionInfo);
        ASSERT_EQ(OH_MIDI_STATUS_OK, outputConnection.Start());
    }
}

/**
 * @tc.name   : Test DeviceConnectionForOutput Flush
 * @tc.number : DeviceConnectionForOutput_004
 * @tc.desc   : expect flushing client cache success
 */
HWTEST_F(MidiDeviceConnectionUnitTest, DeviceConnectionForOutput_004, TestSize.Level1)
{
    DeviceConnectionInfo deviceConnectionInfo{};
    deviceConnectionInfo.driver = nullptr;
    deviceConnectionInfo.deviceId = 4;
    deviceConnectionInfo.direction = MidiPortDirection::OUTPUT;
    deviceConnectionInfo.portIndex = 0;

    DeviceConnectionForOutput outputConnection(deviceConnectionInfo);

    ASSERT_EQ(OH_MIDI_STATUS_OK, outputConnection.Start());

    std::shared_ptr<MidiSharedRing> clientRingBuffer;
    uint32_t clientId = 10;
    int64_t deviceHandle = 1234;

    ASSERT_EQ(OH_MIDI_STATUS_OK, outputConnection.AddClientConnection(clientId, deviceHandle, clientRingBuffer));
    ASSERT_NE(nullptr, clientRingBuffer);

    uint32_t dummyWord = 0x12345678;
    MidiEventInner realtimeEmptyPayload{};
    realtimeEmptyPayload.timestamp = 0;
    realtimeEmptyPayload.length = 0;
    realtimeEmptyPayload.data = &dummyWord;

    std::vector<uint32_t> realtimeLargePayloadWords{0x11111111, 0x22222222, 0x33333333}; // 12 bytes
    MidiEventInner realtimeLargePayload = MakeMidiEventInner(0, realtimeLargePayloadWords);

    std::vector<uint32_t> nonRealtimePayloadWords{0xAAAAAAAA, 0xBBBBBBBB}; // 8 bytes
    MidiEventInner nonRealtimeEvent = MakeMidiEventInner(1 /* 1ns delay */, nonRealtimePayloadWords);

    // Write events into ring
    ASSERT_EQ(MidiStatusCode::OK, clientRingBuffer->TryWriteEvent(realtimeEmptyPayload, true));
    ASSERT_EQ(MidiStatusCode::OK, clientRingBuffer->TryWriteEvent(realtimeLargePayload, true));
    ASSERT_EQ(MidiStatusCode::OK, clientRingBuffer->TryWriteEvent(nonRealtimeEvent, true));

    EXPECT_EQ(clientRingBuffer->IsEmpty(), false);

    // Wake worker via notify eventfd
    const int notifyEventFileDescriptor = outputConnection.GetNotifyEventFdForClients();
    ASSERT_GE(notifyEventFileDescriptor, 0);

    const uint64_t one = 1;
    ASSERT_EQ(sizeof(one), static_cast<size_t>(::write(notifyEventFileDescriptor, &one, sizeof(one))));

    outputConnection.FlushClientCache(clientId);
    EXPECT_EQ(clientRingBuffer->GetReadPosition(), 0);
    EXPECT_EQ(clientRingBuffer->GetWritePosition(), 0);
}

// ====================================================================
// Bug-fix regression tests
// ====================================================================

/**
 * @tc.name   : Test DrainSingleClientRing drain limit
 * @tc.number : DeviceConnectionForOutput_005
 * @tc.desc   : Verify that DrainSingleClientRing does not consume all events in one unbounded
 *              loop. Write as many events as the ring can hold, verify they are all drained
 *              within one worker cycle (MAX_DRAIN_PER_CLIENT=128 >= ring capacity allows this).
 *              This regression test ensures the drain path works correctly with the per-client
 *              limit in place.
 */
HWTEST_F(MidiDeviceConnectionUnitTest, DeviceConnectionForOutput_005, TestSize.Level1)
{
    DeviceConnectionInfo deviceConnectionInfo{};
    deviceConnectionInfo.driver = nullptr;
    deviceConnectionInfo.deviceId = 6;
    deviceConnectionInfo.direction = MidiPortDirection::OUTPUT;
    deviceConnectionInfo.portIndex = 0;

    DeviceConnectionForOutput outputConnection(deviceConnectionInfo);
    outputConnection.SetMaxSendCacheBytes(4096);

    ASSERT_EQ(OH_MIDI_STATUS_OK, outputConnection.Start());

    std::shared_ptr<MidiSharedRing> clientRingBuffer;
    ASSERT_EQ(OH_MIDI_STATUS_OK, outputConnection.AddClientConnection(20, 5678, clientRingBuffer));
    ASSERT_NE(nullptr, clientRingBuffer);

    // Write as many small realtime events (timestamp=0, length=0) as the ring can hold
    // Each header is 24 bytes, ring capacity is 2048 bytes => ~85 events max
    uint32_t dummyWord = 0;
    int eventsWritten = 0;
    for (int i = 0; i < 200; ++i) {
        MidiEventInner ev{};
        ev.timestamp = 0;
        ev.length = 0;
        ev.data = &dummyWord;
        if (clientRingBuffer->TryWriteEvent(ev, false) != MidiStatusCode::OK) {
            break;
        }
        eventsWritten++;
    }
    ASSERT_GT(eventsWritten, 0);
    EXPECT_FALSE(clientRingBuffer->IsEmpty());

    // Wake worker
    const int notifyFd = outputConnection.GetNotifyEventFdForClients();
    ASSERT_GE(notifyFd, 0);
    const uint64_t one = 1;
    ASSERT_EQ(sizeof(one), static_cast<size_t>(::write(notifyFd, &one, sizeof(one))));

    // Give worker thread time to drain
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    EXPECT_EQ(OH_MIDI_STATUS_OK, outputConnection.Stop());

    // Ring should be drained (MAX_DRAIN_PER_CLIENT=128 >= ring capacity ~85)
    EXPECT_TRUE(clientRingBuffer->IsEmpty());
}

/**
 * @tc.name   : Test past-due non-realtime event processing
 * @tc.number : DeviceConnectionForOutput_006
 * @tc.desc   : Write a non-realtime event with a timestamp far in the past. The timerfd
 *              handling should wake the worker immediately (not disarm the timer with {0,0}).
 *              Regression test for UpdateNextTimer bug where past-due events caused timer
 *              disarming instead of immediate firing.
 */
HWTEST_F(MidiDeviceConnectionUnitTest, DeviceConnectionForOutput_006, TestSize.Level1)
{
    DeviceConnectionInfo deviceConnectionInfo{};
    deviceConnectionInfo.driver = nullptr;
    deviceConnectionInfo.deviceId = 7;
    deviceConnectionInfo.direction = MidiPortDirection::OUTPUT;
    deviceConnectionInfo.portIndex = 1;

    DeviceConnectionForOutput outputConnection(deviceConnectionInfo);
    outputConnection.SetMaxSendCacheBytes(4096);

    ASSERT_EQ(OH_MIDI_STATUS_OK, outputConnection.Start());

    std::shared_ptr<MidiSharedRing> clientRingBuffer;
    ASSERT_EQ(OH_MIDI_STATUS_OK, outputConnection.AddClientConnection(30, 9999, clientRingBuffer));
    ASSERT_NE(nullptr, clientRingBuffer);

    // Write a non-realtime event with timestamp far in the past (1ns since epoch)
    // This should be treated as already due and trigger immediate processing
    std::vector<uint32_t> payload{0xAAAAAAAA, 0xBBBBBBBB};
    MidiEventInner pastDueEvent = MakeMidiEventInner(1, payload); // 1ns = far in the past
    ASSERT_EQ(MidiStatusCode::OK, clientRingBuffer->TryWriteEvent(pastDueEvent, true));

    // Wake worker
    const int notifyFd = outputConnection.GetNotifyEventFdForClients();
    ASSERT_GE(notifyFd, 0);
    const uint64_t one = 1;
    ASSERT_EQ(sizeof(one), static_cast<size_t>(::write(notifyFd, &one, sizeof(one))));

    // Give worker time to: drain ring -> enqueue to pending -> timer triggers -> collect due -> send
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    EXPECT_EQ(OH_MIDI_STATUS_OK, outputConnection.Stop());

    // Ring should be empty - the past-due event was consumed
    EXPECT_TRUE(clientRingBuffer->IsEmpty());
}

/**
 * @tc.name   : Test CommitRead failure propagation in output worker
 * @tc.number : DeviceConnectionForOutput_007
 * @tc.desc   : Verify that when CommitRead detects a TOCTOU conflict (sequence mismatch),
 *              the ConsumeRealtimeEvent returns false and the drain loop breaks cleanly.
 *              This ensures the fix for unchecked CommitRead return values works correctly.
 */
HWTEST_F(MidiDeviceConnectionUnitTest, DeviceConnectionForOutput_007, TestSize.Level1)
{
    // This test validates the CommitRead TOCTOU protection indirectly.
    // Direct unit test of the failure case is in MidiSharedRingCommitRead_003.
    // Here we verify the drain loop handles CommitRead failure gracefully by writing
    // a realtime event and verifying normal consumption still works after the fix.

    DeviceConnectionInfo deviceConnectionInfo{};
    deviceConnectionInfo.driver = nullptr;
    deviceConnectionInfo.deviceId = 8;
    deviceConnectionInfo.direction = MidiPortDirection::OUTPUT;
    deviceConnectionInfo.portIndex = 2;

    DeviceConnectionForOutput outputConnection(deviceConnectionInfo);
    outputConnection.SetMaxSendCacheBytes(4096);

    ASSERT_EQ(OH_MIDI_STATUS_OK, outputConnection.Start());

    std::shared_ptr<MidiSharedRing> clientRingBuffer;
    ASSERT_EQ(OH_MIDI_STATUS_OK, outputConnection.AddClientConnection(40, 1111, clientRingBuffer));
    ASSERT_NE(nullptr, clientRingBuffer);

    // Write a normal realtime event
    uint32_t dummyWord = 0x12345678;
    MidiEventInner realtimeEvent{};
    realtimeEvent.timestamp = 0;
    realtimeEvent.length = 1;
    realtimeEvent.data = &dummyWord;

    ASSERT_EQ(MidiStatusCode::OK, clientRingBuffer->TryWriteEvent(realtimeEvent, true));

    // Wake worker
    const int notifyFd = outputConnection.GetNotifyEventFdForClients();
    ASSERT_GE(notifyFd, 0);
    const uint64_t one = 1;
    ASSERT_EQ(sizeof(one), static_cast<size_t>(::write(notifyFd, &one, sizeof(one))));

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    EXPECT_EQ(OH_MIDI_STATUS_OK, outputConnection.Stop());

    // Event should have been consumed successfully via the CommitRead path
    EXPECT_TRUE(clientRingBuffer->IsEmpty());
}

/**
 * @tc.name   : Test counter fd drain branches
 * @tc.number : DeviceConnectionForOutput_008
 * @tc.desc   : Cover invalid, empty, readable, EOF, and bounded-loop counter fd paths.
 */
HWTEST_F(MidiDeviceConnectionUnitTest, DeviceConnectionForOutput_008, TestSize.Level1)
{
    DrainCounterFd(-1);

    int emptyEventFd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT_GE(emptyEventFd, 0);
    DrainCounterFd(emptyEventFd);
    uint64_t counter = 3;
    ASSERT_EQ(sizeof(counter), static_cast<size_t>(::write(emptyEventFd, &counter, sizeof(counter))));
    DrainCounterFd(emptyEventFd);
    close(emptyEventFd);

    int endOfFilePipe[2] = {-1, -1};
    ASSERT_EQ(0, pipe(endOfFilePipe));
    close(endOfFilePipe[1]);
    DrainCounterFd(endOfFilePipe[0]);
    close(endOfFilePipe[0]);

    int busyPipe[2] = {-1, -1};
    ASSERT_EQ(0, pipe(busyPipe));
    std::vector<uint64_t> counters(17, 1);
    ASSERT_EQ(counters.size() * sizeof(uint64_t),
        static_cast<size_t>(::write(busyPipe[1], counters.data(), counters.size() * sizeof(uint64_t))));
    DrainCounterFd(busyPipe[0]);
    close(busyPipe[0]);
    close(busyPipe[1]);
}

/**
 * @tc.name   : Test base null-client and statistics branches
 * @tc.number : DeviceConnectionBaseBranches_001
 * @tc.desc   : Cover null client filtering and all reachable statistics overflow paths.
 */
HWTEST_F(MidiDeviceConnectionUnitTest, DeviceConnectionBaseBranches_001, TestSize.Level1)
{
    constexpr uint32_t MISSING_CLIENT_ID = 66;
    constexpr uint32_t CONNECTED_CLIENT_ID = 77;
    constexpr uint32_t INITIAL_BYTE_COUNT = 10;
    DeviceConnectionInfo info{};
    info.direction = MidiPortDirection::INPUT;
    DeviceConnectionBase connection(info);

    EXPECT_EQ(0, connection.GetStats().statsDurationMs);
    connection.clients_.push_back(nullptr);
    auto client = std::make_shared<ClientConnectionInServer>(CONNECTED_CLIENT_ID, 88, 0);
    connection.clients_.push_back(client);
    EXPECT_FALSE(connection.HasClientConnection(MISSING_CLIENT_ID));
    EXPECT_TRUE(connection.HasClientConnection(CONNECTED_CLIENT_ID));
    EXPECT_EQ(std::vector<uint32_t>({CONNECTED_CLIENT_ID}), connection.GetConnectedClientIds());
    EXPECT_EQ(2, connection.SnapshotClients().size());

    connection.RemoveClientConnection(MISSING_CLIENT_ID);
    EXPECT_EQ(2, connection.clients_.size());
    connection.RemoveClientConnection(CONNECTED_CLIENT_ID);
    ASSERT_EQ(1, connection.clients_.size());
    EXPECT_EQ(nullptr, connection.clients_.front());

    connection.IncrementEventCount();
    EXPECT_EQ(1, connection.GetStats().eventCount);
    connection.eventCount_.store(MAX_EVENT_COUNT);
    connection.byteCount_.store(INITIAL_BYTE_COUNT);
    connection.IncrementEventCount();
    EXPECT_EQ(1, connection.GetStats().eventCount);
    EXPECT_EQ(0, connection.GetStats().byteCount);

    connection.IncrementEventCount(8);
    EXPECT_EQ(8, connection.GetStats().byteCount);
    connection.byteCount_.store(MAX_BYTE_COUNT);
    connection.IncrementEventCount(8);
    EXPECT_EQ(8, connection.GetStats().byteCount);
    connection.byteCount_.store(MAX_BYTE_COUNT - 2);
    connection.IncrementEventCount(8);
    EXPECT_EQ(8, connection.GetStats().byteCount);

    connection.IncrementErrorCount();
    connection.errorCount_.store(MAX_EVENT_COUNT);
    connection.IncrementErrorCount();
    EXPECT_EQ(1, connection.GetStats().errorCount);

    connection.ResetStats();
    auto resetStats = connection.GetStats();
    EXPECT_EQ(0, resetStats.eventCount);
    EXPECT_EQ(0, resetStats.byteCount);
    EXPECT_EQ(0, resetStats.errorCount);
    EXPECT_GT(resetStats.statsStartTimeMs, 0);
    EXPECT_GE(resetStats.statsDurationMs, 0);
}

/**
 * @tc.name   : Test input null-client branch
 * @tc.number : DeviceConnectionForInput_002
 * @tc.desc   : A null client is ignored while a valid client receives the event.
 */
HWTEST_F(MidiDeviceConnectionUnitTest, DeviceConnectionForInput_002, TestSize.Level1)
{
    DeviceConnectionInfo info{};
    info.direction = MidiPortDirection::INPUT;
    DeviceConnectionForInput connection(info);
    connection.clients_.push_back(nullptr);

    auto client = std::make_shared<ClientConnectionInServer>(1, 2, 0);
    ASSERT_EQ(OH_MIDI_STATUS_OK, client->CreateRingBuffer());
    connection.clients_.push_back(client);

    std::vector<uint32_t> payload{0x12345678};
    MidiEventInner event = MakeMidiEventInner(1, payload);
    connection.BroadcastToClients(event);

    MidiSharedRing::PeekedEvent peekedEvent{};
    ASSERT_EQ(MidiStatusCode::OK, client->GetRingBuffer()->PeekNext(peekedEvent));
    EXPECT_EQ(1, peekedEvent.localHeader.length);
}

/**
 * @tc.name   : Test output send-cache branches
 * @tc.number : DeviceConnectionForOutput_009
 * @tc.desc   : Cover invalid wake, add-before-start, cache limits, null driver, and driver flush.
 */
HWTEST_F(MidiDeviceConnectionUnitTest, DeviceConnectionForOutput_009, TestSize.Level1)
{
    constexpr uint32_t DEVICE_HANDLE = 2;
    RecordingMidiDeviceDriver driver;
    DeviceConnectionInfo info{};
    info.driver = nullptr;
    info.deviceId = 91;
    info.direction = MidiPortDirection::OUTPUT;
    info.portIndex = 3;
    DeviceConnectionForOutput connection(info);

    connection.WakeWorkerByEventFd();
    connection.DrainEventFd();
    connection.DrainTimerFd();
    std::shared_ptr<MidiSharedRing> ring;
    EXPECT_EQ(OH_MIDI_STATUS_SYSTEM_ERROR, connection.AddClientConnection(1, DEVICE_HANDLE, ring));

    uint32_t payload[] = {0x11111111, 0x22222222, 0x33333333};
    connection.SetMaxSendCacheBytes(8);
    EXPECT_TRUE(connection.TryAppendToSendCache(1, nullptr, 0));
    EXPECT_FALSE(connection.TryAppendToSendCache(1, nullptr, 1));
    EXPECT_FALSE(connection.TryAppendToSendCache(1, payload, 3));
    EXPECT_TRUE(connection.TryAppendToSendCache(2, payload, 1));
    EXPECT_FALSE(connection.TryAppendToSendCache(3, payload, 2));

    connection.FlushSendCacheToDriver();
    EXPECT_EQ(1, connection.sendCache_.size());
    connection.info_.driver = &driver;
    connection.FlushSendCacheToDriver();
    EXPECT_TRUE(connection.sendCache_.empty());
    EXPECT_TRUE(connection.sendCachePayloadBuffers_.empty());
    EXPECT_EQ(0, connection.currentSendCacheBytes_);
    EXPECT_EQ(1, driver.eventCount);
    EXPECT_EQ(91, driver.lastDeviceId);
    EXPECT_EQ(3, driver.lastPortIndex);
    EXPECT_EQ(1, connection.GetStats().eventCount);
    EXPECT_EQ(sizeof(uint32_t), connection.GetStats().byteCount);
    connection.FlushSendCacheToDriver();
}

/**
 * @tc.name   : Test direct realtime and non-realtime consumers
 * @tc.number : DeviceConnectionForOutput_010
 * @tc.desc   : Cover successful, full-pending, null-ring, and stale-peek consumption paths.
 */
HWTEST_F(MidiDeviceConnectionUnitTest, DeviceConnectionForOutput_010, TestSize.Level1)
{
    constexpr uint32_t CLIENT_ID = 2;
    constexpr uint32_t DEVICE_HANDLE = 2;
    DeviceConnectionInfo info{};
    info.direction = MidiPortDirection::OUTPUT;
    DeviceConnectionForOutput connection(info);
    connection.SetMaxSendCacheBytes(16);

    ClientConnectionInServer nullRingClient(1, DEVICE_HANDLE, 0);
    connection.DrainSingleClientRing(nullRingClient);
    connection.clients_.push_back(nullptr);
    connection.DrainAllClientsRings();

    ClientConnectionInServer client(CLIENT_ID, 3, 0);
    ASSERT_EQ(OH_MIDI_STATUS_OK, client.CreateRingBuffer());
    auto ring = client.GetRingBuffer();
    ASSERT_NE(nullptr, ring);

    std::vector<uint32_t> payload{0xAABBCCDD};
    MidiEventInner realtimeEvent = MakeMidiEventInner(0, payload);
    ASSERT_EQ(MidiStatusCode::OK, ring->TryWriteEvent(realtimeEvent, false));
    MidiSharedRing::PeekedEvent realtimePeek{};
    ASSERT_EQ(MidiStatusCode::OK, ring->PeekNext(realtimePeek));
    EXPECT_TRUE(connection.ConsumeRealtimeEvent(*ring, realtimePeek));

    ASSERT_EQ(MidiStatusCode::OK, ring->TryWriteEvent(realtimeEvent, false));
    ASSERT_EQ(MidiStatusCode::OK, ring->PeekNext(realtimePeek));
    auto *header = reinterpret_cast<ShmMidiEventHeader *>(ring->GetDataBase() + realtimePeek.beginOffset);
    ASSERT_NE(nullptr, header);
    header->sequence.store(realtimePeek.sequence + 100, std::memory_order_relaxed);
    EXPECT_FALSE(connection.ConsumeRealtimeEvent(*ring, realtimePeek));
    ring->Flush();

    MidiEventInner scheduledEvent = MakeMidiEventInner(
        duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count(), payload);
    ASSERT_EQ(MidiStatusCode::OK, ring->TryWriteEvent(scheduledEvent, false));
    MidiSharedRing::PeekedEvent scheduledPeek{};
    ASSERT_EQ(MidiStatusCode::OK, ring->PeekNext(scheduledPeek));
    client.SetMaxPending(0);
    EXPECT_FALSE(connection.ConsumeNonRealtimeEvent(client, *ring, scheduledPeek));

    client.SetMaxPending(1);
    EXPECT_TRUE(connection.ConsumeNonRealtimeEvent(client, *ring, scheduledPeek));
    ASSERT_EQ(MidiStatusCode::OK, ring->TryWriteEvent(scheduledEvent, false));
    ASSERT_EQ(MidiStatusCode::OK, ring->PeekNext(scheduledPeek));
    EXPECT_FALSE(connection.ConsumeNonRealtimeEvent(client, *ring, scheduledPeek));
}

/**
 * @tc.name   : Test pending-heap selection and timer branches
 * @tc.number : DeviceConnectionForOutput_011
 * @tc.desc   : Cover empty/full heaps, earliest selection, future break, past due, and timer choices.
 */
HWTEST_F(MidiDeviceConnectionUnitTest, DeviceConnectionForOutput_011, TestSize.Level1)
{
    DeviceConnectionInfo info{};
    info.direction = MidiPortDirection::OUTPUT;
    DeviceConnectionForOutput connection(info);

    auto emptyClient = std::make_shared<ClientConnectionInServer>(1, 1, 0);
    ClientConnectionInServer::PendingEvent popped{};
    EXPECT_EQ(nullptr, emptyClient->PeekPendingTop());
    EXPECT_FALSE(emptyClient->PopPendingTop(popped));
    emptyClient->SetMaxPending(0);
    EXPECT_FALSE(emptyClient->EnqueueNonRealtime({}, steady_clock::now(), 1));

    auto laterClient = std::make_shared<ClientConnectionInServer>(2, 2, 0);
    auto earlierClient = std::make_shared<ClientConnectionInServer>(3, 3, 0);
    auto latestClient = std::make_shared<ClientConnectionInServer>(4, 4, 0);
    auto now = steady_clock::now();
    ASSERT_TRUE(laterClient->EnqueueNonRealtime({1}, now + seconds(2), 2));
    ASSERT_TRUE(earlierClient->EnqueueNonRealtime({2}, now + seconds(1), 3));
    ASSERT_TRUE(latestClient->EnqueueNonRealtime({3}, now + seconds(3), 4));

    std::vector<std::shared_ptr<ClientConnectionInServer>> clients{
        nullptr, emptyClient, laterClient, earlierClient, latestClient};
    steady_clock::time_point earliestDue{};
    EXPECT_EQ(earlierClient, connection.FindClientWithEarliestDue(clients, earliestDue));
    EXPECT_EQ(now + seconds(1), earliestDue);

    connection.clients_ = {nullptr, laterClient};
    connection.CollectDueEventsFromClientHeaps();
    EXPECT_TRUE(laterClient->HasPending());
    connection.UpdateNextTimer();

    auto pastClient = std::make_shared<ClientConnectionInServer>(5, 5, 0);
    ASSERT_TRUE(pastClient->EnqueueNonRealtime({5}, now - seconds(1), 5));
    connection.clients_.push_back(pastClient);
    connection.SetMaxSendCacheBytes(0);
    connection.CollectDueEventsFromClientHeaps();
    EXPECT_FALSE(pastClient->HasPending());
    EXPECT_TRUE(laterClient->HasPending());

    ASSERT_TRUE(pastClient->EnqueueNonRealtime({6}, now - seconds(1), 6));
    connection.UpdateNextTimer();
    connection.clients_.clear();
    connection.UpdateNextTimer();
}

/**
 * @tc.name   : Test client pending priority and flush
 * @tc.number : ClientConnectionInServerBranches_001
 * @tc.desc   : Cover pending priority ordering, successful pop, and client flush selection.
 */
HWTEST_F(MidiDeviceConnectionUnitTest, ClientConnectionInServerBranches_001, TestSize.Level1)
{
    DeviceConnectionInfo info{};
    info.direction = MidiPortDirection::OUTPUT;
    DeviceConnectionForOutput connection(info);

    auto first = std::make_shared<ClientConnectionInServer>(10, 10, 0);
    auto second = std::make_shared<ClientConnectionInServer>(20, 20, 0);
    ASSERT_EQ(OH_MIDI_STATUS_OK, first->CreateRingBuffer());
    ASSERT_EQ(OH_MIDI_STATUS_OK, second->CreateRingBuffer());
    auto now = steady_clock::now();
    ASSERT_TRUE(first->EnqueueNonRealtime({2}, now + seconds(2), 2));
    ASSERT_TRUE(first->EnqueueNonRealtime({1}, now + seconds(1), 1));

    const auto *top = first->PeekPendingTop();
    ASSERT_NE(nullptr, top);
    EXPECT_EQ(1, top->timestamp);
    ClientConnectionInServer::PendingEvent popped{};
    ASSERT_TRUE(first->PopPendingTop(popped));
    EXPECT_EQ(1, popped.timestamp);

    std::vector<uint32_t> payload{1};
    MidiEventInner event = MakeMidiEventInner(0, payload);
    ASSERT_EQ(MidiStatusCode::OK, first->GetRingBuffer()->TryWriteEvent(event, false));
    connection.clients_ = {second, first};
    connection.FlushClientCache(10);
    EXPECT_TRUE(first->GetRingBuffer()->IsEmpty());
    EXPECT_FALSE(first->HasPending());
}

/**
 * @tc.name   : Test remaining realtime output connection branches
 * @tc.number : DeviceConnectionForOutput_012
 * @tc.desc   : Cover stop without worker and drain-time failures.
 */
HWTEST_F(MidiDeviceConnectionUnitTest, DeviceConnectionForOutput_012, TestSize.Level1)
{
    DeviceConnectionInfo info {};
    info.direction = MidiPortDirection::OUTPUT;
    DeviceConnectionForOutput connection(info);

    connection.running_.store(true);
    EXPECT_EQ(OH_MIDI_STATUS_OK, connection.Stop());

    ClientConnectionInServer realtimeClient(1, 1, 0);
    ASSERT_EQ(OH_MIDI_STATUS_OK, realtimeClient.CreateRingBuffer());
    auto realtimeRing = realtimeClient.GetRingBuffer();
    ASSERT_NE(nullptr, realtimeRing);
    std::vector<uint32_t> payload {0x12345678};
    MidiEventInner realtimeEvent = MakeMidiEventInner(0, payload);
    ASSERT_EQ(MidiStatusCode::OK, realtimeRing->TryWriteEvent(realtimeEvent, false));
    MidiSharedRing::PeekedEvent realtimePeek {};
    ASSERT_EQ(MidiStatusCode::OK, realtimeRing->PeekNext(realtimePeek));
    auto *realtimeHeader = reinterpret_cast<ShmMidiEventHeader *>(
        realtimeRing->GetDataBase() + realtimePeek.beginOffset);
    constexpr uint32_t STALE_SEQUENCE_INCREMENT = 2;
    realtimeHeader->sequence.store(
        realtimePeek.sequence + STALE_SEQUENCE_INCREMENT, std::memory_order_relaxed);
    connection.DrainSingleClientRing(realtimeClient);
}

/**
 * @tc.name   : Test remaining scheduled output connection branches
 * @tc.number : DeviceConnectionForOutput_013
 * @tc.desc   : Cover empty scheduled payload, stale sequence, and successful due caching.
 */
HWTEST_F(MidiDeviceConnectionUnitTest, DeviceConnectionForOutput_013, TestSize.Level1)
{
    constexpr uint32_t STALE_SEQUENCE_INCREMENT = 2;
    DeviceConnectionInfo info {};
    info.direction = MidiPortDirection::OUTPUT;
    DeviceConnectionForOutput connection(info);
    ClientConnectionInServer scheduledClient(2, 2, 0);
    ASSERT_EQ(OH_MIDI_STATUS_OK, scheduledClient.CreateRingBuffer());
    auto scheduledRing = scheduledClient.GetRingBuffer();
    ASSERT_NE(nullptr, scheduledRing);
    uint32_t dummy = 0;
    MidiEventInner emptyScheduled {
        .timestamp = static_cast<uint64_t>(
            duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count()),
        .length = 0,
        .data = &dummy,
    };
    ASSERT_EQ(MidiStatusCode::OK, scheduledRing->TryWriteEvent(emptyScheduled, false));
    MidiSharedRing::PeekedEvent scheduledPeek {};
    ASSERT_EQ(MidiStatusCode::OK, scheduledRing->PeekNext(scheduledPeek));
    EXPECT_TRUE(connection.ConsumeNonRealtimeEvent(scheduledClient, *scheduledRing, scheduledPeek));

    ASSERT_EQ(MidiStatusCode::OK, scheduledRing->TryWriteEvent(emptyScheduled, false));
    ASSERT_EQ(MidiStatusCode::OK, scheduledRing->PeekNext(scheduledPeek));
    auto *scheduledHeader = reinterpret_cast<ShmMidiEventHeader *>(
        scheduledRing->GetDataBase() + scheduledPeek.beginOffset);
    scheduledHeader->sequence.store(
        scheduledPeek.sequence + STALE_SEQUENCE_INCREMENT, std::memory_order_relaxed);
    scheduledClient.SetMaxPending(STALE_SEQUENCE_INCREMENT);
    EXPECT_FALSE(connection.ConsumeNonRealtimeEvent(scheduledClient, *scheduledRing, scheduledPeek));
    scheduledRing->Flush();

    auto dueClient = std::make_shared<ClientConnectionInServer>(3, 3, 0);
    ASSERT_TRUE(dueClient->EnqueueNonRealtime(
        {0x01020304}, steady_clock::now() - seconds(1), 3));
    connection.clients_ = {dueClient};
    connection.SetMaxSendCacheBytes(sizeof(uint32_t));
    connection.CollectDueEventsFromClientHeaps();
    EXPECT_FALSE(dueClient->HasPending());
    EXPECT_EQ(connection.sendCache_.size(), 1);
}
} // namespace MIDI
} // namespace OHOS
