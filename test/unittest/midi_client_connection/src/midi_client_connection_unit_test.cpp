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

#include <chrono>
#include <cstdint>
#include <gtest/gtest.h>
#include <vector>

#include "midi_client_connection.h"
#include "midi_client_connection_unit_test.h"
#include "midi_shared_ring.h"
#include "native_midi_base.h"

using namespace testing::ext;
using namespace std::chrono;

namespace OHOS {
namespace MIDI {
static MidiEventInner MakeMidiEventInner(uint64_t timestamp, const std::vector<uint32_t> &payloadWords)
{
    MidiEventInner midiEventInner{};
    midiEventInner.timestamp = timestamp;
    midiEventInner.length = payloadWords.size();
    midiEventInner.data = payloadWords.data();
    return midiEventInner;
}

/**
 * @tc.name   : Test ClientConnectionInServer Getters
 * @tc.number : ClientConnectionInServerGetters_001
 * @tc.desc   : Verify constructor and getters.
 */
HWTEST_F(MidiClientConnectionUnitTest, ClientConnectionInServerGetters_001, TestSize.Level0)
{
    constexpr uint32_t clientId = 1001;
    constexpr int64_t deviceHandle = 987654321;
    constexpr uint32_t portIndex = 3;

    ClientConnectionInServer clientConnection(clientId, deviceHandle, portIndex);

    EXPECT_EQ(deviceHandle, clientConnection.GetDeviceHandle());
    EXPECT_EQ(clientId, clientConnection.GetClientId());
    EXPECT_EQ(portIndex, static_cast<uint32_t>(clientConnection.GetPortIndex()));
    EXPECT_EQ(nullptr, clientConnection.GetRingBuffer());
}

/**
 * @tc.name   : Test ClientConnectionInServer CreateRingBuffer
 * @tc.number : ClientConnectionInServerCreateRingBuffer_001
 * @tc.desc   : Create ring buffer successfully and verify memset to zero.
 */
HWTEST_F(MidiClientConnectionUnitTest, ClientConnectionInServerCreateRingBuffer_001, TestSize.Level0)
{
    ClientConnectionInServer clientConnection(1, 2, 3);

    EXPECT_EQ(MIDI_STATUS_OK, clientConnection.CreateRingBuffer());

    std::shared_ptr<MidiSharedRing> sharedRing = clientConnection.GetRingBuffer();
    ASSERT_NE(nullptr, sharedRing);

    // Sanity: ring should be initialized and empty.
    EXPECT_TRUE(sharedRing->IsEmpty());
    EXPECT_GT(sharedRing->GetCapacity(), 0u);
    ASSERT_NE(nullptr, sharedRing->GetDataBase());

    // Verify memset_s executed: check a few bytes are 0.
    // (Do not scan whole buffer to keep UT fast.)
    const uint8_t *dataBase = sharedRing->GetDataBase();
    EXPECT_EQ(0u, dataBase[0]);
    EXPECT_EQ(0u, dataBase[1]);
    EXPECT_EQ(0u, dataBase[2]);
    EXPECT_EQ(0u, dataBase[3]);
}

/**
 * @tc.name   : Test ClientConnectionInServer TrySendToClient
 * @tc.number : ClientConnectionInServerTrySendToClient_001
 * @tc.desc   : TrySendToClient success path (write one event).
 */
HWTEST_F(MidiClientConnectionUnitTest, ClientConnectionInServerTrySendToClient_001, TestSize.Level0)
{
    ClientConnectionInServer clientConnection(10, 20, 30);
    ASSERT_EQ(MIDI_STATUS_OK, clientConnection.CreateRingBuffer());

    std::vector<uint32_t> payloadWords{0x11223344, 0x55667788, 0x99AABBCC};
    MidiEventInner midiEventInner = MakeMidiEventInner(12345, payloadWords);

    EXPECT_EQ(MIDI_STATUS_OK, clientConnection.TrySendToClient(midiEventInner));

    // Verify data is really in ring (PeekNext should succeed).
    std::shared_ptr<MidiSharedRing> sharedRing = clientConnection.GetRingBuffer();
    ASSERT_NE(nullptr, sharedRing);

    MidiSharedRing::PeekedEvent peekedEvent{};
    EXPECT_EQ(MidiStatusCode::OK, sharedRing->PeekNext(peekedEvent));
    EXPECT_EQ(12345u, peekedEvent.timestamp);
    EXPECT_EQ(payloadWords.size(), peekedEvent.length);
}

/**
 * @tc.name   : Test ClientConnectionInServer TrySendToClient
 * @tc.number : ClientConnectionInServerTrySendToClient_002
 * @tc.desc   : TrySendToClient failure path when ring buffer is full.
 */
HWTEST_F(MidiClientConnectionUnitTest, ClientConnectionInServerTrySendToClient_002, TestSize.Level0)
{
    ClientConnectionInServer clientConnection(11, 22, 33);
    ASSERT_EQ(MIDI_STATUS_OK, clientConnection.CreateRingBuffer());

    // Use a relatively large payload to fill the ring quickly and deterministically.
    // (Payload size tuned so several writes succeed then ring becomes full.)
    std::vector<uint32_t> payloadWords(64, 0xA5A5A5A5); // 64 words => 256 bytes payload
    MidiEventInner midiEventInner = MakeMidiEventInner(1, payloadWords);

    // Try to fill ring by sending repeatedly.
    // We expect eventually one call returns MIDI_STATUS_SYSTEM_ERROR due to TryWriteEvent != OK.
    bool hasSeenFailure = false;
    int32_t lastReturnCode = MIDI_STATUS_OK;

    for (int32_t attemptIndex = 0; attemptIndex < 100; ++attemptIndex) {
        lastReturnCode = clientConnection.TrySendToClient(midiEventInner);
        if (lastReturnCode != MIDI_STATUS_OK) {
            hasSeenFailure = true;
            break;
        }
        // change timestamp a little (not required, but helps avoid any "dedup" style bugs if existed)
        midiEventInner.timestamp++;
    }

    ASSERT_TRUE(hasSeenFailure);
    EXPECT_EQ(MIDI_STATUS_SYSTEM_ERROR, lastReturnCode);
}

/**
 * @tc.name   : Test ClientConnectionInServer Pending Queue
 * @tc.number : ClientConnectionInServerPendingQueue_001
 * @tc.desc   : Enqueue/Peek/Pop ordering by due time, HasPending transitions.
 */
HWTEST_F(MidiClientConnectionUnitTest, ClientConnectionInServerPendingQueue_001, TestSize.Level0)
{
    ClientConnectionInServer clientConnection(101, 202, 303);

    // Initially empty.
    EXPECT_FALSE(clientConnection.HasPending());
    EXPECT_FALSE(clientConnection.IsPendingFull());
    EXPECT_EQ(nullptr, clientConnection.PeekPendingTop());

    ClientConnectionInServer::PendingEvent pendingEventOut{};
    EXPECT_FALSE(clientConnection.PopPendingTop(pendingEventOut));

    // Enqueue three events with different due times.
    std::vector<uint32_t> payloadData1 = {0x01, 0x02};
    std::vector<uint32_t> payloadData2 = {0x10, 0x11, 0x12};
    std::vector<uint32_t> payloadData3 = {0x20};

    const auto nowTime = steady_clock::now();
    const auto dueLater = nowTime + milliseconds(10);
    const auto dueEarliest = nowTime + milliseconds(1);
    const auto dueMiddle = nowTime + milliseconds(5);

    EXPECT_TRUE(clientConnection.EnqueueNonRealtime(std::move(payloadData1), dueLater, 100));
    EXPECT_TRUE(clientConnection.EnqueueNonRealtime(std::move(payloadData2), dueEarliest, 200));
    EXPECT_TRUE(clientConnection.EnqueueNonRealtime(std::move(payloadData3), dueMiddle, 300));

    EXPECT_TRUE(clientConnection.HasPending());
    EXPECT_FALSE(clientConnection.IsPendingFull());

    // Peek should return the earliest due event.
    const ClientConnectionInServer::PendingEvent *topPending = clientConnection.PeekPendingTop();
    ASSERT_NE(nullptr, topPending);
    EXPECT_EQ(200u, topPending->timestamp);
    ASSERT_EQ(3, topPending->data.size());
    EXPECT_EQ(0x10, topPending->data[0]);

    // Pop should return the same earliest event.
    ClientConnectionInServer::PendingEvent poppedEvent{};
    ASSERT_TRUE(clientConnection.PopPendingTop(poppedEvent));
    EXPECT_EQ(200u, poppedEvent.timestamp);
    ASSERT_EQ(3, poppedEvent.data.size());
    EXPECT_EQ(0x10, poppedEvent.data[0]);
    EXPECT_EQ(0x11, poppedEvent.data[1]);
    EXPECT_EQ(0x12, poppedEvent.data[2]);

    // Next top should be the middle due event.
    topPending = clientConnection.PeekPendingTop();
    ASSERT_NE(nullptr, topPending);
    EXPECT_EQ(300u, topPending->timestamp);

    // Pop remaining two.
    ASSERT_TRUE(clientConnection.PopPendingTop(poppedEvent));
    EXPECT_EQ(300u, poppedEvent.timestamp);

    ASSERT_TRUE(clientConnection.PopPendingTop(poppedEvent));
    EXPECT_EQ(100u, poppedEvent.timestamp);

    // Now empty again.
    EXPECT_FALSE(clientConnection.HasPending());
    EXPECT_EQ(nullptr, clientConnection.PeekPendingTop());
    EXPECT_FALSE(clientConnection.PopPendingTop(pendingEventOut));
}

/**
 * @tc.name   : Test ClientConnectionInServer Pending Full
 * @tc.number : ClientConnectionInServerPendingQueue_002
 * @tc.desc   : SetMaxPending and IsPendingFull/EnqueueNonRealtime failure.
 */
HWTEST_F(MidiClientConnectionUnitTest, ClientConnectionInServerPendingQueue_002, TestSize.Level0)
{
    ClientConnectionInServer clientConnection(111, 222, 333);
    clientConnection.SetMaxPending(1);

    EXPECT_FALSE(clientConnection.IsPendingFull());
    EXPECT_FALSE(clientConnection.HasPending());

    std::vector<uint32_t> payloadData = {0xAA, 0xBB, 0xCC};
    const auto dueTime = steady_clock::now() + milliseconds(3);

    EXPECT_TRUE(clientConnection.EnqueueNonRealtime(std::move(payloadData), dueTime, 999));
    EXPECT_TRUE(clientConnection.HasPending());
    EXPECT_TRUE(clientConnection.IsPendingFull());

    // Second enqueue should fail due to maxPending limit.
    EXPECT_FALSE(clientConnection.EnqueueNonRealtime(std::move(payloadData), dueTime, 1000));
    EXPECT_TRUE(clientConnection.HasPending()); // still has the first one

    ClientConnectionInServer::PendingEvent poppedEvent{};
    EXPECT_TRUE(clientConnection.PopPendingTop(poppedEvent));
    EXPECT_EQ(999u, poppedEvent.timestamp);

    // Now queue is empty again, not full.
    EXPECT_FALSE(clientConnection.HasPending());
    EXPECT_FALSE(clientConnection.IsPendingFull());
    EXPECT_EQ(nullptr, clientConnection.PeekPendingTop());
}

} // namespace MIDI
} // namespace OHOS
