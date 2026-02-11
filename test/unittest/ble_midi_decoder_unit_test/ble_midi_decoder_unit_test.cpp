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
#include <cstring>

namespace {
    constexpr uint8_t BLE_MIDI_TIMESTAMP_HIGH_MASK = 0xC0;
    constexpr uint8_t BLE_MIDI_TIMESTAMP_HIGH_SHIFT = 6;
    constexpr uint8_t BLE_MIDI_BYTE_MASK = 0x7F;

    std::vector<uint8_t> DecodeBleMidi(const uint8_t* src, size_t srcLen)
    {
        std::vector<uint8_t> midi1;
        if (srcLen < 2) {
            return midi1;
        }

        bool isStandardEncoding = true;
        uint8_t headerTimeBits = (src[0] >> BLE_MIDI_TIMESTAMP_HIGH_SHIFT) & 0x03;

        size_t i = 2;
        while (i < srcLen) {
            uint8_t byte = src[i];
            uint8_t timeBits = (byte >> BLE_MIDI_TIMESTAMP_HIGH_SHIFT) & 0x03;
            uint8_t low6Bits = byte & BLE_MIDI_BYTE_MASK;

            if (isStandardEncoding && byte >= 0x80 && byte < 0xC0) {
                isStandardEncoding = false;
            }

            uint8_t midiByte;
            if (isStandardEncoding && low6Bits >= 0x80) {
                midiByte = (low6Bits << 1) | 0x01;
            } else {
                midiByte = low6Bits;
            }

            if (midiByte >= 0x80 && timeBits == headerTimeBits) {
                i++;
                if (i >= srcLen) break;
                i++;
                continue;
            }

            midi1.push_back(midiByte);
            i++;
        }

        return midi1;
    }
}

using namespace testing;
using namespace testing::ext;

class BleMidiDecoderUnitTest : public testing::Test {
public:
    static void SetUpTestCase() {}
    static void TearDownTestCase() {}
    void SetUp() override {}
    void TearDown() override {}
};

HWTEST_F(BleMidiDecoderUnitTest, DecodeBleMidi_Sysex_Complete, TestSize.Level1)
{
    uint8_t bleInput[] = { 0xBA, 0xBA, 0xB8, 0x78, 0x83, 0x05, 0x80, 0x7E, 0xBB, 0x7B };
    uint8_t expected[] = { 0xF0, 0x03, 0x05, 0x00, 0x7E, 0xF7 };

    auto result = DecodeBleMidi(bleInput, sizeof(bleInput));

    ASSERT_EQ(result.size(), sizeof(expected));
    EXPECT_EQ(memcmp(result.data(), expected, sizeof(expected)), 0);
}

HWTEST_F(BleMidiDecoderUnitTest, DecodeBleMidi_NonStandardNoteOn, TestSize.Level1)
{
    uint8_t bleInput[] = { 0xBA, 0xBA, 0x90, 0x3C, 0x40 };
    uint8_t expected[] = { 0x90, 0x3C, 0x40 };

    auto result = DecodeBleMidi(bleInput, sizeof(bleInput));

    ASSERT_EQ(result.size(), sizeof(expected));
    EXPECT_EQ(memcmp(result.data(), expected, sizeof(expected)), 0);
}

HWTEST_F(BleMidiDecoderUnitTest, DecodeBleMidi_Sysex_NonStandardFormat, TestSize.Level1)
{
    uint8_t bleInput[] = { 0xBA, 0xBA, 0xF0, 0x03, 0x05, 0x00, 0x7E, 0xBA, 0xF7 };
    uint8_t expected[] = { 0xF0, 0x03, 0x05, 0x00, 0x7E, 0xF7 };

    auto result = DecodeBleMidi(bleInput, sizeof(bleInput));

    ASSERT_EQ(result.size(), sizeof(expected));
    EXPECT_EQ(memcmp(result.data(), expected, sizeof(expected)), 0);
}

HWTEST_F(BleMidiDecoderUnitTest, DecodeBleMidi_NoteOn, TestSize.Level1)
{
    uint8_t bleInput[] = { 0x80, 0x00, 0x90, 0x3C, 0x40 };
    uint8_t expected[] = { 0x90, 0x3C, 0x40 };

    auto result = DecodeBleMidi(bleInput, sizeof(bleInput));

    ASSERT_EQ(result.size(), sizeof(expected));
    EXPECT_EQ(memcmp(result.data(), expected, sizeof(expected)), 0);
}

HWTEST_F(BleMidiDecoderUnitTest, DecodeBleMidi_NoteOff, TestSize.Level1)
{
    uint8_t bleInput[] = { 0x80, 0x00, 0x80, 0x3C, 0x00 };
    uint8_t expected[] = { 0x80, 0x3C, 0x00 };

    auto result = DecodeBleMidi(bleInput, sizeof(bleInput));

    ASSERT_EQ(result.size(), sizeof(expected));
    EXPECT_EQ(memcmp(result.data(), expected, sizeof(expected)), 0);
}

HWTEST_F(BleMidiDecoderUnitTest, DecodeBleMidi_ProgramChange, TestSize.Level1)
{
    uint8_t bleInput[] = { 0x80, 0x00, 0xC0, 0x05 };
    uint8_t expected[] = { 0xC0, 0x05 };

    auto result = DecodeBleMidi(bleInput, sizeof(bleInput));

    ASSERT_EQ(result.size(), sizeof(expected));
    EXPECT_EQ(memcmp(result.data(), expected, sizeof(expected)), 0);
}

HWTEST_F(BleMidiDecoderUnitTest, DecodeBleMidi_ControlChange, TestSize.Level1)
{
    uint8_t bleInput[] = { 0x80, 0x00, 0xB0, 0x01, 0x7F };
    uint8_t expected[] = { 0xB0, 0x01, 0x7F };

    auto result = DecodeBleMidi(bleInput, sizeof(bleInput));

    ASSERT_EQ(result.size(), sizeof(expected));
    EXPECT_EQ(memcmp(result.data(), expected, sizeof(expected)), 0);
}

HWTEST_F(BleMidiDecoderUnitTest, DecodeBleMidi_SystemRealTime, TestSize.Level1)
{
    uint8_t bleInput[] = { 0x80, 0x00, 0xF8 };
    uint8_t expected[] = { 0xF8 };

    auto result = DecodeBleMidi(bleInput, sizeof(bleInput));

    ASSERT_EQ(result.size(), sizeof(expected));
    EXPECT_EQ(memcmp(result.data(), expected, sizeof(expected)), 0);
}

HWTEST_F(BleMidiDecoderUnitTest, DecodeBleMidi_SongPositionPointer, TestSize.Level1)
{
    uint8_t bleInput[] = { 0x80, 0x00, 0xF2, 0x00, 0x10 };
    uint8_t expected[] = { 0xF2, 0x00, 0x10 };

    auto result = DecodeBleMidi(bleInput, sizeof(bleInput));

    ASSERT_EQ(result.size(), sizeof(expected));
    EXPECT_EQ(memcmp(result.data(), expected, sizeof(expected)), 0);
}

HWTEST_F(BleMidiDecoderUnitTest, DecodeBleMidi_MultiplePackets, TestSize.Level1)
{
    uint8_t bleInput[] = {
        0x80, 0x00, 0x90, 0x3C, 0x40,
        0x81, 0x00, 0x90, 0x40, 0x45,
        0x82, 0x00, 0x90, 0x44, 0x4A
    };
    uint8_t expected[] = {
        0x90, 0x3C, 0x40,
        0x90, 0x40, 0x45,
        0x90, 0x44, 0x4A
    };

    auto result = DecodeBleMidi(bleInput, sizeof(bleInput));

    ASSERT_EQ(result.size(), sizeof(expected));
    EXPECT_EQ(memcmp(result.data(), expected, sizeof(expected)), 0);
}

HWTEST_F(BleMidiDecoderUnitTest, DecodeBleMidi_Sysex_MultiplePackets, TestSize.Level1)
{
    uint8_t bleInput[] = {
        0x80, 0x00, 0x78, 0x01, 0x02,
        0x80, 0x00, 0x03, 0x04, 0x05,
        0x80, 0x00, 0x06, 0x07, 0x7B
    };
    uint8_t expected[] = { 0xF0, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0xF7 };

    auto result = DecodeBleMidi(bleInput, sizeof(bleInput));

    ASSERT_EQ(result.size(), sizeof(expected));
    EXPECT_EQ(memcmp(result.data(), expected, sizeof(expected)), 0);
}

HWTEST_F(BleMidiDecoderUnitTest, DecodeBleMidi_EmptyInput, TestSize.Level1)
{
    uint8_t bleInput[] = { 0x80, 0x00 };

    auto result = DecodeBleMidi(bleInput, sizeof(bleInput));

    EXPECT_EQ(result.size(), 0);
}

HWTEST_F(BleMidiDecoderUnitTest, DecodeBleMidi_InvalidLength, TestSize.Level1)
{
    uint8_t bleInput[] = { 0x80 };

    auto result = DecodeBleMidi(bleInput, sizeof(bleInput));

    EXPECT_EQ(result.size(), 0);
}

HWTEST_F(BleMidiDecoderUnitTest, DecodeBleMidi_RunningStatus, TestSize.Level1)
{
    uint8_t bleInput[] = { 0x80, 0x00, 0x90, 0x3C, 0x40, 0x3C, 0x45, 0x3C, 0x50 };
    uint8_t expected[] = { 0x90, 0x3C, 0x40, 0x3C, 0x45, 0x3C, 0x50 };

    auto result = DecodeBleMidi(bleInput, sizeof(bleInput));

    ASSERT_EQ(result.size(), sizeof(expected));
    EXPECT_EQ(memcmp(result.data(), expected, sizeof(expected)), 0);
}

HWTEST_F(BleMidiDecoderUnitTest, DecodeBleMidi_PitchBend, TestSize.Level1)
{
    uint8_t bleInput[] = { 0x80, 0x00, 0xE0, 0x00, 0x40 };
    uint8_t expected[] = { 0xE0, 0x00, 0x40 };

    auto result = DecodeBleMidi(bleInput, sizeof(bleInput));

    ASSERT_EQ(result.size(), sizeof(expected));
    EXPECT_EQ(memcmp(result.data(), expected, sizeof(expected)), 0);
}

HWTEST_F(BleMidiDecoderUnitTest, DecodeBleMidi_ChannelPressure, TestSize.Level1)
{
    uint8_t bleInput[] = { 0x80, 0x00, 0xD0, 0x7F };
    uint8_t expected[] = { 0xD0, 0x7F };

    auto result = DecodeBleMidi(bleInput, sizeof(bleInput));

    ASSERT_EQ(result.size(), sizeof(expected));
    EXPECT_EQ(memcmp(result.data(), expected, sizeof(expected)), 0);
}

HWTEST_F(BleMidiDecoderUnitTest, DecodeBleMidi_PolyPressure, TestSize.Level1)
{
    uint8_t bleInput[] = { 0x80, 0x00, 0xA0, 0x3C, 0x40 };
    uint8_t expected[] = { 0xA0, 0x3C, 0x40 };

    auto result = DecodeBleMidi(bleInput, sizeof(bleInput));

    ASSERT_EQ(result.size(), sizeof(expected));
    EXPECT_EQ(memcmp(result.data(), expected, sizeof(expected)), 0);
}

HWTEST_F(BleMidiDecoderUnitTest, DecodeBleMidi_MidiTimeCodeQuarterFrame, TestSize.Level1)
{
    uint8_t bleInput[] = { 0x80, 0x00, 0xF1, 0x00 };
    uint8_t expected[] = { 0xF1, 0x00 };

    auto result = DecodeBleMidi(bleInput, sizeof(bleInput));

    ASSERT_EQ(result.size(), sizeof(expected));
    EXPECT_EQ(memcmp(result.data(), expected, sizeof(expected)), 0);
}

HWTEST_F(BleMidiDecoderUnitTest, DecodeBleMidi_SongSelect, TestSize.Level1)
{
    uint8_t bleInput[] = { 0x80, 0x00, 0xF3, 0x0A };
    uint8_t expected[] = { 0xF3, 0x0A };

    auto result = DecodeBleMidi(bleInput, sizeof(bleInput));

    ASSERT_EQ(result.size(), sizeof(expected));
    EXPECT_EQ(memcmp(result.data(), expected, sizeof(expected)), 0);
}
