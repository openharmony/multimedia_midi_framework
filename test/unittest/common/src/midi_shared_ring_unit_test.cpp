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

#ifndef LOG_TAG
#define LOG_TAG "MidiSharedRingUnitTest"
#endif

#include "midi_shared_ring_unit_test.h"

#include <unistd.h>
#include <cstdint>
#include <cstddef>
#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "ashmem.h"
#include "message_parcel.h"

using namespace testing::ext;

namespace OHOS {
namespace MIDI {

namespace {
constexpr int32_t INVALID_FD = -1;
// midi_shared_ring.cpp 内部 MAX_MMAP_BUFFER_SIZE = 0x2000
constexpr uint32_t MAX_MMAP_BUFFER_SIZE = 0x2000;
} // namespace

void MidiSharedRingUnitTest::SetUpTestCase(void) {}

void MidiSharedRingUnitTest::TearDownTestCase(void) {}

void MidiSharedRingUnitTest::SetUp(void) {}

void MidiSharedRingUnitTest::TearDown(void) {}

static void FillU32(std::vector<uint32_t> &buf, uint32_t base)
{
    for (size_t i = 0; i < buf.size(); ++i) {
        buf[i] = base + static_cast<uint32_t>(i);
    }
}

/**
 * @tc.name   : Test SharedMidiRing Init API
 * @tc.number : SharedMidiRingInit_001
 * @tc.desc   : Init with local shared memory (dataFd = -1).
 */
HWTEST_F(MidiSharedRingUnitTest, SharedMidiRingInit_001, TestSize.Level0)
{
    constexpr uint32_t RING_CAPACITY_BYTES = 256;
    SharedMidiRing ring(RING_CAPACITY_BYTES);

    int32_t ret = ring.Init(INVALID_FD);
    EXPECT_EQ(MIDI_STATUS_OK, ret);

    auto *ctrl = ring.GetControlHeader();
    ASSERT_NE(nullptr, ctrl);
    EXPECT_EQ(RING_CAPACITY_BYTES, ctrl->capacity);
    EXPECT_EQ(0u, ctrl->readPosition.load());
    EXPECT_EQ(0u, ctrl->writePosition.load());

    EXPECT_NE(nullptr, ring.GetDataBase());
    EXPECT_NE(nullptr, ring.GetFutex());
    EXPECT_TRUE(ring.IsEmpty());
    EXPECT_EQ(RING_CAPACITY_BYTES, ring.GetCapacity());
}

/**
 * @tc.name   : Test SharedMidiRing Init API
 * @tc.number : SharedMidiRingInit_002
 * @tc.desc   : Init with remote fd created by ashmem.
 */
HWTEST_F(MidiSharedRingUnitTest, SharedMidiRingInit_002, TestSize.Level0)
{
    constexpr uint32_t RING_CAPACITY_BYTES = 512;
    const size_t totalSize = sizeof(ControlHeader) + static_cast<size_t>(RING_CAPACITY_BYTES);

    int fd = AshmemCreate("midi_shared_buffer_ut", totalSize);
    ASSERT_GT(fd, 2);

    SharedMidiRing ring(RING_CAPACITY_BYTES);
    int32_t ret = ring.Init(fd);
    EXPECT_EQ(MIDI_STATUS_OK, ret);

    auto *ctrl = ring.GetControlHeader();
    ASSERT_NE(nullptr, ctrl);
    EXPECT_EQ(RING_CAPACITY_BYTES, ctrl->capacity);
    EXPECT_EQ(0u, ctrl->readPosition.load());
    EXPECT_EQ(0u, ctrl->writePosition.load());

    EXPECT_NE(nullptr, ring.GetDataBase());
    EXPECT_NE(nullptr, ring.GetFutex());
    EXPECT_TRUE(ring.IsEmpty());

    // Init 内部会 dup(fd) 再 mmap；这里关闭原 fd 不应影响 ring 使用
    close(fd);
}

/**
 * @tc.name   : Test SharedMidiRing Init API
 * @tc.number : SharedMidiRingInit_003
 * @tc.desc   : Init with zero ring capacity (edge case).
 */
HWTEST_F(MidiSharedRingUnitTest, SharedMidiRingInit_003, TestSize.Level0)
{
    constexpr uint32_t RING_CAPACITY_BYTES = 0;
    SharedMidiRing ring(RING_CAPACITY_BYTES);

    int32_t ret = ring.Init(INVALID_FD);
    EXPECT_EQ(MIDI_STATUS_OK, ret);

    auto *ctrl = ring.GetControlHeader();
    ASSERT_NE(nullptr, ctrl);
    EXPECT_EQ(RING_CAPACITY_BYTES, ctrl->capacity);
    EXPECT_EQ(0u, ctrl->readPosition.load());
    EXPECT_EQ(0u, ctrl->writePosition.load());

    EXPECT_NE(nullptr, ring.GetDataBase());
    EXPECT_NE(nullptr, ring.GetFutex());
    EXPECT_TRUE(ring.IsEmpty());
    EXPECT_EQ(RING_CAPACITY_BYTES, ring.GetCapacity());
}

/**
 * @tc.name   : Test SharedMidiRing Init API
 * @tc.number : SharedMidiRingInit_004
 * @tc.desc   : Init failed when totalMemorySize_ exceeds MAX_MMAP_BUFFER_SIZE.
 */
HWTEST_F(MidiSharedRingUnitTest, SharedMidiRingInit_004, TestSize.Level0)
{
    // totalMemorySize_ = sizeof(ControlHeader) + ringCapacityBytes
    // 这里 ringCapacityBytes >= MAX_MMAP_BUFFER_SIZE，必然超过上限
    constexpr uint32_t TOO_LARGE_RING_CAPACITY = MAX_MMAP_BUFFER_SIZE;

    SharedMidiRing ring(TOO_LARGE_RING_CAPACITY);
    int32_t ret = ring.Init(INVALID_FD);

    EXPECT_NE(MIDI_STATUS_OK, ret);
    EXPECT_EQ(nullptr, ring.GetControlHeader());
    EXPECT_EQ(nullptr, ring.GetFutex());
    EXPECT_EQ(nullptr, ring.GetDataBase());
}

/**
 * @tc.name   : Test SharedMidiRing Init API
 * @tc.number : SharedMidiRingInit_005
 * @tc.desc   : Init called twice should reset read/write positions.
 */
HWTEST_F(MidiSharedRingUnitTest, SharedMidiRingInit_005, TestSize.Level0)
{
    constexpr uint32_t RING_CAPACITY_BYTES = 256;
    SharedMidiRing ring(RING_CAPACITY_BYTES);

    EXPECT_EQ(MIDI_STATUS_OK, ring.Init(INVALID_FD));
    auto *ctrl = ring.GetControlHeader();
    ASSERT_NE(nullptr, ctrl);

    // Manually move indices to non-zero then Init again.
    ctrl->readPosition.store(7);
    ctrl->writePosition.store(11);

    EXPECT_EQ(MIDI_STATUS_OK, ring.Init(INVALID_FD));
    ctrl = ring.GetControlHeader();
    ASSERT_NE(nullptr, ctrl);
    EXPECT_EQ(0u, ctrl->readPosition.load());
    EXPECT_EQ(0u, ctrl->writePosition.load());
    EXPECT_EQ(RING_CAPACITY_BYTES, ctrl->capacity);
}

/**
 * @tc.name   : Test SharedMidiRing CreateFromRemote API
 * @tc.number : SharedMidiRingCreateFromRemote_001
 * @tc.desc   : CreateFromRemote should return nullptr when fd is invalid (<=2).
 */
HWTEST_F(MidiSharedRingUnitTest, SharedMidiRingCreateFromRemote_001, TestSize.Level0)
{
    constexpr uint32_t RING_CAPACITY_BYTES = 128;
    auto ring = SharedMidiRing::CreateFromRemote(RING_CAPACITY_BYTES, 2); // STDERR_FILENO
    EXPECT_EQ(nullptr, ring);
}

/**
 * @tc.name   : Test SharedMidiRing Marshalling & Unmarshalling
 * @tc.number : SharedMidiRingMarshalling_001
 * @tc.desc   : Marshalling then Unmarshalling should succeed and produce a usable ring.
 */
HWTEST_F(MidiSharedRingUnitTest, SharedMidiRingMarshalling_001, TestSize.Level0)
{
    constexpr uint32_t RING_CAPACITY_BYTES = 256;
    auto ring = SharedMidiRing::CreateFromLocal(RING_CAPACITY_BYTES);
    ASSERT_NE(nullptr, ring);

    MessageParcel parcel;
    ASSERT_TRUE(ring->Marshalling(parcel));
    auto *out = SharedMidiRing::Unmarshalling(parcel);
    ASSERT_NE(nullptr, out);

    EXPECT_EQ(RING_CAPACITY_BYTES, out->GetCapacity());
    EXPECT_TRUE(out->IsEmpty());
    EXPECT_NE(nullptr, out->GetControlHeader());
    EXPECT_NE(nullptr, out->GetDataBase());
    EXPECT_NE(nullptr, out->GetFutex());
    delete out;
}

static MidiEventInner MakeEvent(uint64_t ts, const std::vector<uint32_t> &payload)
{
    MidiEventInner ev{};
    ev.timestamp = ts;
    ev.length = payload.size();
    ev.data = payload.data();
    return ev;
}

/**
 * @tc.name   : Test SharedMidiRing TryWriteEvents API
 * @tc.number : SharedMidiRingTryWriteEvents_001
 * @tc.desc   : eventCount == 0 should return INVALID_ARGUMENT and write 0 events.
 */
HWTEST_F(MidiSharedRingUnitTest, SharedMidiRingTryWriteEvents_001, TestSize.Level0)
{
    SharedMidiRing ring(256);
    ASSERT_EQ(MIDI_STATUS_OK, ring.Init(INVALID_FD));

    uint32_t written = 123;
    auto ret = ring.TryWriteEvents(nullptr, 0, &written, false);
    EXPECT_EQ(0u, written);
    EXPECT_EQ(MidiStatusCode::WOULD_BLOCK, ret);
    EXPECT_TRUE(ring.IsEmpty());
}

/**
 * @tc.name   : Test SharedMidiRing TryWriteEvents API
 * @tc.number : SharedMidiRingTryWriteEvents_002
 * @tc.desc   : events == nullptr and eventCount > 0 should return INVALID_ARGUMENT.
 */
HWTEST_F(MidiSharedRingUnitTest, SharedMidiRingTryWriteEvents_002, TestSize.Level0)
{
    SharedMidiRing ring(256);
    ASSERT_EQ(MIDI_STATUS_OK, ring.Init(INVALID_FD));

    uint32_t written = 0;
    auto ret = ring.TryWriteEvents(nullptr, 1, &written, false);
    EXPECT_EQ(MidiStatusCode::INVALID_ARGUMENT, ret);
    EXPECT_EQ(0u, written);
    EXPECT_TRUE(ring.IsEmpty());
}

/**
 * @tc.name   : Test SharedMidiRing TryWriteEvents API
 * @tc.number : SharedMidiRingTryWriteEvents_003
 * @tc.desc   : capacity too small should return SHM_BROKEN.
 */
HWTEST_F(MidiSharedRingUnitTest, SharedMidiRingTryWriteEvents_003, TestSize.Level0)
{
    SharedMidiRing ring(0);
    ASSERT_EQ(MIDI_STATUS_OK, ring.Init(INVALID_FD));

    std::vector<uint32_t> payload{0x11223344};
    MidiEventInner ev = MakeEvent(0, payload);

    uint32_t written = 0;
    auto ret = ring.TryWriteEvents(&ev, 1, &written, false);
    EXPECT_EQ(MidiStatusCode::SHM_BROKEN, ret);
    EXPECT_EQ(0u, written);
}

/**
 * @tc.name   : Test SharedMidiRing TryWriteEvents API
 * @tc.number : SharedMidiRingTryWriteEvents_004
 * @tc.desc   : invalid event (data == nullptr) should not write anything and return WOULD_BLOCK.
 */
HWTEST_F(MidiSharedRingUnitTest, SharedMidiRingTryWriteEvents_004, TestSize.Level0)
{
    SharedMidiRing ring(256);
    ASSERT_EQ(MIDI_STATUS_OK, ring.Init(INVALID_FD));

    MidiEventInner ev{};
    ev.timestamp = 0;
    ev.length = 1;
    ev.data = nullptr; // const uint32_t* 也可以置空

    uint32_t written = 99;
    auto ret = ring.TryWriteEvents(&ev, 1, &written, false);
    EXPECT_EQ(MidiStatusCode::WOULD_BLOCK, ret);
    EXPECT_EQ(0u, written);
    EXPECT_TRUE(ring.IsEmpty());
}

/**
 * @tc.name   : Test SharedMidiRing TryWriteEvents API
 * @tc.number : SharedMidiRingTryWriteEvents_005
 * @tc.desc   : write single event successfully.
 */
HWTEST_F(MidiSharedRingUnitTest, SharedMidiRingTryWriteEvents_005, TestSize.Level0)
{
    SharedMidiRing ring(256);
    ASSERT_EQ(MIDI_STATUS_OK, ring.Init(INVALID_FD));

    std::vector<uint32_t> payload{0x11111111, 0x22222222};
    MidiEventInner ev = MakeEvent(123, payload);

    uint32_t written = 0;
    auto ret = ring.TryWriteEvents(&ev, 1, &written, false);
    EXPECT_EQ(MidiStatusCode::OK, ret);
    EXPECT_EQ(1u, written);
    EXPECT_FALSE(ring.IsEmpty());
}

/**
 * @tc.name   : Test SharedMidiRing TryWriteEvents API
 * @tc.number : SharedMidiRingTryWriteEvents_006
 * @tc.desc   : partial write when ring free space not enough for all events.
 */
HWTEST_F(MidiSharedRingUnitTest, SharedMidiRingTryWriteEvents_006, TestSize.Level0)
{
    SharedMidiRing ring(64);
    ASSERT_EQ(MIDI_STATUS_OK, ring.Init(INVALID_FD));

    std::vector<uint32_t> payload1(8, 0x11111111);
    std::vector<uint32_t> payload2(8, 0xaaaaaaaa);

    MidiEventInner events[2] = {MakeEvent(1, payload1), MakeEvent(2, payload2)};

    uint32_t written = 0;
    auto ret = ring.TryWriteEvents(events, 2, &written);
    EXPECT_EQ(1u, written);
    EXPECT_EQ(MidiStatusCode::WOULD_BLOCK, ret);
}

/**
 * @tc.name   : Test SharedMidiRing TryWriteEvents API
 * @tc.number : SharedMidiRingTryWriteEvents_007
 * @tc.desc   : cover wrap marker branch in UpdateWriteIndexIfNeed.
 */
HWTEST_F(MidiSharedRingUnitTest, SharedMidiRingTryWriteEvents_007, TestSize.Level0)
{
    SharedMidiRing ring(128);
    ASSERT_EQ(MIDI_STATUS_OK, ring.Init(INVALID_FD));

    // payload1: 21 words => 84 bytes payload, totalBytes = 16 + 84 = 100
    std::vector<uint32_t> payload1(21, 0x1);
    MidiEventInner ev1 = MakeEvent(10, payload1);

    uint32_t written = 0;
    ASSERT_EQ(MidiStatusCode::OK, ring.TryWriteEvents(&ev1, 1, &written, false));
    ASSERT_EQ(1u, written);

    const uint32_t writeAfterEv1 = ring.GetWritePosition();

    // release space to make event2 possible while tail is insufficient
    auto *ctrl = ring.GetControlHeader();
    ASSERT_NE(nullptr, ctrl);
    ctrl->readPosition.store(64);

    std::vector<uint32_t> payload2{0xa, 0xb, 0xc, 0xd};
    MidiEventInner ev2 = MakeEvent(20, payload2);

    ASSERT_EQ(MidiStatusCode::OK, ring.TryWriteEvents(&ev2, 1, &written, false));
    ASSERT_EQ(1u, written);

    auto *base = ring.GetDataBase();
    ASSERT_NE(nullptr, base);
    auto *wrapHdr = reinterpret_cast<ShmMidiEventHeader *>(base + writeAfterEv1);
    EXPECT_EQ(SHM_EVENT_FLAG_WRAP, wrapHdr->flags);
    EXPECT_EQ(0u, wrapHdr->length);
}

/**
 * @tc.name   : Test SharedMidiRing TryWriteEvents API
 * @tc.number : SharedMidiRingTryWriteEvents_008
 * @tc.desc   : length == 0 should be accepted, payload copy skipped (WriteEvent early return).
 */
HWTEST_F(MidiSharedRingUnitTest, SharedMidiRingTryWriteEvents_008, TestSize.Level0)
{
    SharedMidiRing ring(256);
    ASSERT_EQ(MIDI_STATUS_OK, ring.Init(INVALID_FD));

    uint32_t dummyWord = 0x12345678;
    MidiEventInner ev{};
    ev.timestamp = 77;
    ev.length = 0;
    ev.data = &dummyWord; // ValidateOneEvent requires data != nullptr even if length==0

    uint32_t written = 0;
    auto ret = ring.TryWriteEvents(&ev, 1, &written, false);
    EXPECT_EQ(MidiStatusCode::OK, ret);
    EXPECT_EQ(1u, written);

    SharedMidiRing::PeekedEvent peek;
    EXPECT_EQ(MidiStatusCode::OK, ring.PeekNext(peek));
    EXPECT_EQ(77u, peek.timestamp);
    EXPECT_EQ(0u, peek.length);
}

/**
 * @tc.name   : Test SharedMidiRing TryWriteEvent API
 * @tc.number : SharedMidiRingTryWriteEvent1_001
 * @tc.desc   : write single event successfully.
 */
HWTEST_F(MidiSharedRingUnitTest, SharedMidiRingTryWriteEvent_001, TestSize.Level0)
{
    SharedMidiRing ring(256);
    ASSERT_EQ(MIDI_STATUS_OK, ring.Init(INVALID_FD));

    std::vector<uint32_t> payload{0x11111111, 0x22222222};
    MidiEventInner ev = MakeEvent(123, payload);

    EXPECT_EQ(MidiStatusCode::OK, ring.TryWriteEvent(ev));
    EXPECT_FALSE(ring.IsEmpty());
}

//==================== PeekNext / CommitRead / DrainToBatch ====================//

/**
 * @tc.name   : Test SharedMidiRing PeekNext API
 * @tc.number : SharedMidiRingPeekNext_001
 * @tc.desc   : empty ring -> WOULD_BLOCK.
 */
HWTEST_F(MidiSharedRingUnitTest, SharedMidiRingPeekNext_001, TestSize.Level0)
{
    SharedMidiRing ring(256);
    ASSERT_EQ(MIDI_STATUS_OK, ring.Init(INVALID_FD));

    SharedMidiRing::PeekedEvent peek;
    EXPECT_EQ(MidiStatusCode::WOULD_BLOCK, ring.PeekNext(peek));
}

/**
 * @tc.name   : Test SharedMidiRing PeekNext API
 * @tc.number : SharedMidiRingPeekNext_002
 * @tc.desc   : capacity too small -> SHM_BROKEN.
 */
HWTEST_F(MidiSharedRingUnitTest, SharedMidiRingPeekNext_002, TestSize.Level0)
{
    SharedMidiRing ring(0);
    ASSERT_EQ(MIDI_STATUS_OK, ring.Init(INVALID_FD));

    SharedMidiRing::PeekedEvent peek;
    EXPECT_EQ(MidiStatusCode::SHM_BROKEN, ring.PeekNext(peek));
}

/**
 * @tc.name   : Test SharedMidiRing PeekNext API
 * @tc.number : SharedMidiRingPeekNext_003
 * @tc.desc   : invalid offsets -> SHM_BROKEN.
 */
HWTEST_F(MidiSharedRingUnitTest, SharedMidiRingPeekNext_003, TestSize.Level0)
{
    SharedMidiRing ring(128);
    ASSERT_EQ(MIDI_STATUS_OK, ring.Init(INVALID_FD));

    auto *ctrl = ring.GetControlHeader();
    ASSERT_NE(nullptr, ctrl);
    ctrl->readPosition.store(128); // invalid offset (==cap)
    ctrl->writePosition.store(0);

    SharedMidiRing::PeekedEvent peek;
    EXPECT_EQ(MidiStatusCode::SHM_BROKEN, ring.PeekNext(peek));
}

/**
 * @tc.name   : Test SharedMidiRing PeekNext/CommitRead API
 * @tc.number : SharedMidiRingPeekNext_004
 * @tc.desc   : wrap marker should be consumed and continue to next event.
 */
HWTEST_F(MidiSharedRingUnitTest, SharedMidiRingPeekNext_004, TestSize.Level0)
{
    SharedMidiRing ring(128);
    ASSERT_EQ(MIDI_STATUS_OK, ring.Init(INVALID_FD));

    // event1 totalBytes = 16 + 21*4 = 100, so writePosition becomes 100.
    std::vector<uint32_t> payload1(21, 0);
    FillU32(payload1, 0x10);
    MidiEventInner ev1 = MakeEvent(10, payload1);

    uint32_t written = 0;
    ASSERT_EQ(MidiStatusCode::OK, ring.TryWriteEvents(&ev1, 1, &written, false));
    ASSERT_EQ(1u, written);
    uint32_t writeAfterEv1 = ring.GetWritePosition();
    ASSERT_EQ(100u, writeAfterEv1);

    // Read event1 first.
    SharedMidiRing::PeekedEvent p1{};
    ASSERT_EQ(MidiStatusCode::OK, ring.PeekNext(p1));
    EXPECT_EQ(10u, p1.timestamp);
    EXPECT_EQ(21u, p1.length);
    ring.CommitRead(p1);
    EXPECT_EQ(writeAfterEv1, ring.GetReadPosition());

    std::vector<uint32_t> payload2(4, 0);
    FillU32(payload2, 0x20);
    MidiEventInner ev2 = MakeEvent(20, payload2);
    ASSERT_EQ(MidiStatusCode::OK, ring.TryWriteEvents(&ev2, 1, &written, false));
    ASSERT_EQ(1u, written);

    // Now readIndex points to WRAP header; PeekNext should consume it and return event2 at offset 0.
    SharedMidiRing::PeekedEvent p2{};
    ASSERT_EQ(MidiStatusCode::OK, ring.PeekNext(p2));
    EXPECT_EQ(20u, p2.timestamp);
    EXPECT_EQ(4u, p2.length);
    EXPECT_EQ(0u, p2.beginOffset);
}

/**
 * @tc.name   : Test SharedMidiRing PeekNext API
 * @tc.number : SharedMidiRingPeekNext_005
 * @tc.desc   : corrupted header (needed > cap-1) -> SHM_BROKEN.
 */
HWTEST_F(MidiSharedRingUnitTest, SharedMidiRingPeekNext_005, TestSize.Level0)
{
    SharedMidiRing ring(128);
    ASSERT_EQ(MIDI_STATUS_OK, ring.Init(INVALID_FD));

    auto *ctrl = ring.GetControlHeader();
    ASSERT_NE(nullptr, ctrl);
    ctrl->readPosition.store(0);
    ctrl->writePosition.store(32); // not empty

    auto *base = ring.GetDataBase();
    ASSERT_NE(nullptr, base);

    auto *hdr = reinterpret_cast<ShmMidiEventHeader *>(base);
    hdr->timestamp = 1;
    hdr->flags = SHM_EVENT_FLAG_NONE;
    hdr->length = 1000; // needed way larger than cap-1 => SHM_BROKEN

    SharedMidiRing::PeekedEvent peek;
    EXPECT_EQ(MidiStatusCode::SHM_BROKEN, ring.PeekNext(peek));
}

/**
 * @tc.name   : Test SharedMidiRing CommitRead API
 * @tc.number : SharedMidiRingCommitRead_001
 * @tc.desc   : endOffset >= capacity should wrap to 0.
 */
HWTEST_F(MidiSharedRingUnitTest, SharedMidiRingCommitRead_001, TestSize.Level0)
{
    SharedMidiRing ring(128);
    ASSERT_EQ(MIDI_STATUS_OK, ring.Init(INVALID_FD));

    auto *ctrl = ring.GetControlHeader();
    ASSERT_NE(nullptr, ctrl);
    ctrl->readPosition.store(10);

    SharedMidiRing::PeekedEvent ev{};
    ev.endOffset = 128; // == capacity
    ring.CommitRead(ev);
    EXPECT_EQ(0u, ring.GetReadPosition());

    ctrl->readPosition.store(10);
    ev.endOffset = 129; // > capacity
    ring.CommitRead(ev);
    EXPECT_EQ(0u, ring.GetReadPosition());
}

/**
 * @tc.name   : Test SharedMidiRing DrainToBatch API
 * @tc.number : SharedMidiRingDrainToBatch_001
 * @tc.desc   : drain all events when maxEvents==0.
 */
HWTEST_F(MidiSharedRingUnitTest, SharedMidiRingDrainToBatch_001, TestSize.Level0)
{
    SharedMidiRing ring(256);
    ASSERT_EQ(MIDI_STATUS_OK, ring.Init(INVALID_FD));

    std::vector<uint32_t> p1(3, 0);
    FillU32(p1, 0x30);
    std::vector<uint32_t> p2(2, 0);
    FillU32(p2, 0x40);
    MidiEventInner evs[2] = {MakeEvent(1, p1), MakeEvent(2, p2)};

    uint32_t written = 0;
    ASSERT_EQ(MidiStatusCode::OK, ring.TryWriteEvents(evs, 2, &written, false));
    ASSERT_EQ(2u, written);

    std::vector<MidiEvent> out;
    std::vector<std::vector<uint32_t>> bufs;
    ring.DrainToBatch(out, bufs, 0);

    ASSERT_EQ(2u, out.size());
    ASSERT_EQ(2u, bufs.size());

    EXPECT_EQ(1u, out[0].timestamp);
    EXPECT_EQ(3u, out[0].length);
    EXPECT_EQ(p1, bufs[0]);

    EXPECT_EQ(2u, out[1].timestamp);
    EXPECT_EQ(2u, out[1].length);
    EXPECT_EQ(p2, bufs[1]);

    EXPECT_TRUE(ring.IsEmpty());
}

/**
 * @tc.name   : Test SharedMidiRing DrainToBatch API
 * @tc.number : SharedMidiRingDrainToBatch_002
 * @tc.desc   : respect maxEvents limit.
 */
HWTEST_F(MidiSharedRingUnitTest, SharedMidiRingDrainToBatch_002, TestSize.Level0)
{
    SharedMidiRing ring(256);
    ASSERT_EQ(MIDI_STATUS_OK, ring.Init(INVALID_FD));

    std::vector<uint32_t> p1(1, 0x111);
    std::vector<uint32_t> p2(1, 0x222);
    MidiEventInner evs[2] = {MakeEvent(1, p1), MakeEvent(2, p2)};

    uint32_t written = 0;
    ASSERT_EQ(MidiStatusCode::OK, ring.TryWriteEvents(evs, 2, &written, false));
    ASSERT_EQ(2u, written);

    std::vector<MidiEvent> out;
    std::vector<std::vector<uint32_t>> bufs;
    ring.DrainToBatch(out, bufs, 1);

    ASSERT_EQ(1u, out.size());
    ASSERT_EQ(1u, bufs.size());
    EXPECT_EQ(1u, out[0].timestamp);

    // still has one event
    SharedMidiRing::PeekedEvent peek;
    EXPECT_EQ(MidiStatusCode::OK, ring.PeekNext(peek));
    EXPECT_EQ(2u, peek.timestamp);
}

/**
 * @tc.name   : Test SharedMidiRing DrainToBatch API
 * @tc.number : SharedMidiRingDrainToBatch_003
 * @tc.desc   : stop draining when PeekNext returns SHM_BROKEN (corrupted header).
 */
HWTEST_F(MidiSharedRingUnitTest, SharedMidiRingDrainToBatch_003, TestSize.Level0)
{
    SharedMidiRing ring(128);
    ASSERT_EQ(MIDI_STATUS_OK, ring.Init(INVALID_FD));

    std::vector<uint32_t> p1(1, 0xabc);
    MidiEventInner ev1 = MakeEvent(1, p1);

    uint32_t written = 0;
    ASSERT_EQ(MidiStatusCode::OK, ring.TryWriteEvents(&ev1, 1, &written, false));
    ASSERT_EQ(1u, written);

    // corrupt next header at current write position,
    // and advance writePosition a bit to make ring non-empty after first commit.
    uint32_t corruptOff = ring.GetWritePosition();
    auto *ctrl = ring.GetControlHeader();
    ASSERT_NE(nullptr, ctrl);
    auto *base = ring.GetDataBase();
    ASSERT_NE(nullptr, base);

    auto *hdr = reinterpret_cast<ShmMidiEventHeader *>(base + corruptOff);
    hdr->timestamp = 9;
    hdr->flags = SHM_EVENT_FLAG_NONE;
    hdr->length = 999; // will cause SHM_BROKEN in BuildPeekedEvent

    ctrl->writePosition.store(corruptOff + 4); // keep non-empty

    std::vector<MidiEvent> out;
    std::vector<std::vector<uint32_t>> bufs;
    ring.DrainToBatch(out, bufs, 0);

    EXPECT_EQ(1u, out.size());
    EXPECT_EQ(1u, bufs.size());

    // After draining first event, read position should now point to corrupted header offset.
    EXPECT_EQ(corruptOff, ring.GetReadPosition());
}
} // namespace MIDI
} // namespace OHOS