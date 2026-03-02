/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
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
#include "ump_converter.h"

using namespace testing;
using namespace testing::ext;

namespace {

constexpr uint32_t SHIFT_MT = 28;
constexpr uint32_t SHIFT_GROUP = 24;

constexpr uint8_t MT_SYSTEM = 0x1;
constexpr uint8_t MT_MIDI1_CH = 0x2;
constexpr uint8_t MT_MIDI2_CH = 0x4;

constexpr uint8_t MASK_NIBBLE = 0x0F;

constexpr uint32_t SHIFT_STATUS_NIBBLE_M2 = 20;
constexpr uint32_t SHIFT_CHANNEL_M2 = 16;
constexpr uint32_t SHIFT_DATA1_M2 = 8;

constexpr uint8_t MASK_7BIT = 0x7F;

constexpr uint32_t SHIFT_U7_TO_U32 = 25;
constexpr uint32_t SHIFT_U14_TO_U32 = 18;

constexpr uint32_t ROUND_U32_TO_U7 = 1u << (SHIFT_U7_TO_U32 - 1);
constexpr uint32_t ROUND_U32_TO_U14 = 1u << (SHIFT_U14_TO_U32 - 1);

// Helpers to build expected words quickly.
inline uint32_t MakeMidi1ChVoiceWord(uint8_t group, uint8_t status, uint8_t data1, uint8_t data2)
{
    return (static_cast<uint32_t>(MT_MIDI1_CH) << SHIFT_MT) |
           (static_cast<uint32_t>(group & MASK_NIBBLE) << SHIFT_GROUP) |
           (static_cast<uint32_t>(status) << SHIFT_CHANNEL_M2) |
           (static_cast<uint32_t>(data1) << SHIFT_DATA1_M2) |
           static_cast<uint32_t>(data2);
}

inline uint32_t MakeSystemWord(uint8_t group, uint8_t status, uint8_t data1 = 0, uint8_t data2 = 0)
{
    return (static_cast<uint32_t>(MT_SYSTEM) << SHIFT_MT) |
           (static_cast<uint32_t>(group & MASK_NIBBLE) << SHIFT_GROUP) |
           (static_cast<uint32_t>(status) << SHIFT_CHANNEL_M2) |
           (static_cast<uint32_t>(data1) << SHIFT_DATA1_M2) |
           static_cast<uint32_t>(data2);
}

inline uint32_t MakeMidi2ChVoiceWord0(uint8_t group, uint8_t statusNibble,
    uint8_t channel, uint8_t data1, uint8_t data2)
{
    return (static_cast<uint32_t>(MT_MIDI2_CH) << SHIFT_MT) |
           (static_cast<uint32_t>(group & MASK_NIBBLE) << SHIFT_GROUP) |
           (static_cast<uint32_t>(statusNibble & MASK_NIBBLE) << SHIFT_STATUS_NIBBLE_M2) |
           (static_cast<uint32_t>(channel & MASK_NIBBLE) << SHIFT_CHANNEL_M2) |
           (static_cast<uint32_t>(data1) << SHIFT_DATA1_M2) |
           static_cast<uint32_t>(data2);
}

inline uint32_t U7ToU32(uint8_t v7)
{
    return static_cast<uint32_t>(v7) << SHIFT_U7_TO_U32;
}

inline uint32_t U14ToU32(uint16_t v14)
{
    return static_cast<uint32_t>(v14) << SHIFT_U14_TO_U32;
}

inline uint8_t U32ToU7(uint32_t v32)
{
    return static_cast<uint8_t>((v32 + ROUND_U32_TO_U7) >> SHIFT_U7_TO_U32);
}

inline uint16_t U32ToU14(uint32_t v32)
{
    return static_cast<uint16_t>((v32 + ROUND_U32_TO_U14) >> SHIFT_U14_TO_U32);
}

} // namespace

class UmpConverterUnitTest : public testing::Test {
public:
    static void SetUpTestCase() {}
    static void TearDownTestCase() {}
    void SetUp() override {}
    void TearDown() override {}
};

// ====================================================================
// 1. MIDI 1.0 -> MIDI 2.0 (Channel Voice, MT=0x2 -> MT=0x4)
// ====================================================================

/**
 * @tc.name: TestMidi1ToMidi2_NoteOn
 * @tc.desc: Convert MIDI1 UMP Note On (MT=0x2) to MIDI2 Channel Voice (MT=0x4), velocity 7-bit -> 32-bit.
 * @tc.type: FUNC
 */
HWTEST_F(UmpConverterUnitTest, TestMidi1ToMidi2_NoteOn, TestSize.Level1)
{
    // MIDI1 UMP: MT=2, group=0, status=0x90, note=0x3C, vel=0x64
    const uint32_t in = MakeMidi1ChVoiceWord(0, 0x90, 0x3C, 0x64);

    std::vector<uint32_t> out;
    const bool ok = UmpConverter::ConvertMidi1ToMidi2(&in, 1, out);

    ASSERT_TRUE(ok);
    ASSERT_EQ(out.size(), 2u);

    // MIDI2 Word0: MT=4, group=0, statusNibble=9, channel=0, data1=note, data2=attrType(0)
    EXPECT_EQ(out[0], MakeMidi2ChVoiceWord0(0, 0x9, 0, 0x3C, 0x00));
    // MIDI2 Word1: velocity32
    EXPECT_EQ(out[1], U7ToU32(0x64));
}

/**
 * @tc.name: TestMidi1ToMidi2_PitchBend
 * @tc.desc: Convert MIDI1 Pitch Bend (14-bit) to MIDI2 32-bit value (lossless upconvert).
 * @tc.type: FUNC
 */
HWTEST_F(UmpConverterUnitTest, TestMidi1ToMidi2_PitchBend, TestSize.Level1)
{
    // MIDI1 Pitch Bend: status=0xE0, LSB=0x00, MSB=0x40 => center 0x2000
    const uint32_t in = MakeMidi1ChVoiceWord(0, 0xE0, 0x00, 0x40);

    std::vector<uint32_t> out;
    const bool ok = UmpConverter::ConvertMidi1ToMidi2(&in, 1, out);

    ASSERT_TRUE(ok);
    ASSERT_EQ(out.size(), 2u);

    const uint16_t pb14 = static_cast<uint16_t>((0x40u << 7) | 0x00u);
    EXPECT_EQ(out[0], MakeMidi2ChVoiceWord0(0, 0xE, 0, 0x00, 0x00));
    EXPECT_EQ(out[1], U14ToU32(pb14));
}

/**
 * @tc.name: TestMidi1ToMidi2_ProgramChange
 * @tc.desc: Convert MIDI1 Program Change to MIDI2 Channel Voice. Program kept in data1 and also upscaled in word1.
 * @tc.type: FUNC
 */
HWTEST_F(UmpConverterUnitTest, TestMidi1ToMidi2_ProgramChange, TestSize.Level1)
{
    // MIDI1 Program Change: status=0xC0, program=0x05, pad=0
    const uint32_t in = MakeMidi1ChVoiceWord(0, 0xC0, 0x05, 0x00);

    std::vector<uint32_t> out;
    const bool ok = UmpConverter::ConvertMidi1ToMidi2(&in, 1, out);

    ASSERT_TRUE(ok);
    ASSERT_EQ(out.size(), 2u);

    EXPECT_EQ(out[0], MakeMidi2ChVoiceWord0(0, 0xC, 0, 0x05, 0x00));
    EXPECT_EQ(out[1], U7ToU32(0x05));
}

// ====================================================================
// 2. System (MT=0x1) Pass-through in both directions
// ====================================================================

/**
 * @tc.name: TestPassThrough_SystemClock_Midi1ToMidi2
 * @tc.desc: MT=0x1 Timing Clock UMP should be passed through unchanged for Midi1->Midi2 conversion.
 * @tc.type: FUNC
 */
HWTEST_F(UmpConverterUnitTest, TestPassThrough_SystemClock_Midi1ToMidi2, TestSize.Level1)
{
    // MT=1, status=0xF8 (Timing Clock)
    const uint32_t in = MakeSystemWord(0, 0xF8);

    std::vector<uint32_t> out;
    const bool ok = UmpConverter::ConvertMidi1ToMidi2(&in, 1, out);

    ASSERT_TRUE(ok);
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0], in);
}

/**
 * @tc.name: TestPassThrough_SystemSongSelect_Midi2ToMidi1
 * @tc.desc: MT=0x1 Song Select UMP should be passed through unchanged for Midi2->Midi1 conversion.
 * @tc.type: FUNC
 */
HWTEST_F(UmpConverterUnitTest, TestPassThrough_SystemSongSelect_Midi2ToMidi1, TestSize.Level1)
{
    // MT=1, status=0xF3, data1=0x12
    const uint32_t in = MakeSystemWord(0, 0xF3, 0x12, 0x00);

    std::vector<uint32_t> out;
    const bool ok = UmpConverter::ConvertMidi2ToMidi1(&in, 1, out);

    ASSERT_TRUE(ok);
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0], in);
}

// ====================================================================
// 3. SysEx7/Data (MT=0x3) Pass-through in both directions
// ====================================================================

/**
 * @tc.name: TestPassThrough_SysEx7_OnePacket_Midi1ToMidi2
 * @tc.desc: MT=0x3 SysEx7 (64-bit, 2 words) should be passed through unchanged for Midi1->Midi2 conversion.
 * @tc.type: FUNC
 */
HWTEST_F(UmpConverterUnitTest, TestPassThrough_SysEx7_OnePacket_Midi1ToMidi2, TestSize.Level1)
{
    // Use example from your processor UT:
    // Word0: 0x30030102, Word1: 0x03000000
    const uint32_t inWords[2] = { 0x30030102U, 0x03000000U };

    std::vector<uint32_t> out;
    const bool ok = UmpConverter::ConvertMidi1ToMidi2(inWords, 2, out);

    ASSERT_TRUE(ok);
    ASSERT_EQ(out.size(), 2u);
    EXPECT_EQ(out[0], inWords[0]);
    EXPECT_EQ(out[1], inWords[1]);
}

/**
 * @tc.name: TestPassThrough_SysEx7_MultiPacket_Midi2ToMidi1
 * @tc.desc: MT=0x3 SysEx7 spanning multiple packets (multiple of 2 words) should be passed through unchanged.
 * @tc.type: FUNC
 */
HWTEST_F(UmpConverterUnitTest, TestPassThrough_SysEx7_MultiPacket_Midi2ToMidi1, TestSize.Level1)
{
    // Two 64-bit packets => 4 words
    const uint32_t inWords[4] = {
        0x30160102U, 0x03040506U, // Start packet
        0x30320708U, 0x00000000U  // End packet
    };

    std::vector<uint32_t> out;
    const bool ok = UmpConverter::ConvertMidi2ToMidi1(inWords, 4, out);

    ASSERT_TRUE(ok);
    ASSERT_EQ(out.size(), 4u);
    for (size_t i = 0; i < 4; ++i) {
        EXPECT_EQ(out[i], inWords[i]);
    }
}

// ====================================================================
// 4. MIDI 2.0 -> MIDI 1.0 (Channel Voice, MT=0x4 -> MT=0x2)
// ====================================================================

/**
 * @tc.name: TestMidi2ToMidi1_NoteOn_Lossy
 * @tc.desc: Convert MIDI2 Note On (velocity32) to MIDI1 Note On (velocity7) with acceptable precision loss.
 * @tc.type: FUNC
 */
HWTEST_F(UmpConverterUnitTest, TestMidi2ToMidi1_NoteOn_Lossy, TestSize.Level1)
{
    // Build a MIDI2 NoteOn: group=0, statusNibble=9, channel=0, note=0x3C, attr=0
    const uint32_t w0 = MakeMidi2ChVoiceWord0(0, 0x9, 0, 0x3C, 0x00);

    // Choose velocity32 corresponding exactly to v7=0x64 via our upconvert mapping.
    const uint32_t w1 = U7ToU32(0x64);

    const uint32_t inWords[2] = { w0, w1 };

    std::vector<uint32_t> out;
    const bool ok = UmpConverter::ConvertMidi2ToMidi1(inWords, 2, out);

    ASSERT_TRUE(ok);
    ASSERT_EQ(out.size(), 1u);

    const uint8_t vel7 = static_cast<uint8_t>(U32ToU7(w1) & MASK_7BIT);
    EXPECT_EQ(out[0], MakeMidi1ChVoiceWord(0, 0x90, 0x3C, vel7));
    EXPECT_EQ(vel7, 0x64);
}

/**
 * @tc.name: TestMidi2ToMidi1_PitchBend_Lossy
 * @tc.desc: Convert MIDI2 Pitch Bend 32-bit value to MIDI1 14-bit (LSB/MSB) with acceptable precision loss.
 * @tc.type: FUNC
 */
HWTEST_F(UmpConverterUnitTest, TestMidi2ToMidi1_PitchBend_Lossy, TestSize.Level1)
{
    const uint32_t w0 = MakeMidi2ChVoiceWord0(0, 0xE, 0, 0, 0);

    // Use a pitch value corresponding exactly to pb14=0x2000 (center) in our upconvert mapping.
    const uint16_t pb14 = 0x2000;
    const uint32_t w1 = U14ToU32(pb14);

    const uint32_t inWords[2] = { w0, w1 };

    std::vector<uint32_t> out;
    const bool ok = UmpConverter::ConvertMidi2ToMidi1(inWords, 2, out);

    ASSERT_TRUE(ok);
    ASSERT_EQ(out.size(), 1u);

    const uint16_t got14 = static_cast<uint16_t>(U32ToU14(w1) & 0x3FFF);
    const uint8_t lsb = static_cast<uint8_t>(got14 & 0x7F);
    const uint8_t msb = static_cast<uint8_t>((got14 >> 7) & 0x7F);

    EXPECT_EQ(out[0], MakeMidi1ChVoiceWord(0, 0xE0, lsb, msb));
    EXPECT_EQ(got14, pb14);
}

// ====================================================================
// 5. Drop policy & malformed inputs
// ====================================================================

/**
 * @tc.name: TestDrop_UnsupportedMsgType
 * @tc.desc: Messages not in supported set (MT!=0x1/0x2/0x3/0x4) should be dropped (no output).
 * @tc.type: FUNC
 */
HWTEST_F(UmpConverterUnitTest, TestDrop_UnsupportedMsgType, TestSize.Level1)
{
    // MT=0x5 (e.g., SysEx8 / higher data) => not supported by this converter
    const uint32_t in = (static_cast<uint32_t>(0x5) << SHIFT_MT);

    std::vector<uint32_t> out;
    const bool ok12 = UmpConverter::ConvertMidi1ToMidi2(&in, 1, out);
    const bool ok21 = UmpConverter::ConvertMidi2ToMidi1(&in, 1, out);

    EXPECT_FALSE(ok12);
    EXPECT_FALSE(ok21);
    EXPECT_TRUE(out.empty());
}

/**
 * @tc.name: TestDrop_MalformedSysEx7WordCount
 * @tc.desc: MT=0x3 SysEx7 must be multiples of 2 words; odd word count should be rejected (dropped).
 * @tc.type: FUNC
 */
HWTEST_F(UmpConverterUnitTest, TestDrop_MalformedSysEx7WordCount, TestSize.Level1)
{
    // Word0 looks like MT=0x3, but only one word provided => malformed
    const uint32_t in = 0x30030102U;

    std::vector<uint32_t> out;
    const bool ok = UmpConverter::ConvertMidi1ToMidi2(&in, 1, out);

    EXPECT_FALSE(ok);
    EXPECT_TRUE(out.empty());
}

/**
 * @tc.name: TestDrop_MalformedMidi2WordCount
 * @tc.desc: MT=0x4 MIDI2 Channel Voice must be exactly 2 words; non-2 count should be rejected (dropped).
 * @tc.type: FUNC
 */
HWTEST_F(UmpConverterUnitTest, TestDrop_MalformedMidi2WordCount, TestSize.Level1)
{
    const uint32_t in = MakeMidi2ChVoiceWord0(0, 0x9, 0, 0x3C, 0);

    std::vector<uint32_t> out;
    const bool ok = UmpConverter::ConvertMidi2ToMidi1(&in, 1, out);

    EXPECT_FALSE(ok);
    EXPECT_TRUE(out.empty());
}

/**
 * @tc.name: TestDrop_Midi1UnsupportedStatus
 * @tc.desc: MIDI1 Channel Voice with unsupported status nibble should be dropped in Midi1->Midi2 conversion.
 * @tc.type: FUNC
 */
HWTEST_F(UmpConverterUnitTest, TestDrop_Midi1UnsupportedStatus, TestSize.Level1)
{
    // status 0xF0 is not channel voice (invalid here), but placed into MT=0x2 word to simulate unsupported nibble.
    const uint32_t in = MakeMidi1ChVoiceWord(0, 0xF0, 0x00, 0x00);

    std::vector<uint32_t> out;
    const bool ok = UmpConverter::ConvertMidi1ToMidi2(&in, 1, out);

    EXPECT_FALSE(ok);
    EXPECT_TRUE(out.empty());
}

/**
 * @tc.name: TestDrop_Midi2UnsupportedStatusNibble
 * @tc.desc: MIDI2 Channel Voice with unsupported status nibble should be dropped in Midi2->Midi1 conversion.
 * @tc.type: FUNC
 */
HWTEST_F(UmpConverterUnitTest, TestDrop_Midi2UnsupportedStatusNibble, TestSize.Level1)
{
    // statusNibble=0xF is not a valid channel voice nibble in our converter
    const uint32_t w0 = MakeMidi2ChVoiceWord0(0, 0xF, 0, 0, 0);
    const uint32_t w1 = 0x00000000U;
    const uint32_t inWords[2] = { w0, w1 };

    std::vector<uint32_t> out;
    const bool ok = UmpConverter::ConvertMidi2ToMidi1(inWords, 2, out);

    EXPECT_FALSE(ok);
    EXPECT_TRUE(out.empty());
}

/**
 * @tc.name: TestNoMutation_WhenConvertFails
 * @tc.desc: When conversion fails, output vector should not be partially appended.
 * @tc.type: FUNC
 */
HWTEST_F(UmpConverterUnitTest, TestNoMutation_WhenConvertFails, TestSize.Level1)
{
    std::vector<uint32_t> out;
    out.push_back(0xDEADBEEFU); // sentinel

    const uint32_t in = (static_cast<uint32_t>(0x5) << SHIFT_MT);
    const size_t oldSize = out.size();

    const bool ok = UmpConverter::ConvertMidi1ToMidi2(&in, 1, out);

    EXPECT_FALSE(ok);
    EXPECT_EQ(out.size(), oldSize);
    EXPECT_EQ(out[0], 0xDEADBEEFU);
}
