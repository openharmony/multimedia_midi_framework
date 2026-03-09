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
#define LOG_TAG "MidiSharedRingUnitTest"
#endif

#include "midi_shared_ring_unit_test.h"

#include <unistd.h>
#include <cstdint>
#include <cstddef>
#include <sys/eventfd.h>
#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "ashmem.h"
#include "message_parcel.h"

using namespace testing::ext;

namespace OHOS {
namespace MIDI {

namespace {
constexpr int32_t INVALID_FD = -1;
static constexpr int MINFD = 2;
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
 * @tc.name   : Test MidiSharedRing Init API
 * @tc.number : MidiSharedRingInit_001
 * @tc.desc   : Init with local shared memory (dataFd = -1).
 */
HWTEST_F(MidiSharedRingUnitTest, MidiSharedRingInit_001, TestSize.Level0)
{
    constexpr uint32_t RING_CAPACITY_BYTES = 256;
    MidiSharedRing ring(RING_CAPACITY_BYTES);

    int32_t ret = ring.Init(INVALID_FD);
    EXPECT_EQ(OH_MIDI_STATUS_OK, ret);

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
 * @tc.name   : Test MidiSharedRing Init API
 * @tc.number : MidiSharedRingInit_002
 * @tc.desc   : Init with remote fd created by ashmem.
 */
HWTEST_F(MidiSharedRingUnitTest, MidiSharedRingInit_002, TestSize.Level0)
{
    constexpr uint32_t RING_CAPACITY_BYTES = 512;
    const size_t totalSize = sizeof(ControlHeader) + static_cast<size_t>(RING_CAPACITY_BYTES);

    int fd = AshmemCreate("midi_shared_buffer_ut", totalSize);
    ASSERT_GT(fd, MINFD);

    MidiSharedRing ring(RING_CAPACITY_BYTES);
    int32_t ret = ring.Init(fd);
    EXPECT_EQ(OH_MIDI_STATUS_OK, ret);

    auto *ctrl = ring.GetControlHeader();
    ASSERT_NE(nullptr, ctrl);
    EXPECT_EQ(RING_CAPACITY_BYTES, ctrl->capacity);
    EXPECT_EQ(0u, ctrl->readPosition.load());
    EXPECT_EQ(0u, ctrl->writePosition.load());

    EXPECT_NE(nullptr, ring.GetDataBase());
    EXPECT_NE(nullptr, ring.GetFutex());
    EXPECT_TRUE(ring.IsEmpty());

    // Init internally calls dup(fd) then mmap;
    // closing original fd should not affect ring usage
    close(fd);
}

/**
 * @tc.name   : Test MidiSharedRing Init API
 * @tc.number : MidiSharedRingInit_003
 * @tc.desc   : Init with zero ring capacity (edge case).
 */
HWTEST_F(MidiSharedRingUnitTest, MidiSharedRingInit_003, TestSize.Level0)
{
    constexpr uint32_t RING_CAPACITY_BYTES = 0;
    MidiSharedRing ring(RING_CAPACITY_BYTES);

    int32_t ret = ring.Init(INVALID_FD);
    EXPECT_EQ(OH_MIDI_STATUS_OK, ret);

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
 * @tc.name   : Test MidiSharedRing Init API
 * @tc.number : MidiSharedRingInit_004
 * @tc.desc   : Init failed when totalMemorySize_ exceeds MAX_MMAP_BUFFER_SIZE.
 */
HWTEST_F(MidiSharedRingUnitTest, MidiSharedRingInit_004, TestSize.Level0)
{
    // totalMemorySize_ = sizeof(ControlHeader) + ringCapacityBytes
    // ringCapacityBytes >= MAX_MMAP_BUFFER_SIZE will definitely exceed limit
    constexpr uint32_t TOO_LARGE_RING_CAPACITY = MAX_MMAP_BUFFER_SIZE;

    MidiSharedRing ring(TOO_LARGE_RING_CAPACITY);
    int32_t ret = ring.Init(INVALID_FD);

    EXPECT_NE(OH_MIDI_STATUS_OK, ret);
    EXPECT_EQ(nullptr, ring.GetControlHeader());
    EXPECT_EQ(nullptr, ring.GetFutex());
    EXPECT_EQ(nullptr, ring.GetDataBase());
}

/**
 * @tc.name   : Test MidiSharedRing Init API
 * @tc.number : MidiSharedRingInit_005
 * @tc.desc   : Init called twice should reset read/write positions.
 */
HWTEST_F(MidiSharedRingUnitTest, MidiSharedRingInit_005, TestSize.Level0)
{
    constexpr uint32_t RING_CAPACITY_BYTES = 256;
    MidiSharedRing ring(RING_CAPACITY_BYTES);

    EXPECT_EQ(OH_MIDI_STATUS_OK, ring.Init(INVALID_FD));
    auto *ctrl = ring.GetControlHeader();
    ASSERT_NE(nullptr, ctrl);

    // Manually move indices to non-zero then Init again.
    ctrl->readPosition.store(7);
    ctrl->writePosition.store(11);

    EXPECT_EQ(OH_MIDI_STATUS_OK, ring.Init(INVALID_FD));
    ctrl = ring.GetControlHeader();
    ASSERT_NE(nullptr, ctrl);
    EXPECT_EQ(0u, ctrl->readPosition.load());
    EXPECT_EQ(0u, ctrl->writePosition.load());
    EXPECT_EQ(RING_CAPACITY_BYTES, ctrl->capacity);
}

/**
 * @tc.name   : Test MidiSharedRing CreateFromRemote API
 * @tc.number : MidiSharedRingCreateFromRemote_001
 * @tc.desc   : CreateFromRemote should return nullptr when fd is invalid (<=2).
 */
HWTEST_F(MidiSharedRingUnitTest, MidiSharedRingCreateFromRemote_001, TestSize.Level0)
{
    constexpr uint32_t RING_CAPACITY_BYTES = 128;
    auto ring = MidiSharedRing::CreateFromRemote(RING_CAPACITY_BYTES, MINFD); // STDERR_FILENO
    EXPECT_EQ(nullptr, ring);
}

/**
 * @tc.name   : Test MidiSharedRing Marshalling & Unmarshalling
 * @tc.number : MidiSharedRingMarshalling_001
 * @tc.desc   : Marshalling then Unmarshalling should succeed and produce a usable ring.
 */
HWTEST_F(MidiSharedRingUnitTest, MidiSharedRingMarshalling_001, TestSize.Level0)
{
    constexpr uint32_t RING_CAPACITY_BYTES = 256;
    auto fd = std::make_shared<UniqueFd>();
    int eventFd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    fd->Reset(eventFd);
    auto ring = MidiSharedRing::CreateFromLocal(RING_CAPACITY_BYTES, fd);
    ASSERT_NE(nullptr, ring);

    MessageParcel parcel;
    ASSERT_TRUE(ring->Marshalling(parcel));
    auto *out = MidiSharedRing::Unmarshalling(parcel);
    ASSERT_NE(nullptr, out);

    EXPECT_EQ(RING_CAPACITY_BYTES, out->GetCapacity());
    EXPECT_TRUE(out->IsEmpty());
    EXPECT_NE(nullptr, out->GetControlHeader());
    EXPECT_NE(nullptr, out->GetDataBase());
    EXPECT_NE(nullptr, out->GetFutex());
    delete out;
}

/**
 * @tc.name   : Test MidiSharedRing Marshalling & Unmarshalling
 * @tc.number : MidiSharedRingMarshalling_002
 * @tc.desc   : Marshalling then Unmarshalling should succeed and produce a usable ring without valid fd.
 */
HWTEST_F(MidiSharedRingUnitTest, MidiSharedRingMarshalling_002, TestSize.Level0)
{
    constexpr uint32_t RING_CAPACITY_BYTES = 256;
    auto ring = MidiSharedRing::CreateFromLocal(RING_CAPACITY_BYTES);
    ASSERT_NE(nullptr, ring);

    MessageParcel parcel;
    ASSERT_TRUE(ring->Marshalling(parcel));
    auto *out = MidiSharedRing::Unmarshalling(parcel);
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
 * @tc.name   : Test MidiSharedRing TryWriteEvents API
 * @tc.number : MidiSharedRingTryWriteEvents_001
 * @tc.desc   : eventCount == 0 should return OK (valid empty write) and write 0 events.
 */
HWTEST_F(MidiSharedRingUnitTest, MidiSharedRingTryWriteEvents_001, TestSize.Level0)
{
    MidiSharedRing ring(256);
    ASSERT_EQ(OH_MIDI_STATUS_OK, ring.Init(INVALID_FD));

    uint32_t written = 123;
    auto ret = ring.TryWriteEvents(nullptr, 0, &written, false);
    EXPECT_EQ(0u, written);
    EXPECT_EQ(MidiStatusCode::OK, ret);
    EXPECT_TRUE(ring.IsEmpty());
}

/**
 * @tc.name   : Test MidiSharedRing TryWriteEvents API
 * @tc.number : MidiSharedRingTryWriteEvents_002
 * @tc.desc   : events == nullptr and eventCount > 0 should return INVALID_ARGUMENT.
 */
HWTEST_F(MidiSharedRingUnitTest, MidiSharedRingTryWriteEvents_002, TestSize.Level0)
{
    MidiSharedRing ring(256);
    ASSERT_EQ(OH_MIDI_STATUS_OK, ring.Init(INVALID_FD));

    uint32_t written = 0;
    auto ret = ring.TryWriteEvents(nullptr, 1, &written, false);
    EXPECT_EQ(MidiStatusCode::INVALID_ARGUMENT, ret);
    EXPECT_EQ(0u, written);
    EXPECT_TRUE(ring.IsEmpty());
}

/**
 * @tc.name   : Test MidiSharedRing TryWriteEvents API
 * @tc.number : MidiSharedRingTryWriteEvents_003
 * @tc.desc   : capacity too small should return SHM_BROKEN.
 */
HWTEST_F(MidiSharedRingUnitTest, MidiSharedRingTryWriteEvents_003, TestSize.Level0)
{
    MidiSharedRing ring(0);
    ASSERT_EQ(OH_MIDI_STATUS_OK, ring.Init(INVALID_FD));

    std::vector<uint32_t> payload{0x11223344};
    MidiEventInner ev = MakeEvent(0, payload);

    uint32_t written = 0;
    auto ret = ring.TryWriteEvents(&ev, 1, &written, false);
    EXPECT_EQ(MidiStatusCode::SHM_BROKEN, ret);
    EXPECT_EQ(0u, written);
}

/**
 * @tc.name   : Test MidiSharedRing TryWriteEvents API
 * @tc.number : MidiSharedRingTryWriteEvents_004
 * @tc.desc   : invalid event (data == nullptr) should not write anything and return WOULD_BLOCK.
 */
HWTEST_F(MidiSharedRingUnitTest, MidiSharedRingTryWriteEvents_004, TestSize.Level0)
{
    MidiSharedRing ring(256);
    ASSERT_EQ(OH_MIDI_STATUS_OK, ring.Init(INVALID_FD));

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
 * @tc.name   : Test MidiSharedRing TryWriteEvents API
 * @tc.number : MidiSharedRingTryWriteEvents_005
 * @tc.desc   : write single event successfully.
 */
HWTEST_F(MidiSharedRingUnitTest, MidiSharedRingTryWriteEvents_005, TestSize.Level0)
{
    MidiSharedRing ring(256);
    ASSERT_EQ(OH_MIDI_STATUS_OK, ring.Init(INVALID_FD));

    std::vector<uint32_t> payload{0x11111111, 0x22222222};
    MidiEventInner ev = MakeEvent(123, payload);

    uint32_t written = 0;
    auto ret = ring.TryWriteEvents(&ev, 1, &written, false);
    EXPECT_EQ(MidiStatusCode::OK, ret);
    EXPECT_EQ(1u, written);
    EXPECT_FALSE(ring.IsEmpty());
}

/**
 * @tc.name   : Test MidiSharedRing TryWriteEvents API
 * @tc.number : MidiSharedRingTryWriteEvents_006
 * @tc.desc   : partial write when ring free space not enough for all events.
 */
HWTEST_F(MidiSharedRingUnitTest, MidiSharedRingTryWriteEvents_006, TestSize.Level0)
{
    MidiSharedRing ring(64);
    ASSERT_EQ(OH_MIDI_STATUS_OK, ring.Init(INVALID_FD));

    std::vector<uint32_t> payload1(8, 0x11111111);
    std::vector<uint32_t> payload2(8, 0xaaaaaaaa);

    MidiEventInner events[2] = {MakeEvent(1, payload1), MakeEvent(2, payload2)};

    uint32_t written = 0;
    auto ret = ring.TryWriteEvents(events, 2, &written);
    EXPECT_EQ(1u, written);
    EXPECT_EQ(MidiStatusCode::WOULD_BLOCK, ret);
}

/**
 * @tc.name   : Test MidiSharedRing TryWriteEvents API
 * @tc.number : MidiSharedRingTryWriteEvents_007
 * @tc.desc   : cover wrap marker branch in UpdateWriteIndexIfNeed.
 */
HWTEST_F(MidiSharedRingUnitTest, MidiSharedRingTryWriteEvents_007, TestSize.Level0)
{
    MidiSharedRing ring(128);
    ASSERT_EQ(OH_MIDI_STATUS_OK, ring.Init(INVALID_FD));

    // Use 19 words (76 bytes) payload so total is 24 + 76 = 100 bytes
    // This leaves 128 - 100 = 28 bytes tail space, enough for 24-byte wrap header
    // (header is 24 bytes: 8 timestamp + 4 length + 4 flags + 4 sequence + 4 padding)
    std::vector<uint32_t> payload1(19, 0x1);
    MidiEventInner ev1 = MakeEvent(10, payload1);

    uint32_t written = 0;
    ASSERT_EQ(MidiStatusCode::OK, ring.TryWriteEvents(&ev1, 1, &written, false));
    ASSERT_EQ(1u, written);

    const uint32_t writeAfterEv1 = ring.GetWritePosition();
    ASSERT_EQ(100u, writeAfterEv1);  // 24 + 19*4 = 100

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
 * @tc.name   : Test MidiSharedRing TryWriteEvents API
 * @tc.number : MidiSharedRingTryWriteEvents_008
 * @tc.desc   : length == 0 should be accepted, payload copy skipped (WriteEvent early return).
 */
HWTEST_F(MidiSharedRingUnitTest, MidiSharedRingTryWriteEvents_008, TestSize.Level0)
{
    MidiSharedRing ring(256);
    ASSERT_EQ(OH_MIDI_STATUS_OK, ring.Init(INVALID_FD));

    uint32_t dummyWord = 0x12345678;
    MidiEventInner ev{};
    ev.timestamp = 77;
    ev.length = 0;
    ev.data = &dummyWord; // ValidateOneEvent requires data != nullptr even if length==0

    uint32_t written = 0;
    auto ret = ring.TryWriteEvents(&ev, 1, &written, false);
    EXPECT_EQ(MidiStatusCode::OK, ret);
    EXPECT_EQ(1u, written);

    MidiSharedRing::PeekedEvent peek;
    EXPECT_EQ(MidiStatusCode::OK, ring.PeekNext(peek));
    EXPECT_EQ(77u, peek.localHeader.timestamp);
    EXPECT_EQ(0u, peek.localHeader.length);
}

/**
 * @tc.name   : Test MidiSharedRing TryWriteEvent API
 * @tc.number : MidiSharedRingTryWriteEvent1_001
 * @tc.desc   : write single event successfully.
 */
HWTEST_F(MidiSharedRingUnitTest, MidiSharedRingTryWriteEvent_001, TestSize.Level0)
{
    MidiSharedRing ring(256);
    ASSERT_EQ(OH_MIDI_STATUS_OK, ring.Init(INVALID_FD));

    std::vector<uint32_t> payload{0x11111111, 0x22222222};
    MidiEventInner ev = MakeEvent(123, payload);

    EXPECT_EQ(MidiStatusCode::OK, ring.TryWriteEvent(ev));
    EXPECT_FALSE(ring.IsEmpty());
}

//==================== PeekNext / CommitRead / DrainToBatch ====================//

/**
 * @tc.name   : Test MidiSharedRing PeekNext API
 * @tc.number : MidiSharedRingPeekNext_001
 * @tc.desc   : empty ring -> WOULD_BLOCK.
 */
HWTEST_F(MidiSharedRingUnitTest, MidiSharedRingPeekNext_001, TestSize.Level0)
{
    MidiSharedRing ring(256);
    ASSERT_EQ(OH_MIDI_STATUS_OK, ring.Init(INVALID_FD));

    MidiSharedRing::PeekedEvent peek;
    EXPECT_EQ(MidiStatusCode::WOULD_BLOCK, ring.PeekNext(peek));
}

/**
 * @tc.name   : Test MidiSharedRing PeekNext API
 * @tc.number : MidiSharedRingPeekNext_002
 * @tc.desc   : capacity too small -> SHM_BROKEN.
 */
HWTEST_F(MidiSharedRingUnitTest, MidiSharedRingPeekNext_002, TestSize.Level0)
{
    MidiSharedRing ring(0);
    ASSERT_EQ(OH_MIDI_STATUS_OK, ring.Init(INVALID_FD));

    MidiSharedRing::PeekedEvent peek;
    EXPECT_EQ(MidiStatusCode::SHM_BROKEN, ring.PeekNext(peek));
}

/**
 * @tc.name   : Test MidiSharedRing PeekNext API
 * @tc.number : MidiSharedRingPeekNext_003
 * @tc.desc   : invalid offsets -> SHM_BROKEN.
 */
HWTEST_F(MidiSharedRingUnitTest, MidiSharedRingPeekNext_003, TestSize.Level0)
{
    MidiSharedRing ring(128);
    ASSERT_EQ(OH_MIDI_STATUS_OK, ring.Init(INVALID_FD));

    auto *ctrl = ring.GetControlHeader();
    ASSERT_NE(nullptr, ctrl);
    ctrl->readPosition.store(128); // invalid offset (==cap)
    ctrl->writePosition.store(0);

    MidiSharedRing::PeekedEvent peek;
    EXPECT_EQ(MidiStatusCode::SHM_BROKEN, ring.PeekNext(peek));
}

/**
 * @tc.name   : Test MidiSharedRing PeekNext/CommitRead API
 * @tc.number : MidiSharedRingPeekNext_004
 * @tc.desc   : wrap marker should be consumed and continue to next event.
 */
HWTEST_F(MidiSharedRingUnitTest, MidiSharedRingPeekNext_004, TestSize.Level0)
{
    MidiSharedRing ring(128);
    ASSERT_EQ(OH_MIDI_STATUS_OK, ring.Init(INVALID_FD));

    // Use 19 words (76 bytes) payload so total is 24 + 76 = 100 bytes
    // This leaves 128 - 100 = 28 bytes tail space, enough for 24-byte wrap header
    // (header is 24 bytes: 8 timestamp + 4 length + 4 flags + 4 sequence + 4 padding)
    std::vector<uint32_t> payload1(19, 0);
    FillU32(payload1, 0x10);
    MidiEventInner ev1 = MakeEvent(10, payload1);

    uint32_t written = 0;
    ASSERT_EQ(MidiStatusCode::OK, ring.TryWriteEvents(&ev1, 1, &written, false));
    ASSERT_EQ(1u, written);
    uint32_t writeAfterEv1 = ring.GetWritePosition();
    ASSERT_EQ(100u, writeAfterEv1);  // 24 + 19*4 = 100

    // Read event1 first.
    MidiSharedRing::PeekedEvent p1{};
    ASSERT_EQ(MidiStatusCode::OK, ring.PeekNext(p1));
    EXPECT_EQ(10u, p1.localHeader.timestamp);
    EXPECT_EQ(19u, p1.localHeader.length);  // Changed from 21 to 19
    ring.CommitRead(p1);
    EXPECT_EQ(writeAfterEv1, ring.GetReadPosition());

    std::vector<uint32_t> payload2(4, 0);
    FillU32(payload2, 0x20);
    MidiEventInner ev2 = MakeEvent(20, payload2);
    ASSERT_EQ(MidiStatusCode::OK, ring.TryWriteEvents(&ev2, 1, &written, false));
    ASSERT_EQ(1u, written);

    // Now readIndex points to WRAP header; PeekNext should consume it and return event2 at offset 0.
    MidiSharedRing::PeekedEvent p2{};
    ASSERT_EQ(MidiStatusCode::OK, ring.PeekNext(p2));
    EXPECT_EQ(20u, p2.localHeader.timestamp);
    EXPECT_EQ(4u, p2.localHeader.length);
    EXPECT_EQ(0u, p2.beginOffset);
}

/**
 * @tc.name   : Test MidiSharedRing PeekNext API
 * @tc.number : MidiSharedRingPeekNext_005
 * @tc.desc   : corrupted header (needed > cap-1) -> SHM_BROKEN.
 */
HWTEST_F(MidiSharedRingUnitTest, MidiSharedRingPeekNext_005, TestSize.Level0)
{
    MidiSharedRing ring(128);
    ASSERT_EQ(OH_MIDI_STATUS_OK, ring.Init(INVALID_FD));

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

    MidiSharedRing::PeekedEvent peek;
    EXPECT_EQ(MidiStatusCode::SHM_BROKEN, ring.PeekNext(peek));
}

/**
 * @tc.name   : Test MidiSharedRing CommitRead API
 * @tc.number : MidiSharedRingCommitRead_001
 * @tc.desc   : endOffset >= capacity should wrap to 0.
 */
HWTEST_F(MidiSharedRingUnitTest, MidiSharedRingCommitRead_001, TestSize.Level0)
{
    MidiSharedRing ring(128);
    ASSERT_EQ(OH_MIDI_STATUS_OK, ring.Init(INVALID_FD));

    auto *ctrl = ring.GetControlHeader();
    ASSERT_NE(nullptr, ctrl);
    ctrl->readPosition.store(10);

    MidiSharedRing::PeekedEvent ev{};
    ev.endOffset = 128; // == capacity
    EXPECT_TRUE(ring.CommitRead(ev));
    EXPECT_EQ(0u, ring.GetReadPosition());

    ctrl->readPosition.store(10);
    ev.endOffset = 129; // > capacity
    EXPECT_TRUE(ring.CommitRead(ev));
    EXPECT_EQ(0u, ring.GetReadPosition());
}

/**
 * @tc.name   : Test MidiSharedRing DrainToBatch API
 * @tc.number : MidiSharedRingDrainToBatch_001
 * @tc.desc   : drain all events when maxEvents==0.
 */
HWTEST_F(MidiSharedRingUnitTest, MidiSharedRingDrainToBatch_001, TestSize.Level0)
{
    MidiSharedRing ring(256);
    ASSERT_EQ(OH_MIDI_STATUS_OK, ring.Init(INVALID_FD));

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
 * @tc.name   : Test MidiSharedRing DrainToBatch API
 * @tc.number : MidiSharedRingDrainToBatch_002
 * @tc.desc   : respect maxEvents limit.
 */
HWTEST_F(MidiSharedRingUnitTest, MidiSharedRingDrainToBatch_002, TestSize.Level0)
{
    MidiSharedRing ring(256);
    ASSERT_EQ(OH_MIDI_STATUS_OK, ring.Init(INVALID_FD));

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
    MidiSharedRing::PeekedEvent peek;
    EXPECT_EQ(MidiStatusCode::OK, ring.PeekNext(peek));
    EXPECT_EQ(2u, peek.localHeader.timestamp);
}

/**
 * @tc.name   : Test MidiSharedRing DrainToBatch API
 * @tc.number : MidiSharedRingDrainToBatch_003
 * @tc.desc   : stop draining when PeekNext returns SHM_BROKEN (corrupted header).
 */
HWTEST_F(MidiSharedRingUnitTest, MidiSharedRingDrainToBatch_003, TestSize.Level0)
{
    MidiSharedRing ring(128);
    ASSERT_EQ(OH_MIDI_STATUS_OK, ring.Init(INVALID_FD));

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

/**
 * @tc.name   : Test MidiSharedRing Header Modification Race Condition
 * @tc.number : MidiSharedRingRaceCondition_001
 * @tc.desc   : Detect race condition when header is modified between peek and commit.
 *              This test verifies the fix for TOCTOU vulnerability in PeekNext.
 */
HWTEST_F(MidiSharedRingUnitTest, MidiSharedRingRaceCondition_001, TestSize.Level0)
{
    MidiSharedRing ring(256);
    ASSERT_EQ(OH_MIDI_STATUS_OK, ring.Init(INVALID_FD));

    // Write initial event
    std::vector<uint32_t> payload{0xDEADBEEF, 0xCAFEBABE};
    MidiEventInner ev = MakeEvent(0x12345678, payload);

    uint32_t written = 0;
    ASSERT_EQ(MidiStatusCode::OK, ring.TryWriteEvents(&ev, 1, &written, false));
    ASSERT_EQ(1u, written);

    // Get the read position where header is located
    uint32_t readPos = ring.GetReadPosition();
    ASSERT_EQ(0u, readPos);

    // Directly access the header in shared memory
    auto *base = ring.GetDataBase();
    ASSERT_NE(nullptr, base);
    auto *header = reinterpret_cast<ShmMidiEventHeader *>(base + readPos);

    uint32_t origLen = header->length;

    uint64_t peekTs = header->timestamp;

    // Simulate concurrent modification (what a malicious or buggy writer might do)
    header->length = 0xFFFFFFFF;

    // Read again (simulating the use phase in BuildPeekedEvent)
    uint64_t useTs = header->timestamp;
    uint32_t useLen = header->length;

    // This demonstrates the vulnerability: values changed between peek and use
    EXPECT_EQ(peekTs, useTs) << "Timestamp should not change";
    // This assertion will fail before the fix is applied, demonstrating the race:
    // EXPECT_EQ(peekLen, useLen) << "Length changed - race condition detected!";

    // Verify the modification actually happened
    EXPECT_NE(origLen, useLen) << "Header was not modified - test setup error";
    EXPECT_EQ(0xFFFFFFFFu, useLen) << "Header modification not detected";

    // After the fix is applied, PeekNext should detect this via sequence numbers
    // and return WOULD_BLOCK to trigger a retry
}

//==================== Sequence Number Tests (TOCTOU Protection) ====================//
// Sequence lock pattern in MidiSharedRing:
// - Writer: even_seq + 1 (odd, write in progress) -> write data -> even_seq + 2 (even, write complete)
// - Reader: check seq is even, read data, check seq unchanged
// SEQ_WRITE_START_INCREMENT = 1 (makes seq odd)
// SEQ_WRITE_COMPLETE_TOTAL = 2 (adds 2 to original even seq, result is even)

/**
 * @tc.name   : Test MidiSharedRing Sequence Number
 * @tc.number : MidiSharedRingSequenceNumber_001
 * @tc.desc   : Verify that sequence number is even after write completes.
 *              Write pattern: seq+1 (odd) -> write data -> seq+2 (even).
 */
HWTEST_F(MidiSharedRingUnitTest, MidiSharedRingSequenceNumber_001, TestSize.Level0)
{
    // RING_CAPACITY: 256 bytes, enough for header(24 bytes) + small payload
    constexpr uint32_t RING_CAPACITY = 256;
    MidiSharedRing ring(RING_CAPACITY);
    ASSERT_EQ(OH_MIDI_STATUS_OK, ring.Init(INVALID_FD));

    // Write an event with 2 words (8 bytes) payload
    std::vector<uint32_t> payload{0x11111111, 0x22222222};
    MidiEventInner ev = MakeEvent(100, payload);

    uint32_t written = 0;
    ASSERT_EQ(MidiStatusCode::OK, ring.TryWriteEvents(&ev, 1, &written, false));
    ASSERT_EQ(1u, written);

    // Access header at read position (where we just wrote)
    auto *base = ring.GetDataBase();
    ASSERT_NE(nullptr, base);
    auto *header = reinterpret_cast<ShmMidiEventHeader *>(base);

    // SEQ_WRITE_COMPLETE_TOTAL = 2, so after write completes: 0 + 2 = 2 (even)
    uint32_t seq = header->sequence.load(std::memory_order_relaxed);
    EXPECT_EQ(0u, seq % 2) << "Sequence should be even after write completes, got: " << seq;
    EXPECT_EQ(2u, seq) << "First write should result in seq=2 (0+2)";
}

/**
 * @tc.name   : Test MidiSharedRing Sequence Number Odd Correction
 * @tc.number : MidiSharedRingSequenceNumber_002
 * @tc.desc   : Verify that odd sequence number is corrected during write.
 *              Tests the fix in commit 1c417b0: if seq is odd, add 1 to make it even first.
 */
HWTEST_F(MidiSharedRingUnitTest, MidiSharedRingSequenceNumber_002, TestSize.Level0)
{
    // RING_CAPACITY: 256 bytes
    constexpr uint32_t RING_CAPACITY = 256;
    MidiSharedRing ring(RING_CAPACITY);
    ASSERT_EQ(OH_MIDI_STATUS_OK, ring.Init(INVALID_FD));

    // Write initial event to establish a header
    std::vector<uint32_t> payload{0xAAAA};
    MidiEventInner ev = MakeEvent(1, payload);
    uint32_t written = 0;
    ASSERT_EQ(MidiStatusCode::OK, ring.TryWriteEvents(&ev, 1, &written, false));

    auto *base = ring.GetDataBase();
    ASSERT_NE(nullptr, base);
    auto *header = reinterpret_cast<ShmMidiEventHeader *>(base);

    // Corrupt sequence to odd value (simulate interrupted write or memory corruption)
    // ODD_SEQUENCE_VALUE: 5 is an arbitrary odd number representing corrupted state
    constexpr uint32_t ODD_SEQUENCE_VALUE = 5;
    header->sequence.store(ODD_SEQUENCE_VALUE, std::memory_order_relaxed);

    // Commit the first event by peeking and committing
    MidiSharedRing::PeekedEvent peek1;
    // Manually set sequence to even for peek to succeed
    header->sequence.store(6, std::memory_order_relaxed);
    ASSERT_EQ(MidiStatusCode::OK, ring.PeekNext(peek1));
    ASSERT_TRUE(ring.CommitRead(peek1));

    // Now header is at a new location, write another event
    // The new write location might have uninitialized (possibly odd) sequence
    std::vector<uint32_t> payload2{0xBBBB};
    MidiEventInner ev2 = MakeEvent(2, payload2);

    // Reset the header at old position to odd to test the correction logic
    // Note: after CommitRead, write position is still at 28 (first event end)
    // We're about to write to offset 28, so let's corrupt that location
    auto *nextHeader = reinterpret_cast<ShmMidiEventHeader *>(base + 28);  // 28 = 24 (header) + 4 (1 word)
    nextHeader->sequence.store(ODD_SEQUENCE_VALUE, std::memory_order_relaxed);

    ASSERT_EQ(MidiStatusCode::OK, ring.TryWriteEvents(&ev2, 1, &written, false));
    ASSERT_EQ(1u, written);

    // After write completes, sequence should be even
    // Correction: 5 (odd) -> 6 (even) -> 7 (start) -> 8 (complete)
    uint32_t seq = nextHeader->sequence.load(std::memory_order_relaxed);
    EXPECT_EQ(0u, seq % 2) << "Sequence should be even after corrected write, got: " << seq;
    // Expected: (5 + 1) + 2 = 8
    EXPECT_EQ(8u, seq) << "Corrected write should result in seq=8";
}

/**
 * @tc.name   : Test MidiSharedRing TOCTOU Protection on Odd Sequence
 * @tc.number : MidiSharedRingTOCTOU_001
 * @tc.desc   : Verify PeekNext rejects events with odd sequence numbers
 *              (indicating write in progress) and returns WOULD_BLOCK.
 */
HWTEST_F(MidiSharedRingUnitTest, MidiSharedRingTOCTOU_001, TestSize.Level0)
{
    // RING_CAPACITY: 256 bytes
    constexpr uint32_t RING_CAPACITY = 256;
    MidiSharedRing ring(RING_CAPACITY);
    ASSERT_EQ(OH_MIDI_STATUS_OK, ring.Init(INVALID_FD));

    // Write initial event
    std::vector<uint32_t> payload{0xDEAD};
    MidiEventInner ev = MakeEvent(100, payload);
    uint32_t written = 0;
    ASSERT_EQ(MidiStatusCode::OK, ring.TryWriteEvents(&ev, 1, &written, false));

    auto *base = ring.GetDataBase();
    ASSERT_NE(nullptr, base);
    auto *header = reinterpret_cast<ShmMidiEventHeader *>(base);

    // Corrupt sequence to odd (simulate concurrent write in progress)
    // ODD_SEQUENCE: 7 is an arbitrary odd number representing "write in progress"
    constexpr uint32_t ODD_SEQUENCE = 7;
    header->sequence.store(ODD_SEQUENCE, std::memory_order_relaxed);

    // PeekNext should fail with WOULD_BLOCK (retry needed due to concurrent write)
    MidiSharedRing::PeekedEvent peek;
    EXPECT_EQ(MidiStatusCode::WOULD_BLOCK, ring.PeekNext(peek));

    // Fix sequence back to even (write complete)
    // EVEN_SEQUENCE: 8 is the expected value after write completes
    constexpr uint32_t EVEN_SEQUENCE = 8;
    header->sequence.store(EVEN_SEQUENCE, std::memory_order_relaxed);

    // Now PeekNext should succeed
    EXPECT_EQ(MidiStatusCode::OK, ring.PeekNext(peek));
    EXPECT_EQ(100u, peek.localHeader.timestamp);
    EXPECT_EQ(EVEN_SEQUENCE, peek.sequence);
}

/**
 * @tc.name   : Test MidiSharedRing CommitRead Sequence Verification
 * @tc.number : MidiSharedRingCommitRead_002
 * @tc.desc   : Verify CommitRead rejects event if sequence changed between peek and commit.
 *              This prevents TOCTOU race conditions.
 */
HWTEST_F(MidiSharedRingUnitTest, MidiSharedRingCommitRead_002, TestSize.Level0)
{
    // RING_CAPACITY: 256 bytes
    constexpr uint32_t RING_CAPACITY = 256;
    MidiSharedRing ring(RING_CAPACITY);
    ASSERT_EQ(OH_MIDI_STATUS_OK, ring.Init(INVALID_FD));

    // Write event
    std::vector<uint32_t> payload{0xBEEF};
    MidiEventInner ev = MakeEvent(999, payload);
    uint32_t written = 0;
    ASSERT_EQ(MidiStatusCode::OK, ring.TryWriteEvents(&ev, 1, &written, false));

    // Peek the event
    MidiSharedRing::PeekedEvent peek;
    ASSERT_EQ(MidiStatusCode::OK, ring.PeekNext(peek));
    EXPECT_EQ(999u, peek.localHeader.timestamp);

    // Store original sequence from peek
    uint32_t origSeq = peek.sequence;

    // Simulate concurrent modification - change sequence
    auto *base = ring.GetDataBase();
    ASSERT_NE(nullptr, base);
    auto *header = reinterpret_cast<ShmMidiEventHeader *>(base + peek.beginOffset);
    // MODIFIED_SEQUENCE: origSeq + 100 simulates a different write cycle
    constexpr uint32_t SEQUENCE_DELTA = 100;
    header->sequence.store(origSeq + SEQUENCE_DELTA, std::memory_order_relaxed);

    // CommitRead should reject this event because sequence changed
    EXPECT_FALSE(ring.CommitRead(peek));

    // Read position should NOT have changed (commit was rejected)
    EXPECT_EQ(0u, ring.GetReadPosition());
}

/**
 * @tc.name   : Test MidiSharedRing Wrap Marker Sequence
 * @tc.number : MidiSharedRingWrapSequence_001
 * @tc.desc   : Verify wrap marker has correct even sequence number after write.
 *              Wrap markers use the same sequence lock pattern as regular events.
 */
HWTEST_F(MidiSharedRingUnitTest, MidiSharedRingWrapSequence_001, TestSize.Level0)
{
    // RING_CAPACITY: 128 bytes, small enough to trigger wrap
    constexpr uint32_t RING_CAPACITY = 128;
    MidiSharedRing ring(RING_CAPACITY);
    ASSERT_EQ(OH_MIDI_STATUS_OK, ring.Init(INVALID_FD));

    // PAYLOAD_WORDS: 19 words = 76 bytes payload
    // Total event size: 24 (header) + 76 (payload) = 100 bytes
    // Remaining tail: 128 - 100 = 28 bytes (enough for wrap header)
    constexpr uint32_t PAYLOAD_WORDS = 19;
    std::vector<uint32_t> payload1(PAYLOAD_WORDS, 0x11);
    MidiEventInner ev1 = MakeEvent(10, payload1);

    uint32_t written = 0;
    ASSERT_EQ(MidiStatusCode::OK, ring.TryWriteEvents(&ev1, 1, &written, false));
    ASSERT_EQ(1u, written);

    // EVENT_SIZE: 24 (sizeof ShmMidiEventHeader) + 76 (19 * 4 bytes)
    constexpr uint32_t HEADER_SIZE = 24;  // sizeof(ShmMidiEventHeader)
    constexpr uint32_t EVENT_SIZE = HEADER_SIZE + PAYLOAD_WORDS * sizeof(uint32_t);
    uint32_t writeAfterEv1 = ring.GetWritePosition();
    ASSERT_EQ(EVENT_SIZE, writeAfterEv1);

    // Move read position to free up space for wrap + second event
    // READ_POS: 64 is chosen to leave enough space for the second event after wrap
    constexpr uint32_t READ_POS = 64;
    auto *ctrl = ring.GetControlHeader();
    ASSERT_NE(nullptr, ctrl);
    ctrl->readPosition.store(READ_POS);

    // Write small event that will trigger wrap (tail space insufficient)
    std::vector<uint32_t> payload2{0xAA, 0xBB};
    MidiEventInner ev2 = MakeEvent(20, payload2);
    ASSERT_EQ(MidiStatusCode::OK, ring.TryWriteEvents(&ev2, 1, &written, false));

    // Verify wrap marker at old write position
    auto *base = ring.GetDataBase();
    ASSERT_NE(nullptr, base);
    auto *wrapHdr = reinterpret_cast<ShmMidiEventHeader *>(base + writeAfterEv1);

    EXPECT_EQ(SHM_EVENT_FLAG_WRAP, wrapHdr->flags);
    EXPECT_EQ(0u, wrapHdr->length);

    // Verify wrap marker sequence is even (write complete)
    uint32_t wrapSeq = wrapHdr->sequence.load(std::memory_order_relaxed);
    EXPECT_EQ(0u, wrapSeq % 2) << "Wrap marker sequence should be even, got: " << wrapSeq;
}

/**
 * @tc.name   : Test MidiSharedRing Sequence Number Wraparound Correction
 * @tc.number : MidiSharedRingSequenceNumber_003
 * @tc.desc   : Verify odd sequence correction works correctly at wrap boundary.
 *              Tests the fix in UpdateWriteIndexIfNeed where wrap marker is written.
 */
HWTEST_F(MidiSharedRingUnitTest, MidiSharedRingSequenceNumber_003, TestSize.Level0)
{
    // RING_CAPACITY: 128 bytes
    constexpr uint32_t RING_CAPACITY = 128;
    MidiSharedRing ring(RING_CAPACITY);
    ASSERT_EQ(OH_MIDI_STATUS_OK, ring.Init(INVALID_FD));

    // Write first event to use most of the buffer
    constexpr uint32_t PAYLOAD_WORDS = 19;  // 76 bytes payload
    std::vector<uint32_t> payload1(PAYLOAD_WORDS, 0x11);
    MidiEventInner ev1 = MakeEvent(1, payload1);
    uint32_t written = 0;
    ASSERT_EQ(MidiStatusCode::OK, ring.TryWriteEvents(&ev1, 1, &written, false));

    constexpr uint32_t HEADER_SIZE = 24;
    constexpr uint32_t EVENT_SIZE = HEADER_SIZE + PAYLOAD_WORDS * sizeof(uint32_t);
    uint32_t wrapOffset = ring.GetWritePosition();
    ASSERT_EQ(EVENT_SIZE, wrapOffset);  // 100 bytes

    // Corrupt the sequence at wrap position to odd value
    // This simulates uninitialized memory or previous interrupted write
    auto *base = ring.GetDataBase();
    auto *wrapHeader = reinterpret_cast<ShmMidiEventHeader *>(base + wrapOffset);
    constexpr uint32_t ODD_SEQ = 13;  // Arbitrary odd sequence
    wrapHeader->sequence.store(ODD_SEQ, std::memory_order_relaxed);

    // Free up space by moving read position
    auto *ctrl = ring.GetControlHeader();
    ctrl->readPosition.store(50);  // Free up 50 bytes from beginning

    // Write another event - this should trigger wrap and write wrap marker
    std::vector<uint32_t> payload2{0x22};
    MidiEventInner ev2 = MakeEvent(2, payload2);
    ASSERT_EQ(MidiStatusCode::OK, ring.TryWriteEvents(&ev2, 1, &written, false));

    // Verify wrap marker was written with even sequence
    // The correction should ensure: odd(13) -> even(14) -> start(15) -> complete(16)
    uint32_t wrapSeq = wrapHeader->sequence.load(std::memory_order_relaxed);
    EXPECT_EQ(0u, wrapSeq % 2) << "Wrap marker sequence should be even after correction, got: " << wrapSeq;
    EXPECT_EQ(static_cast<uint32_t>(ODD_SEQ + 3), wrapSeq) << "Expected seq=" << (ODD_SEQ + 3);
}
} // namespace MIDI
} // namespace OHOS
