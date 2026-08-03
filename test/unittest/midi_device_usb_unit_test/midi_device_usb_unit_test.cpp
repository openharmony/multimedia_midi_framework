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
            device.productId = "0x1234";
            device.vendorId = "0x5678";
            device.deviceName = "TestProduct";

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

    EXPECT_EQ(expectedDeviceId, devInfo.midiDeviceInfo.driverDeviceId);
    EXPECT_EQ(DeviceType::DEVICE_TYPE_USB, devInfo.midiDeviceInfo.deviceType);
    EXPECT_EQ(static_cast<TransportProtocol>(expectedProtocol), devInfo.midiDeviceInfo.transportProtocol);
    EXPECT_EQ("TestProduct", devInfo.midiDeviceInfo.deviceName);
    EXPECT_EQ(0x1234u, devInfo.midiDeviceInfo.productId);
    EXPECT_EQ(0x5678u, devInfo.midiDeviceInfo.vendorId);

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

/**
 * @tc.name: NullHdiBranches001
 * @tc.desc: Every HDI-backed operation returns a safe error when the interface is unavailable.
 * @tc.type: FUNC
 */
HWTEST_F(MidiDeviceUsbUnitTest, NullHdiBranches001, TestSize.Level0)
{
    UsbMidiTransportDeviceDriver driver;
    driver.midiHdi_ = nullptr;
    std::vector<MidiEventInner> events;
    EXPECT_TRUE(driver.GetRegisteredDevices().empty());
    EXPECT_EQ(driver.OpenDevice(1), OH_MIDI_STATUS_SYSTEM_ERROR);
    EXPECT_EQ(driver.CloseDevice(1), OH_MIDI_STATUS_SYSTEM_ERROR);
    EXPECT_EQ(driver.OpenInputPort(1, 2, nullptr), OH_MIDI_STATUS_SYSTEM_ERROR);
    EXPECT_EQ(driver.CloseInputPort(1, 2), OH_MIDI_STATUS_SYSTEM_ERROR);
    EXPECT_EQ(driver.OpenOutputPort(1, 2), OH_MIDI_STATUS_SYSTEM_ERROR);
    EXPECT_EQ(driver.CloseOutputPort(1, 2), OH_MIDI_STATUS_SYSTEM_ERROR);
    EXPECT_EQ(driver.HandleUmpInput(1, 2, events), OH_MIDI_STATUS_SYSTEM_ERROR);
    EXPECT_EQ(driver.OpenDevice("not-ble", nullptr), -1);
}

/**
 * @tc.name: RegisteredDeviceValidation001
 * @tc.desc: Verify HDI failure, invalid protocol filtering, and invalid port-direction filtering.
 * @tc.type: FUNC
 */
HWTEST_F(MidiDeviceUsbUnitTest, RegisteredDeviceValidation001, TestSize.Level0)
{
    sptr<MockIMidiInterface> mockMidiHdi = sptr<MockIMidiInterface>::MakeSptr();
    ASSERT_NE(mockMidiHdi, nullptr);
    UsbMidiTransportDeviceDriver driver;
    driver.midiHdi_ = mockMidiHdi;

    EXPECT_CALL(*mockMidiHdi, GetDeviceList(_)).WillOnce(Return(OH_MIDI_STATUS_SYSTEM_ERROR));
    EXPECT_TRUE(driver.GetRegisteredDevices().empty());

    EXPECT_CALL(*mockMidiHdi, GetDeviceList(_))
        .WillOnce(Invoke([](std::vector<HDI::Midi::V1_0::MidiDeviceInfo> &devices) {
            HDI::Midi::V1_0::MidiDeviceInfo invalidProtocol {};
    invalidProtocol.protocol = static_cast<HDI::Midi::V1_0::MidiProtocol>(99);
            devices.push_back(invalidProtocol);

            HDI::Midi::V1_0::MidiDeviceInfo valid {};
            valid.deviceId = 2;
            valid.protocol = HDI::Midi::V1_0::MIDI_PROTOCOL_2_0;
            valid.productId = "invalid";
            valid.vendorId = "0X10";
            HDI::Midi::V1_0::MidiPortInfo invalidPort {};
    invalidPort.direction = static_cast<HDI::Midi::V1_0::MidiPortDirection>(99);
            valid.ports.push_back(invalidPort);
            HDI::Midi::V1_0::MidiPortInfo outputPort {};
            outputPort.portId = 3;
            outputPort.direction = HDI::Midi::V1_0::PORT_DIRECTION_OUTPUT;
            valid.ports.push_back(outputPort);
            devices.push_back(valid);
            return OH_MIDI_STATUS_OK;
        }));
    auto devices = driver.GetRegisteredDevices();
    ASSERT_EQ(devices.size(), 1);
    EXPECT_EQ(devices[0].midiDeviceInfo.transportProtocol, PROTOCOL_2_0);
    EXPECT_EQ(devices[0].midiDeviceInfo.productId, 0);
    EXPECT_EQ(devices[0].midiDeviceInfo.vendorId, 0x10);
    ASSERT_EQ(devices[0].portInfos.size(), 1);
    EXPECT_EQ(devices[0].portInfos[0].direction, PORT_DIRECTION_OUTPUT);
}

/**
 * @tc.name: OutputAndSendBranches001
 * @tc.desc: Verify output-port forwarding and empty/non-empty UMP batch conversion.
 * @tc.type: FUNC
 */
HWTEST_F(MidiDeviceUsbUnitTest, OutputAndSendBranches001, TestSize.Level0)
{
    constexpr int64_t deviceId = 3;
    constexpr uint32_t portIndex = 4;
    sptr<MockIMidiInterface> mockMidiHdi = sptr<MockIMidiInterface>::MakeSptr();
    ASSERT_NE(mockMidiHdi, nullptr);
    UsbMidiTransportDeviceDriver driver;
    driver.midiHdi_ = mockMidiHdi;

    EXPECT_CALL(*mockMidiHdi, OpenOutputPort(deviceId, portIndex))
        .WillOnce(Return(OH_MIDI_STATUS_OK));
    EXPECT_CALL(*mockMidiHdi, CloseOutputPort(deviceId, portIndex))
        .WillOnce(Return(OH_MIDI_STATUS_SYSTEM_ERROR));
    EXPECT_EQ(driver.OpenOutputPort(deviceId, portIndex), OH_MIDI_STATUS_OK);
    EXPECT_EQ(driver.CloseOutputPort(deviceId, portIndex), OH_MIDI_STATUS_SYSTEM_ERROR);

    EXPECT_CALL(*mockMidiHdi, SendMidiMessages(deviceId, portIndex, _))
        .WillOnce(Invoke([](int64_t, uint32_t,
            const std::vector<HDI::Midi::V1_0::MidiMessage> &messages) {
            EXPECT_TRUE(messages.empty());
            return OH_MIDI_STATUS_OK;
        }))
        .WillOnce(Invoke([](int64_t, uint32_t,
            const std::vector<HDI::Midi::V1_0::MidiMessage> &messages) {
            EXPECT_EQ(messages.size(), 2);
            if (messages.size() == 2) {
                EXPECT_TRUE(messages[0].data.empty());
                EXPECT_EQ(messages[1].data.size(), 2);
            }
            return OH_MIDI_STATUS_SYSTEM_ERROR;
        }));
    std::vector<MidiEventInner> events;
    EXPECT_EQ(driver.HandleUmpInput(deviceId, portIndex, events), OH_MIDI_STATUS_OK);
    uint32_t words[] = {0x11223344, 0x55667788};
    events.push_back({1, 0, nullptr});
    events.push_back({2, 2, words});
    EXPECT_EQ(driver.HandleUmpInput(deviceId, portIndex, events), OH_MIDI_STATUS_SYSTEM_ERROR);
}

/**
 * @tc.name: DriverCallbackFiltering001
 * @tc.desc: Verify first/second callback QoS paths, empty-message filtering, and empty-batch return.
 * @tc.type: FUNC
 */
HWTEST_F(MidiDeviceUsbUnitTest, DriverCallbackFiltering001, TestSize.Level0)
{
    size_t callbackCount = 0;
    UsbDriverCallback callback([&callbackCount](std::vector<MidiEventInner> &events) {
        callbackCount += events.size();
    });
    std::vector<HDI::Midi::V1_0::MidiMessage> messages;
    EXPECT_EQ(callback.OnMidiDataReceived(messages), OH_MIDI_STATUS_OK);
    EXPECT_EQ(callbackCount, 0);

    HDI::Midi::V1_0::MidiMessage emptyMessage {};
    HDI::Midi::V1_0::MidiMessage validMessage {};
    validMessage.timestamp = 1;
    validMessage.data = {0x20903C40};
    messages = {emptyMessage, validMessage};
    EXPECT_EQ(callback.OnMidiDataReceived(messages), OH_MIDI_STATUS_OK);
    EXPECT_EQ(callbackCount, 1);
}
