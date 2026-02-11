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
#include <cstring>
#include "ump_processor.h"

using namespace testing;
using namespace testing::ext;

class BleMidiDecoderUnitTest : public testing::Test {
public:
    static void SetUpTestCase() {}
    static void TearDownTestCase() {}
    void SetUp() override {}
    void TearDown() override {}
};

// Case 1: Sysex split with timestamps
// Input:  A4 8C F0 03 05 00 01 8C F7
// Expect: F0 03 05 00 01 F7
HWTEST_F(BleMidiDecoderUnitTest, DecodeBleMidi_FixCase1_SysexComplex, TestSize.Level1)
{
    uint8_t bleInput[] = { 0xA4, 0x8C, 0xF0, 0x03, 0x05, 0x00, 0x01, 0x8C, 0xF7 };
    uint8_t expected[] = { 0xF0, 0x03, 0x05, 0x00, 0x01, 0xF7 };

    UmpProcessor processor;
    auto result = processor.DecodeBleMidi(bleInput, sizeof(bleInput));

    ASSERT_EQ(result.size(), sizeof(expected));
    EXPECT_EQ(memcmp(result.data(), expected, sizeof(expected)), 0);
}

// Case 2: Another Sysex variant
// Input:  9B C1 F0 03 05 00 01 E2 F7
// Expect: F0 03 05 00 01 F7
HWTEST_F(BleMidiDecoderUnitTest, DecodeBleMidi_FixCase2_SysexVariant, TestSize.Level1)
{
    uint8_t bleInput[] = { 0x9B, 0xC1, 0xF0, 0x03, 0x05, 0x00, 0x01, 0xE2, 0xF7 };
    uint8_t expected[] = { 0xF0, 0x03, 0x05, 0x00, 0x01, 0xF7 };

    UmpProcessor processor;
    auto result = processor.DecodeBleMidi(bleInput, sizeof(bleInput));

    ASSERT_EQ(result.size(), sizeof(expected));
    EXPECT_EQ(memcmp(result.data(), expected, sizeof(expected)), 0);
}

// Case 3: Standard Note On
// Input:  84 94 90 24 29
// Expect: 90 24 29
HWTEST_F(BleMidiDecoderUnitTest, DecodeBleMidi_FixCase3_NoteOn, TestSize.Level1)
{
    uint8_t bleInput[] = { 0x84, 0x94, 0x90, 0x24, 0x29 };
    uint8_t expected[] = { 0x90, 0x24, 0x29 };

    UmpProcessor processor;
    auto result = processor.DecodeBleMidi(bleInput, sizeof(bleInput));

    ASSERT_EQ(result.size(), sizeof(expected));
    EXPECT_EQ(memcmp(result.data(), expected, sizeof(expected)), 0);
}

// Case 4: Note Off
// Input:  85 D0 80 24 7F
// Expect: 80 24 7F
HWTEST_F(BleMidiDecoderUnitTest, DecodeBleMidi_FixCase4_NoteOff, TestSize.Level1)
{
    uint8_t bleInput[] = { 0x85, 0xD0, 0x80, 0x24, 0x7F };
    uint8_t expected[] = { 0x80, 0x24, 0x7F };

    UmpProcessor processor;
    auto result = processor.DecodeBleMidi(bleInput, sizeof(bleInput));

    ASSERT_EQ(result.size(), sizeof(expected));
    EXPECT_EQ(memcmp(result.data(), expected, sizeof(expected)), 0);
}

// Case 5: Sysex with Zero Data
// Input:  89 BB F0 03 05 00 00 BB F7
// Expect: F0 03 05 00 00 F7
HWTEST_F(BleMidiDecoderUnitTest, DecodeBleMidi_FixCase5_SysexZeroData, TestSize.Level1)
{
    uint8_t bleInput[] = { 0x89, 0xBB, 0xF0, 0x03, 0x05, 0x00, 0x00, 0xBB, 0xF7 };
    uint8_t expected[] = { 0xF0, 0x03, 0x05, 0x00, 0x00, 0xF7 };

    UmpProcessor processor;
    auto result = processor.DecodeBleMidi(bleInput, sizeof(bleInput));

    ASSERT_EQ(result.size(), sizeof(expected));
    EXPECT_EQ(memcmp(result.data(), expected, sizeof(expected)), 0);
}


HWTEST_F(BleMidiDecoderUnitTest, DecodeBleMidi_RunningStatus, TestSize.Level1)
{
    // Original test adapted to new logic: TS(80) NoteOn(90) D1 D2 TS(3C? No, 3C is data)
    // Wait, old test data: 80 00 90 3C 40 3C 45 ...
    // Standard BLE Running Status: Header, Timestamp, Status, Data, Data, Timestamp(Optional), Data, Data
    // The old test data `0x80, 0x00, 0x90...`
    // 0x80 Header
    // 0x00 (Data byte at start?? Invalid BLE MIDI, but new parser handles as data) -> Push 00
    // 0x90 (Status or TS). Preceded by Data. Treated as TS.
    // 0x3C (Data).
    // result: 00 3C ... (Status 90 eaten).

    // NOTE: The original test data may be based on incorrect understanding (e.g., treating 0x00 as Timestamp low).
    // Standard BLE MIDI packets must start with Header, followed by Timestamp (>=0x80).
    // Here is a test data conforming to standard Running Status:

    // Header(80), TS(80), NoteOn(90), 3C, 40, TS(80 - time update), 3C, 45
    uint8_t bleInput[] = { 0x80, 0x80, 0x90, 0x3C, 0x40, 0x80, 0x3C, 0x45 };
    uint8_t expected[] = { 0x90, 0x3C, 0x40, 0x3C, 0x45 };

    UmpProcessor processor;
    auto result = processor.DecodeBleMidi(bleInput, sizeof(bleInput));

    ASSERT_EQ(result.size(), sizeof(expected));
    EXPECT_EQ(memcmp(result.data(), expected, sizeof(expected)), 0);
}

HWTEST_F(BleMidiDecoderUnitTest, DecodeBleMidi_RealTime, TestSize.Level1)
{
    // Header, TS, Start(FA), Continue(FB)
    uint8_t bleInput[] = { 0x80, 0x80, 0xFA, 0xFB };
    uint8_t expected[] = { 0xFA, 0xFB };
    UmpProcessor processor;
    auto result = processor.DecodeBleMidi(bleInput, sizeof(bleInput));
    ASSERT_EQ(result.size(), sizeof(expected));
    EXPECT_EQ(memcmp(result.data(), expected, sizeof(expected)), 0);
}