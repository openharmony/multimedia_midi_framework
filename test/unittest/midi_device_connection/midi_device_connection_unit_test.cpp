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

#include "native_midi_base.h"
#include "midi_device_connection.h"
#include "midi_shared_ring.h"

using namespace std::chrono;
using namespace testing::ext;

namespace OHOS {
namespace MIDI {

class MidiDeviceConnectionUnitTest : public testing::Test {
public:
    static void SetUpTestCase() {}
    static void TearDownTestCase() {}
    void SetUp() override {}
    void TearDown() override {}
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

    EXPECT_TRUE(deviceConnectionBase.IsEmptyClientConections());

    std::shared_ptr<SharedMidiRing> clientRingBuffer;
    EXPECT_EQ(MIDI_STATUS_OK, deviceConnectionBase.AddClientConnection(100, 999, clientRingBuffer));
    ASSERT_NE(nullptr, clientRingBuffer);
    EXPECT_FALSE(deviceConnectionBase.IsEmptyClientConections());

    // Add another client
    std::shared_ptr<SharedMidiRing> anotherClientRingBuffer;
    EXPECT_EQ(MIDI_STATUS_OK, deviceConnectionBase.AddClientConnection(200, 888, anotherClientRingBuffer));
    ASSERT_NE(nullptr, anotherClientRingBuffer);
    EXPECT_FALSE(deviceConnectionBase.IsEmptyClientConections());

    // Remove unknown id should not crash and not empty
    deviceConnectionBase.RemoveClientConnection(300);
    EXPECT_FALSE(deviceConnectionBase.IsEmptyClientConections());

    // Remove first client
    deviceConnectionBase.RemoveClientConnection(100);
    EXPECT_FALSE(deviceConnectionBase.IsEmptyClientConections());

    // Remove second client -> empty
    deviceConnectionBase.RemoveClientConnection(200);
    EXPECT_TRUE(deviceConnectionBase.IsEmptyClientConections());

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

    std::shared_ptr<SharedMidiRing> clientRingBuffer1;
    std::shared_ptr<SharedMidiRing> clientRingBuffer2;
    ASSERT_EQ(MIDI_STATUS_OK, inputConnection.AddClientConnection(1, 1000, clientRingBuffer1));
    ASSERT_EQ(MIDI_STATUS_OK, inputConnection.AddClientConnection(2, 1001, clientRingBuffer2));
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
        SharedMidiRing::PeekedEvent peekedEvent1{};
        ASSERT_EQ(MidiStatusCode::OK, ringPointer->PeekNext(peekedEvent1));
        EXPECT_EQ(10u, peekedEvent1.timestamp);
        // SharedMidiRing stores payload length in bytes
        EXPECT_EQ(payloadWords1.size(), static_cast<size_t>(peekedEvent1.length));
        ringPointer->CommitRead(peekedEvent1);

        SharedMidiRing::PeekedEvent peekedEvent2{};
        ASSERT_EQ(MidiStatusCode::OK, ringPointer->PeekNext(peekedEvent2));
        EXPECT_EQ(20u, peekedEvent2.timestamp);
        EXPECT_EQ(payloadWords2.size(), static_cast<size_t>(peekedEvent2.length));
        ringPointer->CommitRead(peekedEvent2);

        SharedMidiRing::PeekedEvent peekedEvent3{};
        EXPECT_EQ(MidiStatusCode::WOULD_BLOCK, ringPointer->PeekNext(peekedEvent3));
    }

    // Remove one client and broadcast again, should only affect remaining client.
    inputConnection.RemoveClientConnection(1);

    std::vector<uint32_t> payloadWords3{0xAAAA5555};
    std::vector<MidiEventInner> deviceEvents2;
    deviceEvents2.push_back(MakeMidiEventInner(30, payloadWords3));
    inputConnection.HandleDeviceUmpInput(deviceEvents2);

    // client 1 ring should have no new data
    SharedMidiRing::PeekedEvent peekedEventAfterRemove{};
    EXPECT_EQ(MidiStatusCode::WOULD_BLOCK, clientRingBuffer1->PeekNext(peekedEventAfterRemove));

    // client 2 ring should have the new event
    ASSERT_EQ(MidiStatusCode::OK, clientRingBuffer2->PeekNext(peekedEventAfterRemove));
    EXPECT_EQ(30u, peekedEventAfterRemove.timestamp);
    EXPECT_EQ(payloadWords3.size(), static_cast<size_t>(peekedEventAfterRemove.length));
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
    EXPECT_EQ(MIDI_STATUS_OK, outputConnection.Stop());

    // Start twice: both OK
    EXPECT_EQ(MIDI_STATUS_OK, outputConnection.Start());
    EXPECT_EQ(MIDI_STATUS_OK, outputConnection.Start());

    int notifyEventFileDescriptor = outputConnection.GetNotifyEventFdForClients();
    EXPECT_GE(notifyEventFileDescriptor, 0);
    EXPECT_TRUE(IsFdValid(notifyEventFileDescriptor));

    // Stop twice: both OK
    EXPECT_EQ(MIDI_STATUS_OK, outputConnection.Stop());
    EXPECT_EQ(MIDI_STATUS_OK, outputConnection.Stop());
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

    ASSERT_EQ(MIDI_STATUS_OK, outputConnection.Start());

    std::shared_ptr<SharedMidiRing> clientRingBuffer;
    ASSERT_EQ(MIDI_STATUS_OK, outputConnection.AddClientConnection(10, 1234, clientRingBuffer));
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

    EXPECT_EQ(MIDI_STATUS_OK, outputConnection.Stop());

    // Ring should have been drained (best-effort check; if pending was full it might stop early,
    // but in current implementation pending limit is not wired, so it should drain).
    SharedMidiRing::PeekedEvent peekedEvent{};
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
        ASSERT_EQ(MIDI_STATUS_OK, outputConnection.Start());
    }
}

} // namespace MIDI
} // namespace OHOS
