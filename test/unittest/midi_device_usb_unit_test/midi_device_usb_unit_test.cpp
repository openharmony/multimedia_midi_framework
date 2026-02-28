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

#include "midi_device_driver.h"
#include "midi_device_usb.h"
#include "midi_info.h"
#include "native_midi_base.h"
#include "v1_0/imidi_interface.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace OHOS;
using namespace MIDI;
using namespace testing;
using namespace testing::ext;

class MockIMidiInterface : public HDI::Midi::V1_0::IMidiInterface {
public:
    MOCK_METHOD(int32_t, GetDeviceList, (std::vector<HDI::Midi::V1_0::MidiDeviceInfo> & deviceList), (override));
    MOCK_METHOD(int32_t, OpenDevice, (int64_t deviceId), (override));
    MOCK_METHOD(int32_t, CloseDevice, (int64_t deviceId), (override));
    MOCK_METHOD(int32_t, OpenInputPort,
                (int64_t deviceId, uint32_t portId, const sptr<HDI::Midi::V1_0::IMidiCallback> &dataCallback),
                (override));
    MOCK_METHOD(int32_t, OpenOutputPort, (int64_t deviceId, uint32_t portId), (override));
    MOCK_METHOD(int32_t, CloseInputPort, (int64_t deviceId, uint32_t portId), (override));
    MOCK_METHOD(int32_t, CloseOutputPort, (int64_t deviceId, uint32_t portId), (override));
    MOCK_METHOD(int32_t, SendMidiMessages,
                (int64_t deviceId, uint32_t portId, const std::vector<HDI::Midi::V1_0::MidiMessage> &messages),
                (override));
};

class MidiDeviceUsbUnitTest : public testing::Test {
public:
};

/**
 * @tc.name: GetRegisteredDevices_001
 * @tc.desc: GetDeviceList returns empty -> GetRegisteredDevices should return empty.
 * @tc.type: FUNC
 */
HWTEST_F(MidiDeviceUsbUnitTest, GetRegisteredDevices_001, TestSize.Level0)
{
    sptr<MockIMidiInterface> mockMidiHdi = sptr<MockIMidiInterface>::MakeSptr();
    ASSERT_NE(nullptr, mockMidiHdi);

    UsbMidiTransportDeviceDriver driver;
    driver.midiHdi_ = mockMidiHdi;

    EXPECT_CALL(*mockMidiHdi, GetDeviceList(_))
        .Times(1)
        .WillOnce(Invoke([](std::vector<HDI::Midi::V1_0::MidiDeviceInfo> &deviceList) {
            deviceList.clear();
            return OH_MIDI_STATUS_OK;
        }));

    auto deviceInfos = driver.GetRegisteredDevices();
    EXPECT_TRUE(deviceInfos.empty());
}

/**
 * @tc.name: GetRegisteredDevices_002
 * @tc.desc: Single device with multiple ports -> verify field mapping + ConvertToDeviceInformation loop.
 * @tc.type: FUNC
 */
HWTEST_F(MidiDeviceUsbUnitTest, GetRegisteredDevices_002, TestSize.Level0)
{
    sptr<MockIMidiInterface> mockMidiHdi = sptr<MockIMidiInterface>::MakeSptr();

    UsbMidiTransportDeviceDriver driver;
    driver.midiHdi_ = mockMidiHdi;

    int64_t expectedDeviceId = 12345;
    int32_t expectedProtocol = 1;
    uint32_t expectedPortId0 = 10;
    uint32_t expectedPortId1 = 11;
    int32_t expectedDirection0 = 0;
    int32_t expectedDirection1 = 1;

    EXPECT_CALL(*mockMidiHdi, GetDeviceList(_))
        .Times(1)
        .WillOnce(Invoke([&](std::vector<HDI::Midi::V1_0::MidiDeviceInfo> &deviceList) {
            deviceList.clear();

            HDI::Midi::V1_0::MidiDeviceInfo device{};
            device.deviceId = expectedDeviceId;
            device.protocol = HDI::Midi::V1_0::MIDI_PROTOCOL_1_0;
            device.productName = "TestProduct";
            device.vendorName = "TestVendor";

            HDI::Midi::V1_0::MidiPortInfo port0{};
            port0.portId = expectedPortId0;
            port0.name = "InputPort0";
            port0.direction = HDI::Midi::V1_0::PORT_DIRECTION_INPUT;

            HDI::Midi::V1_0::MidiPortInfo port1{};
            port1.portId = expectedPortId1;
            port1.name = "OutputPort1";
            port1.direction = HDI::Midi::V1_0::PORT_DIRECTION_OUTPUT;

            device.ports.push_back(port0);
            device.ports.push_back(port1);

            deviceList.push_back(device);
            return OH_MIDI_STATUS_OK;
        }));

    auto deviceInfos = driver.GetRegisteredDevices();

    ASSERT_EQ(1u, deviceInfos.size());
    const auto &devInfo = deviceInfos[0];

    EXPECT_EQ(expectedDeviceId, devInfo.driverDeviceId);
    EXPECT_EQ(DEVICE_TYPE_USB, devInfo.deviceType);
    EXPECT_EQ(static_cast<TransportProtocol>(expectedProtocol), devInfo.transportProtocol);
    EXPECT_EQ("TestProduct", devInfo.productId);
    EXPECT_EQ("TestVendor", devInfo.vendorId);

    ASSERT_EQ(2u, devInfo.portInfos.size());

    EXPECT_EQ(expectedPortId0, devInfo.portInfos[0].portId);
    EXPECT_EQ("InputPort0", devInfo.portInfos[0].name);
    EXPECT_EQ(static_cast<PortDirection>(expectedDirection0), devInfo.portInfos[0].direction);
    EXPECT_EQ(static_cast<TransportProtocol>(expectedProtocol), devInfo.portInfos[0].transportProtocol);

    EXPECT_EQ(expectedPortId1, devInfo.portInfos[1].portId);
    EXPECT_EQ("OutputPort1", devInfo.portInfos[1].name);
    EXPECT_EQ(static_cast<PortDirection>(expectedDirection1), devInfo.portInfos[1].direction);
    EXPECT_EQ(static_cast<TransportProtocol>(expectedProtocol), devInfo.portInfos[1].transportProtocol);
}

/**
 * @tc.name: OpenDevice001
 * @tc.desc: open correct deviceId, expect ok
 * @tc.type: FUNC
 */
HWTEST_F(MidiDeviceUsbUnitTest, OpenDevice001, TestSize.Level0)
{
    sptr<MockIMidiInterface> mockMidiHdi = sptr<MockIMidiInterface>::MakeSptr();
    EXPECT_CALL(*mockMidiHdi, OpenDevice(123)).WillOnce(Return(0));

    UsbMidiTransportDeviceDriver driver;
    driver.midiHdi_ = mockMidiHdi;
    EXPECT_EQ(0, driver.OpenDevice(123));
}

/**
 * @tc.name: CloseDevice001
 * @tc.desc: close correct deviceId, expect ok
 * @tc.type: FUNC
 */
HWTEST_F(MidiDeviceUsbUnitTest, CloseDevice001, TestSize.Level0)
{
    sptr<MockIMidiInterface> mockMidiHdi = sptr<MockIMidiInterface>::MakeSptr();

    EXPECT_CALL(*mockMidiHdi, CloseDevice(123)).WillOnce(Return(0));

    UsbMidiTransportDeviceDriver driver;
    driver.midiHdi_ = mockMidiHdi;
    EXPECT_EQ(0, driver.CloseDevice(123));
}

/**
 * @tc.name: OpenInputPort001
 * @tc.desc: open correct port, expect ok
 * @tc.type: FUNC
 */
HWTEST_F(MidiDeviceUsbUnitTest, OpenInputPort001, TestSize.Level0)
{
    constexpr int64_t deviceId = 100;
    constexpr uint32_t portIndex = 1;
    sptr<MockIMidiInterface> mockMidiHdi = sptr<MockIMidiInterface>::MakeSptr();
    ASSERT_NE(nullptr, mockMidiHdi);
    UsbMidiTransportDeviceDriver driver;
    driver.midiHdi_ = mockMidiHdi;

    bool callbackCalled = false;
    std::vector<MidiEventInner> receivedEvents;
    UmpInputCallback inputCallback = [&](std::vector<MidiEventInner> &events) {
        callbackCalled = true;
        receivedEvents = events;
    };

    sptr<HDI::Midi::V1_0::IMidiCallback> capturedCallback = nullptr;

    EXPECT_CALL(*mockMidiHdi, OpenInputPort(deviceId, portIndex, _))
        .Times(1)
        .WillOnce(Invoke([&](int64_t, uint32_t, const sptr<HDI::Midi::V1_0::IMidiCallback> &dataCallback) {
            capturedCallback = dataCallback;
            return OH_MIDI_STATUS_OK;
        }));

    EXPECT_EQ(OH_MIDI_STATUS_OK, driver.OpenInputPort(deviceId, portIndex, inputCallback));
    ASSERT_NE(nullptr, capturedCallback);

    std::vector<HDI::Midi::V1_0::MidiMessage> messages;
    HDI::Midi::V1_0::MidiMessage message1;
    message1.timestamp = 123;
    message1.data = {0x11223344u, 0x55667788u};

    HDI::Midi::V1_0::MidiMessage message2;
    message2.timestamp = 456;
    message2.data = {0xAABBCCDDu};

    messages.push_back(message1);
    messages.push_back(message2);

    EXPECT_EQ(0, capturedCallback->OnMidiDataReceived(messages));
    EXPECT_TRUE(callbackCalled);
    ASSERT_EQ(2u, receivedEvents.size());
    EXPECT_EQ(123u, receivedEvents[0].timestamp);
    EXPECT_EQ(2u, receivedEvents[0].length);
    ASSERT_NE(nullptr, receivedEvents[0].data);

    EXPECT_EQ(456u, receivedEvents[1].timestamp);
    EXPECT_EQ(1u, receivedEvents[1].length);
    ASSERT_NE(nullptr, receivedEvents[1].data);
}

/**
 * @tc.name: CloseInputPort001
 * @tc.desc: close correct port, expect ok
 * @tc.type: FUNC
 */
HWTEST_F(MidiDeviceUsbUnitTest, CloseInputPort001, TestSize.Level0)
{
    constexpr int64_t deviceId = 100;
    constexpr uint32_t portIndex = 1;
    sptr<MockIMidiInterface> mockMidiHdi = sptr<MockIMidiInterface>::MakeSptr();
    ASSERT_NE(nullptr, mockMidiHdi);
    UsbMidiTransportDeviceDriver driver;
    driver.midiHdi_ = mockMidiHdi;

    EXPECT_CALL(*mockMidiHdi, CloseInputPort(deviceId, portIndex)).WillOnce(Return(OH_MIDI_STATUS_OK));

    EXPECT_EQ(OH_MIDI_STATUS_OK, driver.CloseInputPort(deviceId, portIndex));
}