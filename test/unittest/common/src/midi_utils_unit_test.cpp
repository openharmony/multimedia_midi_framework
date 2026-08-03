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

#include <sys/eventfd.h>
#include <unistd.h>

#include <gtest/gtest.h>

#include "message_parcel.h"
#include "midi_utils.h"
#include "ump_packet.h"

using namespace testing::ext;

namespace OHOS {
namespace MIDI {

class MidiUtilsUnitTest : public testing::Test {
};

/**
 * @tc.name: StringAndDumpBranches001
 * @tc.desc: Verify encryption, byte formatting, empty/null payloads, separators, and vector overloads.
 * @tc.type: FUNC
 */
HWTEST_F(MidiUtilsUnitTest, StringAndDumpBranches001, TestSize.Level0)
{
    EXPECT_EQ(GetEncryptStr(""), "");
    EXPECT_EQ(GetEncryptStr("abc"), "*bc");
    EXPECT_EQ(GetEncryptStr("12345678"), "12*45678");
    EXPECT_EQ(BytesToString(0x1234ABCD), "12 34 AB CD");

    uint32_t words[] = {0x11223344, 0xAABBCCDD};
    EXPECT_NE(DumpOneEvent(1, 0, nullptr).find("<empty>"), std::string::npos);
    EXPECT_NE(DumpOneEvent(1, 1, nullptr).find("<null>"), std::string::npos);
    EXPECT_NE(DumpOneEvent(1, 2, words).find(" | "), std::string::npos);

    std::vector<MidiEvent> events;
    EXPECT_NE(DumpMidiEvents(events).find("count=0"), std::string::npos);
    events.push_back({1, 1, words});
    events.push_back({2, 0, words});
    EXPECT_NE(DumpMidiEvents(events).find("[1]"), std::string::npos);

    std::vector<MidiEventInner> innerEvents;
    EXPECT_NE(DumpMidiEvents(innerEvents).find("count=0"), std::string::npos);
    innerEvents.push_back({1, 1, words});
    innerEvents.push_back({2, 2, words});
    EXPECT_NE(DumpMidiEvents(innerEvents).find("[1]"), std::string::npos);
}

/**
 * @tc.name: ConversionTemplateBranches001
 * @tc.desc: Verify decimal/hex parser validation and all SysEx status selections.
 * @tc.type: FUNC
 */
HWTEST_F(MidiUtilsUnitTest, ConversionTemplateBranches001, TestSize.Level0)
{
    uint32_t value = 0;
    EXPECT_FALSE(StringToDecNum("", value));
    EXPECT_TRUE(StringToDecNum("123", value));
    EXPECT_EQ(value, 123);
    EXPECT_FALSE(StringToDecNum("12x", value));
    EXPECT_FALSE(StringToHexNum("0x", value));
    EXPECT_TRUE(StringToHexNum("0x1a", value));
    EXPECT_EQ(value, 0x1A);
    EXPECT_TRUE(StringToHexNum("0X2B", value));
    EXPECT_EQ(value, 0x2B);
    EXPECT_TRUE(StringToHexNum("3c", value));
    EXPECT_EQ(value, 0x3C);

    EXPECT_EQ(GetSysexStatus(0, 1), SYSEX7_COMPLETE);
    EXPECT_EQ(GetSysexStatus(0, 3), SYSEX7_START);
    EXPECT_EQ(GetSysexStatus(1, 3), SYSEX7_CONTINUE);
    EXPECT_EQ(GetSysexStatus(2, 3), SYSEX7_END);
}

/**
 * @tc.name: PackAndObjectBranches001
 * @tc.desc: Verify empty/full SysEx packing, UMP packet bounds, parcel allocation, and UniqueFd self move.
 * @tc.type: FUNC
 */
HWTEST_F(MidiUtilsUnitTest, PackAndObjectBranches001, TestSize.Level0)
{
    auto emptyPacket = PackSysEx7Ump64(0, SYSEX7_COMPLETE, nullptr, 0);
    EXPECT_EQ(emptyPacket[0], 0x30000000);
    uint8_t bytes[MAX_PACKET_BYTES] = {1, 2, 3, 4, 5, 6};
    auto fullPacket = PackSysEx7Ump64(0x1F, 0xF, bytes, MAX_PACKET_BYTES);
    EXPECT_NE(fullPacket[0], 0);
    EXPECT_NE(fullPacket[1], 0);

    UmpPacket single(0x12345678);
    EXPECT_EQ(single.WordCount(), 1);
    EXPECT_EQ(single.Word(0), 0x12345678);
    EXPECT_EQ(single.Word(4), 0);
    UmpPacket limited({1, 2, 3, 4, 5});
    EXPECT_EQ(limited.WordCount(), 4);
    EXPECT_EQ(limited.Word(3), 4);

    MessageParcel portParcel;
    MidiPortInfo portInfo;
    portInfo.portId = 1;
    portInfo.name = "port";
    portInfo.direction = PORT_DIRECTION_INPUT;
    portInfo.transportProtocol = PROTOCOL_1_0;
    ASSERT_TRUE(portInfo.Marshalling(portParcel));
    std::unique_ptr<MidiPortInfo> restoredPort(MidiPortInfo::Unmarshalling(portParcel));
    ASSERT_NE(restoredPort, nullptr);
    EXPECT_EQ(restoredPort->portId, 1);

    MessageParcel deviceParcel;
    MidiDeviceInfo deviceInfo;
    deviceInfo.deviceId = 2;
    deviceInfo.deviceName = "device";
    ASSERT_TRUE(deviceInfo.Marshalling(deviceParcel));
    std::unique_ptr<MidiDeviceInfo> restoredDevice(MidiDeviceInfo::Unmarshalling(deviceParcel));
    ASSERT_NE(restoredDevice, nullptr);
    EXPECT_EQ(restoredDevice->deviceId, 2);

    int fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT_GE(fd, 0);
    UniqueFd uniqueFd(fd);
    UniqueFd *sameFd = &uniqueFd;
    uniqueFd = std::move(*sameFd);
    EXPECT_EQ(uniqueFd.Get(), fd);
}

/**
 * @tc.name: CloseFdBranches001
 * @tc.desc: Verify all standard-descriptor warnings and the normal descriptor path while restoring test IO.
 * @tc.type: FUNC
 */
HWTEST_F(MidiUtilsUnitTest, CloseFdBranches001, TestSize.Level0)
{
    for (int fd : {STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO}) {
        int savedFd = dup(fd);
        ASSERT_GE(savedFd, 0);
        CloseFd(fd);
        ASSERT_EQ(dup2(savedFd, fd), fd);
        close(savedFd);
    }

    int normalFd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT_GE(normalFd, 0);
    CloseFd(normalFd);
    EXPECT_EQ(close(normalFd), -1);
}

} // namespace MIDI
} // namespace OHOS
