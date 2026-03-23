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

#include <gtest/gtest.h>
#include <vector>
#include <cstdint>
#include "ble_midi_encoder.h"

using namespace testing;
using namespace testing::ext;

/**
 * @brief BLE MIDI Encoder Unit Test
 *
 * Tests for BLE MIDI packet encoding according to Bluetooth MIDI Specification.
 */
class BleMidiEncoderUnitTest : public testing::Test {
public:
    static void SetUpTestCase() {}
    static void TearDownTestCase() {}
    void SetUp() override {}
    void TearDown() override {}
};

// ====================================================================
// Basic Encoder Tests
// ====================================================================

/**
 * @tc.name: EncodeSingleNoteOn
 * @tc.desc: Test encoding a single Note On event with timestamp
 * @tc.type: FUNC
 */
HWTEST_F(BleMidiEncoderUnitTest, EncodeSingleNoteOn, TestSize.Level1)
{
    uint8_t midiData[] = { 0x90, 0x3C, 0x64 };
    uint16_t timestamp = 0x0000;

    std::vector<uint8_t> result = BleMidiPacketEncoder::EncodeEvent(
        midiData, sizeof(midiData), timestamp);

    EXPECT_EQ(result.size(), 5u);
    EXPECT_EQ(result[0], 0x80);  // Header byte (ts=0)
    EXPECT_EQ(result[1], 0x80);  // Timestamp byte (ts=0)
    EXPECT_EQ(result[2], 0x90);  // Note On status
    EXPECT_EQ(result[3], 0x3C);  // Note number
    EXPECT_EQ(result[4], 0x64);  // Velocity
}

/**
 * @tc.name: EncodeWithMaxTimestamp
 * @tc.desc: Test encoding with maximum 13-bit timestamp (8191)
 * @tc.type: FUNC
 */
HWTEST_F(BleMidiEncoderUnitTest, EncodeWithMaxTimestamp, TestSize.Level1)
{
    uint8_t midiData[] = { 0x90, 0x3C, 0x64 };
    uint16_t timestamp = 8191;  // Max 13-bit value

    std::vector<uint8_t> result = BleMidiPacketEncoder::EncodeEvent(
        midiData, sizeof(midiData), timestamp);

    EXPECT_EQ(result.size(), 5u);
    EXPECT_EQ(result[0], 0xBF);  // Header: 0x80 | 0x3F
    EXPECT_EQ(result[1], 0xFF);  // Timestamp: 0x80 | 0x7F
}

/**
 * @tc.name: EncodeWithTimestamp1234
 * @tc.desc: Test encoding with timestamp 0x1234
 * @tc.type: FUNC
 */
HWTEST_F(BleMidiEncoderUnitTest, EncodeWithTimestamp1234, TestSize.Level1)
{
    uint8_t midiData[] = { 0x90, 0x3C, 0x64 };
    uint16_t timestamp = 0x1234;

    std::vector<uint8_t> result = BleMidiPacketEncoder::EncodeEvent(
        midiData, sizeof(midiData), timestamp);

    EXPECT_EQ(result.size(), 5u);
    EXPECT_EQ(result[0], 0xA4);  // Header: 0x80 | 0x24
    EXPECT_EQ(result[1], 0xB4);  // Timestamp: 0x80 | 0x34
}

/**
 * @tc.name: EncodeNoteOff
 * @tc.desc: Test encoding Note Off event
 * @tc.type: FUNC
 */
HWTEST_F(BleMidiEncoderUnitTest, EncodeNoteOff, TestSize.Level1)
{
    uint8_t midiData[] = { 0x80, 0x3C, 0x40 };
    uint16_t timestamp = 0x0100;

    std::vector<uint8_t> result = BleMidiPacketEncoder::EncodeEvent(
        midiData, sizeof(midiData), timestamp);

    EXPECT_EQ(result.size(), 5u);
    EXPECT_EQ(result[0], 0x82);  // Header
    EXPECT_EQ(result[1], 0x80);  // Timestamp
    EXPECT_EQ(result[2], 0x80);  // Note Off status
}

/**
 * @tc.name: EncodeControlChange
 * @tc.desc: Test encoding Control Change event
 * @tc.type: FUNC
 */
HWTEST_F(BleMidiEncoderUnitTest, EncodeControlChange, TestSize.Level1)
{
    uint8_t midiData[] = { 0xB0, 0x07, 0x7F };
    uint16_t timestamp = 0x0000;

    std::vector<uint8_t> result = BleMidiPacketEncoder::EncodeEvent(
        midiData, sizeof(midiData), timestamp);

    EXPECT_EQ(result.size(), 5u);
    EXPECT_EQ(result[2], 0xB0);
    EXPECT_EQ(result[3], 0x07);
    EXPECT_EQ(result[4], 0x7F);
}

/**
 * @tc.name: EncodeProgramChange
 * @tc.desc: Test encoding Program Change (2-byte message)
 * @tc.type: FUNC
 */
HWTEST_F(BleMidiEncoderUnitTest, EncodeProgramChange, TestSize.Level1)
{
    uint8_t midiData[] = { 0xC0, 0x0A };
    uint16_t timestamp = 0x0000;

    std::vector<uint8_t> result = BleMidiPacketEncoder::EncodeEvent(
        midiData, sizeof(midiData), timestamp);

    EXPECT_EQ(result.size(), 4u);  // 2 header + 2 MIDI bytes
    EXPECT_EQ(result[2], 0xC0);
    EXPECT_EQ(result[3], 0x0A);
}

/**
 * @tc.name: EncodeSysExShort
 * @tc.desc: Test encoding short SysEx message
 * @tc.type: FUNC
 */
HWTEST_F(BleMidiEncoderUnitTest, EncodeSysExShort, TestSize.Level1)
{
    uint8_t midiData[] = { 0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7 };
    uint16_t timestamp = 0x0000;

    std::vector<uint8_t> result = BleMidiPacketEncoder::EncodeEvent(
        midiData, sizeof(midiData), timestamp);

    // SysEx has 2 status bytes (F0, F7), so extra timestamp before F7
    // Expected: [Header][TS][F0][7E][7F][06][01][TS][F7]
    EXPECT_EQ(result.size(), 9u);
    EXPECT_EQ(result[2], 0xF0);
    EXPECT_EQ(result[8], 0xF7);
}

/**
 * @tc.name: EncodeEmptyData
 * @tc.desc: Test encoding empty MIDI data
 * @tc.type: FUNC
 */
HWTEST_F(BleMidiEncoderUnitTest, EncodeEmptyData, TestSize.Level1)
{
    std::vector<uint8_t> result = BleMidiPacketEncoder::EncodeEvent(nullptr, 0, 0x0000);
    EXPECT_TRUE(result.empty());
}

/**
 * @tc.name: EncodeNullData
 * @tc.desc: Test encoding with null pointer
 * @tc.type: FUNC
 */
HWTEST_F(BleMidiEncoderUnitTest, EncodeNullData, TestSize.Level1)
{
    std::vector<uint8_t> result = BleMidiPacketEncoder::EncodeEvent(nullptr, 10, 0x0000);
    EXPECT_TRUE(result.empty());
}

// ====================================================================
// Multi-Status Byte Tests
// ====================================================================

/**
 * @tc.name: EncodeMultipleStatusBytes
 * @tc.desc: Test encoding MIDI data with multiple status bytes
 * @tc.type: FUNC
 */
HWTEST_F(BleMidiEncoderUnitTest, EncodeMultipleStatusBytes, TestSize.Level1)
{
    // Two Note On events in sequence
    uint8_t midiData[] = { 0x90, 0x3C, 0x64, 0x90, 0x3E, 0x41 };
    uint16_t timestamp = 0x0000;

    std::vector<uint8_t> result = BleMidiPacketEncoder::EncodeEvent(
        midiData, sizeof(midiData), timestamp);

    // Expected: [Header][TS][0x90][0x3C][0x64][TS][0x90][0x3E][0x41]
    EXPECT_EQ(result.size(), 9u);
    EXPECT_EQ(result[0], 0x80);  // Header
    EXPECT_EQ(result[1], 0x80);  // TS1
    EXPECT_EQ(result[2], 0x90);  // Status1
    EXPECT_EQ(result[5], 0x80);  // TS2 (before second status)
    EXPECT_EQ(result[6], 0x90);  // Status2
}

/**
 * @tc.name: EncodeNoteOnAndControlChange
 * @tc.desc: Test encoding Note On followed by Control Change
 * @tc.type: FUNC
 */
HWTEST_F(BleMidiEncoderUnitTest, EncodeNoteOnAndControlChange, TestSize.Level1)
{
    uint8_t midiData[] = { 0x90, 0x3C, 0x64, 0xB0, 0x07, 0x7F };
    uint16_t timestamp = 0x0100;

    std::vector<uint8_t> result = BleMidiPacketEncoder::EncodeEvent(
        midiData, sizeof(midiData), timestamp);

    EXPECT_EQ(result.size(), 9u);
    EXPECT_EQ(result[2], 0x90);  // Note On
    EXPECT_EQ(result[5], 0x80);  // TS before CC
    EXPECT_EQ(result[6], 0xB0);  // Control Change
}

// ====================================================================
// Timestamp Utilities Tests
// ====================================================================

/**
 * @tc.name: GetCurrentTimestampMs
 * @tc.desc: Test timestamp generation
 * @tc.type: FUNC
 */
HWTEST_F(BleMidiEncoderUnitTest, GetCurrentTimestampMs, TestSize.Level1)
{
    uint16_t ts1 = BleMidiPacketEncoder::GetCurrentTimestampMs();
    uint16_t ts2 = BleMidiPacketEncoder::GetCurrentTimestampMs();

    EXPECT_LE(ts1, 8191u);
    EXPECT_LE(ts2, 8191u);
}

/**
 * @tc.name: TimestampMaskingOverflow
 * @tc.desc: Test that timestamps above 8191 are correctly masked
 * @tc.type: FUNC
 */
HWTEST_F(BleMidiEncoderUnitTest, TimestampMaskingOverflow, TestSize.Level1)
{
    uint8_t midiData[] = { 0x90, 0x3C, 0x64 };

    struct TestCase {
        uint16_t input;
        uint16_t expected;
    };

    TestCase cases[] = {
        {8192, 0},
        {8193, 1},
        {10000, 1808},
        {16384, 0},
        {20000, 3616},
        {65535, 8191},
    };

    for (const auto& tc : cases) {
        std::vector<uint8_t> result = BleMidiPacketEncoder::EncodeEvent(
            midiData, sizeof(midiData), tc.input);

        EXPECT_FALSE(result.empty());
        uint8_t expectedHigh = 0x80 | ((tc.expected >> 7) & 0x3F);
        uint8_t expectedLow = 0x80 | (tc.expected & 0x7F);
        EXPECT_EQ(result[0], expectedHigh);
        EXPECT_EQ(result[1], expectedLow);
    }
}

/**
 * @tc.name: EncodeWithEventTimestamp
 * @tc.desc: Test encoding with event timestamp (ns -> ms conversion)
 * @tc.type: FUNC
 */
HWTEST_F(BleMidiEncoderUnitTest, EncodeWithEventTimestamp, TestSize.Level1)
{
    uint8_t midiData[] = { 0x90, 0x3C, 0x64 };

    // 1234567890 ns = 1234 ms
    int64_t timestampNs = 1234567890;
    uint16_t timestampMs = static_cast<uint16_t>((timestampNs / 1000000) & 0x1FFF);

    std::vector<uint8_t> result = BleMidiPacketEncoder::EncodeEvent(
        midiData, sizeof(midiData), timestampMs);

    // 1234 = 0x4D2, high: 0x09, low: 0x52
    EXPECT_EQ(result.size(), 5u);
    EXPECT_EQ(result[0], 0x80 | 0x09);  // 0x89
    EXPECT_EQ(result[1], 0x80 | 0x52);  // 0xD2
}

/**
 * @tc.name: EncodeWithZeroTimestamp
 * @tc.desc: Test encoding when event timestamp is 0
 * @tc.type: FUNC
 */
HWTEST_F(BleMidiEncoderUnitTest, EncodeWithZeroTimestamp, TestSize.Level1)
{
    uint8_t midiData[] = { 0x90, 0x3C, 0x64 };

    uint16_t timestampMs = BleMidiPacketEncoder::GetCurrentTimestampMs();

    std::vector<uint8_t> result = BleMidiPacketEncoder::EncodeEvent(
        midiData, sizeof(midiData), timestampMs);

    EXPECT_EQ(result.size(), 5u);
    EXPECT_EQ(result[0] & 0x80, 0x80);  // Header bit 7 set
    EXPECT_EQ(result[1] & 0x80, 0x80);  // Timestamp bit 7 set
    EXPECT_EQ(result[0] & 0x40, 0x00);  // Header bit 6 must be 0
}

/**
 * @tc.name: NanosecondToMillisecondConversion
 * @tc.desc: Test ns to ms conversion
 * @tc.type: FUNC
 */
HWTEST_F(BleMidiEncoderUnitTest, NanosecondToMillisecondConversion, TestSize.Level1)
{
    uint8_t midiData[] = { 0x90, 0x3C, 0x64 };

    // 1 second = 1000 ms
    int64_t oneSecondNs = 1000000000;
    uint16_t ts1 = static_cast<uint16_t>((oneSecondNs / 1000000) & 0x1FFF);
    EXPECT_EQ(ts1, 1000u);

    // 10 seconds -> masked to 1808
    int64_t largeNs = 10000000000;
    uint16_t ts2 = static_cast<uint16_t>((largeNs / 1000000) & 0x1FFF);
    EXPECT_EQ(ts2, 1808u);

    // 8191 ms
    int64_t maxNs = 8191000000;
    uint16_t ts3 = static_cast<uint16_t>((maxNs / 1000000) & 0x1FFF);
    EXPECT_EQ(ts3, 8191u);
}
