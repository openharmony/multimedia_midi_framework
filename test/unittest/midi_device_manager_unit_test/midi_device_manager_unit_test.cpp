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
#include "midi_device_mananger.h"
#include "midi_info.h"
#include "midi_test_common.h"
#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace OHOS;
using namespace MIDI;
using namespace testing;
using namespace testing::ext;

class MidiDeviceManagerUnitTest : public testing::Test {
public:
    void SetUp() override
    {
        manager_ = std::make_unique<MidiDeviceManager>();
        mockUsbDriver_ = std::make_unique<MockMidiDeviceDriver>();
        rawUsbDriver_ = mockUsbDriver_.get();
        // Use test helper to inject mock driver
        manager_->InjectDriverForTest(DeviceType::DEVICE_TYPE_USB, std::move(mockUsbDriver_));
    }

    void TearDown() override
    {
        // Use test helper to clear state
        manager_->ClearStateForTest();
    }

    DeviceInformation CreateDriverDeviceInfo(int64_t driverId, std::string name)
    {
        DeviceInformation info;
        info.driverDeviceId = driverId;
        info.deviceType = DeviceType::DEVICE_TYPE_USB;
        info.deviceName = name;
        info.productId = "1234";
        info.vendorId = "5678";
        info.transportProtocol = TransportProtocol::PROTOCOL_1_0;
        return info;
    }

protected:
    std::unique_ptr<MidiDeviceManager> manager_;
    std::unique_ptr<MockMidiDeviceDriver> mockUsbDriver_;
    MockMidiDeviceDriver *rawUsbDriver_ = nullptr;
};

/**
 * @tc.name: GetDevices001
 * @tc.desc: Verify empty list is returned when no drivers have devices
 * @tc.type: FUNC
 */
HWTEST_F(MidiDeviceManagerUnitTest, GetDevices001, TestSize.Level0)
{
    EXPECT_CALL(*rawUsbDriver_, GetRegisteredDevices()).WillOnce(Return(std::vector<DeviceInformation>{}));

    manager_->UpdateDevices();
    auto devices = manager_->GetDevices();
    EXPECT_TRUE(devices.empty());
}

/**
 * @tc.name: UpdateDevices001
 * @tc.desc: Verify device discovery and ID mapping
 * @tc.type: FUNC
 */
HWTEST_F(MidiDeviceManagerUnitTest, UpdateDevices001, TestSize.Level0)
{
    int64_t driverId = 101;
    std::string prodName = "Test Piano";
    std::vector<DeviceInformation> driverDevs = {CreateDriverDeviceInfo(driverId, prodName)};

    EXPECT_CALL(*rawUsbDriver_, GetRegisteredDevices()).WillOnce(Return(driverDevs));

    manager_->UpdateDevices();

    auto devices = manager_->GetDevices();
    ASSERT_EQ(devices.size(), 1);

    EXPECT_EQ(devices[0].productId, prodName);
    EXPECT_EQ(devices[0].driverDeviceId, driverId);
    EXPECT_NE(devices[0].deviceId, 0);

    EXPECT_TRUE(manager_->HasDriverMappingForTest(driverId));
}

/**
 * @tc.name: OpenDevice001
 * @tc.desc: Successfully open a device using its Global ID
 * @tc.type: FUNC
 */
HWTEST_F(MidiDeviceManagerUnitTest, OpenDevice001, TestSize.Level0)
{
    int64_t driverId = 202;
    std::vector<DeviceInformation> driverDevs = {CreateDriverDeviceInfo(driverId, "USB MIDI")};
    EXPECT_CALL(*rawUsbDriver_, GetRegisteredDevices()).WillOnce(Return(driverDevs));
    manager_->UpdateDevices();

    int64_t globalId = manager_->GetDevices()[0].deviceId;

    EXPECT_CALL(*rawUsbDriver_, OpenDevice(driverId)).WillOnce(Return(MIDI_STATUS_OK));

    int32_t ret = manager_->OpenDevice(globalId);
    EXPECT_EQ(ret, MIDI_STATUS_OK);
}

/**
 * @tc.name: OpenDevice002
 * @tc.desc: Fail to open non-existent global ID
 * @tc.type: FUNC
 */
HWTEST_F(MidiDeviceManagerUnitTest, OpenDevice002, TestSize.Level0)
{
    int64_t fakeGlobalId = 999999;

    EXPECT_CALL(*rawUsbDriver_, OpenDevice(_)).Times(0);

    int32_t ret = manager_->OpenDevice(fakeGlobalId);
    EXPECT_NE(ret, MIDI_STATUS_OK);
}

/**
 * @tc.name: CloseDevice001
 * @tc.desc: Successfully close a device
 * @tc.type: FUNC
 */
HWTEST_F(MidiDeviceManagerUnitTest, CloseDevice001, TestSize.Level0)
{
    int64_t driverId = 303;
    std::vector<DeviceInformation> driverDevs = {CreateDriverDeviceInfo(driverId, "USB MIDI")};
    EXPECT_CALL(*rawUsbDriver_, GetRegisteredDevices()).WillOnce(Return(driverDevs));
    manager_->UpdateDevices();
    int64_t globalId = manager_->GetDevices()[0].deviceId;

    EXPECT_CALL(*rawUsbDriver_, CloseDevice(driverId)).WillOnce(Return(MIDI_STATUS_OK));

    int32_t ret = manager_->CloseDevice(globalId);
    EXPECT_EQ(ret, MIDI_STATUS_OK);
}

/**
 * @tc.name: OpenInputPort001
 * @tc.desc: Verify OpenInputPort is routed to driver
 * @tc.type: FUNC
 */
HWTEST_F(MidiDeviceManagerUnitTest, OpenInputPort001, TestSize.Level0)
{
    int64_t driverId = 404;
    uint32_t portIndex = 1;
    std::vector<DeviceInformation> driverDevs = {CreateDriverDeviceInfo(driverId, "USB MIDI")};
    EXPECT_CALL(*rawUsbDriver_, GetRegisteredDevices()).WillOnce(Return(driverDevs));
    manager_->UpdateDevices();
    int64_t globalId = manager_->GetDevices()[0].deviceId;

    EXPECT_CALL(*rawUsbDriver_, OpenInputPort(driverId, portIndex, _)).WillOnce(Return(MIDI_STATUS_OK));
    std::shared_ptr<DeviceConnectionForInput> inputConnection = nullptr;
    int32_t ret = manager_->OpenInputPort(inputConnection, globalId, portIndex);
    EXPECT_EQ(ret, MIDI_STATUS_OK);
}

/**
 * @tc.name: CloseInputPort001
 * @tc.desc: Verify CloseInputPort is routed to driver
 * @tc.type: FUNC
 */
HWTEST_F(MidiDeviceManagerUnitTest, CloseInputPort001, TestSize.Level0)
{
    int64_t driverId = 505;
    uint32_t portIndex = 0;
    std::vector<DeviceInformation> driverDevs = {CreateDriverDeviceInfo(driverId, "USB MIDI")};
    EXPECT_CALL(*rawUsbDriver_, GetRegisteredDevices()).WillOnce(Return(driverDevs));
    manager_->UpdateDevices();
    int64_t globalId = manager_->GetDevices()[0].deviceId;

    EXPECT_CALL(*rawUsbDriver_, CloseInputPort(driverId, portIndex)).WillOnce(Return(MIDI_STATUS_OK));

    int32_t ret = manager_->CloseInputPort(globalId, portIndex);
    EXPECT_EQ(ret, MIDI_STATUS_OK);
}

/**
 * @tc.name: DeviceRemoval001
 * @tc.desc: Verify devices are removed from list when driver no longer reports them
 * @tc.type: FUNC
 */
HWTEST_F(MidiDeviceManagerUnitTest, DeviceRemoval001, TestSize.Level0)
{
    int64_t driverId = 606;
    EXPECT_CALL(*rawUsbDriver_, GetRegisteredDevices())
        .WillOnce(Return(std::vector<DeviceInformation>{CreateDriverDeviceInfo(driverId, "To Remove")}));
    manager_->UpdateDevices();
    ASSERT_EQ(manager_->GetDevices().size(), 1);
    int64_t oldGlobalId = manager_->GetDevices()[0].deviceId;

    EXPECT_CALL(*rawUsbDriver_, GetRegisteredDevices()).WillOnce(Return(std::vector<DeviceInformation>{}));

    manager_->UpdateDevices();

    auto currentDevices = manager_->GetDevices();
    EXPECT_TRUE(currentDevices.empty());

    EXPECT_FALSE(manager_->HasDriverMappingForTest(driverId));

    EXPECT_CALL(*rawUsbDriver_, OpenDevice(_)).Times(0);
    EXPECT_NE(manager_->OpenDevice(oldGlobalId), MIDI_STATUS_OK);
}

/**
 * @tc.name: MultiDriver001
 * @tc.desc: Test handling multiple drivers (e.g. USB and BLE)
 * @tc.type: FUNC
 */
HWTEST_F(MidiDeviceManagerUnitTest, MultiDriver001, TestSize.Level0)
{
    auto mockBleDriver = std::make_unique<MockMidiDeviceDriver>();
    MockMidiDeviceDriver *rawBleDriver = mockBleDriver.get();
    manager_->InjectDriverForTest(DeviceType::DEVICE_TYPE_BLE, std::move(mockBleDriver));

    int64_t usbDriverId = 10;
    int64_t bleDriverId = 20;

    EXPECT_CALL(*rawUsbDriver_, GetRegisteredDevices())
        .WillOnce(Return(std::vector<DeviceInformation>{CreateDriverDeviceInfo(usbDriverId, "USB Piano")}));

    DeviceInformation bleDev = CreateDriverDeviceInfo(bleDriverId, "BLE Guitar");
    bleDev.deviceType = DeviceType::DEVICE_TYPE_BLE;
    EXPECT_CALL(*rawBleDriver, GetRegisteredDevices()).WillOnce(Return(std::vector<DeviceInformation>{bleDev}));

    manager_->UpdateDevices();
    auto allDevices = manager_->GetDevices();

    EXPECT_EQ(allDevices.size(), 2);

    bool foundUsb = false;
    bool foundBle = false;
    for (auto &d : allDevices) {
        if (d.deviceType == DeviceType::DEVICE_TYPE_USB && d.productId == "USB Piano")
            foundUsb = true;
        if (d.deviceType == DeviceType::DEVICE_TYPE_BLE && d.productId == "BLE Guitar")
            foundBle = true;
    }
    EXPECT_TRUE(foundUsb);
    EXPECT_TRUE(foundBle);
}