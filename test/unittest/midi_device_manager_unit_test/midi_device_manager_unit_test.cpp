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
#include "midi_device_ble.h"
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
        manager_ = std::make_shared<MidiDeviceManager>();
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
        info.midiDeviceInfo.driverDeviceId = driverId;
        info.midiDeviceInfo.deviceType = DeviceType::DEVICE_TYPE_USB;
        info.midiDeviceInfo.deviceName = name;
        info.midiDeviceInfo.productId = 0x1234;
        info.midiDeviceInfo.vendorId = 0x5678;
        info.midiDeviceInfo.transportProtocol = TransportProtocol::PROTOCOL_1_0;
        return info;
    }

protected:
    std::shared_ptr<MidiDeviceManager> manager_;
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
    std::string deviceName = "Test Piano";
    std::vector<DeviceInformation> driverDevs = {CreateDriverDeviceInfo(driverId, deviceName)};

    EXPECT_CALL(*rawUsbDriver_, GetRegisteredDevices()).WillOnce(Return(driverDevs));

    manager_->UpdateDevices();

    auto devices = manager_->GetDevices();
    ASSERT_EQ(devices.size(), 1);

    EXPECT_EQ(devices[0].midiDeviceInfo.deviceName, deviceName);
    EXPECT_EQ(devices[0].midiDeviceInfo.driverDeviceId, driverId);
    EXPECT_NE(devices[0].midiDeviceInfo.deviceId, 0);

    EXPECT_TRUE(manager_->HasDriverMappingForTest(driverId, DeviceType::DEVICE_TYPE_USB));
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

    int64_t globalId = manager_->GetDevices()[0].midiDeviceInfo.deviceId;

    EXPECT_CALL(*rawUsbDriver_, OpenDevice(driverId)).WillOnce(Return(OH_MIDI_STATUS_OK));

    int32_t ret = manager_->OpenDevice(globalId);
    EXPECT_EQ(ret, OH_MIDI_STATUS_OK);
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
    EXPECT_NE(ret, OH_MIDI_STATUS_OK);
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
    int64_t globalId = manager_->GetDevices()[0].midiDeviceInfo.deviceId;

    EXPECT_CALL(*rawUsbDriver_, CloseDevice(driverId)).WillOnce(Return(OH_MIDI_STATUS_OK));

    int32_t ret = manager_->CloseDevice(globalId);
    EXPECT_EQ(ret, OH_MIDI_STATUS_OK);
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
    EXPECT_CALL(*rawUsbDriver_, GetRegisteredDevices).WillOnce(Return(driverDevs));
    manager_->UpdateDevices();
    int64_t globalId = manager_->GetDevices()[0].midiDeviceInfo.deviceId;

    EXPECT_CALL(*rawUsbDriver_, OpenInputPort(driverId, portIndex, _)).WillOnce(Return(OH_MIDI_STATUS_OK));
    std::shared_ptr<DeviceConnectionForInput> inputConnection = nullptr;
    int32_t ret = manager_->OpenInputPort(inputConnection, globalId, portIndex);
    EXPECT_EQ(ret, OH_MIDI_STATUS_OK);
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
    EXPECT_CALL(*rawUsbDriver_, GetRegisteredDevices).WillOnce(Return(driverDevs));
    manager_->UpdateDevices();
    int64_t globalId = manager_->GetDevices()[0].midiDeviceInfo.deviceId;

    EXPECT_CALL(*rawUsbDriver_, CloseInputPort(driverId, portIndex)).WillOnce(Return(OH_MIDI_STATUS_OK));

    int32_t ret = manager_->CloseInputPort(globalId, portIndex);
    EXPECT_EQ(ret, OH_MIDI_STATUS_OK);
}

/**
 * @tc.name: DeviceRemoval001
 * @tc.desc: Verify devices are removed from list when driver no longer reports them
 * @tc.type: FUNC
 */
HWTEST_F(MidiDeviceManagerUnitTest, DeviceRemoval001, TestSize.Level0)
{
    int64_t driverId = 606;
    EXPECT_CALL(*rawUsbDriver_, GetRegisteredDevices)
        .WillOnce(Return(std::vector<DeviceInformation>{CreateDriverDeviceInfo(driverId, "To Remove")}));
    manager_->UpdateDevices();
    ASSERT_EQ(manager_->GetDevices().size(), 1);
    int64_t oldGlobalId = manager_->GetDevices()[0].midiDeviceInfo.deviceId;

    EXPECT_CALL(*rawUsbDriver_, GetRegisteredDevices).WillOnce(Return(std::vector<DeviceInformation>{}));

    manager_->UpdateDevices();

    auto currentDevices = manager_->GetDevices();
    EXPECT_TRUE(currentDevices.empty());

    EXPECT_FALSE(manager_->HasDriverMappingForTest(driverId, DeviceType::DEVICE_TYPE_USB));

    EXPECT_CALL(*rawUsbDriver_, OpenDevice(_)).Times(0);
    EXPECT_NE(manager_->OpenDevice(oldGlobalId), OH_MIDI_STATUS_OK);
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

    EXPECT_CALL(*rawUsbDriver_, GetRegisteredDevices)
        .WillOnce(Return(std::vector<DeviceInformation>{CreateDriverDeviceInfo(usbDriverId, "USB Piano")}));

    DeviceInformation bleDev = CreateDriverDeviceInfo(bleDriverId, "BLE Guitar");
    bleDev.midiDeviceInfo.deviceType = DeviceType::DEVICE_TYPE_BLE;
    EXPECT_CALL(*rawBleDriver, GetRegisteredDevices).WillOnce(Return(std::vector<DeviceInformation>{bleDev}));

    manager_->UpdateDevices();
    auto allDevices = manager_->GetDevices();

    EXPECT_EQ(allDevices.size(), 2);

    bool foundUsb = false;
    bool foundBle = false;
    for (auto &d : allDevices) {
        if (d.midiDeviceInfo.deviceType == DeviceType::DEVICE_TYPE_USB && d.midiDeviceInfo.deviceName == "USB Piano")
            foundUsb = true;
        if (d.midiDeviceInfo.deviceType == DeviceType::DEVICE_TYPE_BLE && d.midiDeviceInfo.deviceName == "BLE Guitar")
            foundBle = true;
    }
    EXPECT_TRUE(foundUsb);
    EXPECT_TRUE(foundBle);
}

/**
 * @tc.name: UsbBleDriverIdCollision001
 * @tc.desc: Verify that USB and BLE devices with the same numeric driverDeviceId receive distinct MIDI IDs
 * @tc.type: FUNC
 */
HWTEST_F(MidiDeviceManagerUnitTest, UsbBleDriverIdCollision001, TestSize.Level0)
{
    auto mockBleDriver = std::make_unique<MockMidiDeviceDriver>();
    MockMidiDeviceDriver *rawBleDriver = mockBleDriver.get();
    manager_->InjectDriverForTest(DeviceType::DEVICE_TYPE_BLE, std::move(mockBleDriver));

    // Both USB and BLE use the same numeric driverDeviceId (42)
    int64_t collisionDriverId = 42;

    EXPECT_CALL(*rawUsbDriver_, GetRegisteredDevices)
        .WillOnce(Return(std::vector<DeviceInformation>{CreateDriverDeviceInfo(collisionDriverId, "USB Piano")}));

    DeviceInformation bleDev = CreateDriverDeviceInfo(collisionDriverId, "BLE Guitar");
    bleDev.midiDeviceInfo.deviceType = DeviceType::DEVICE_TYPE_BLE;
    EXPECT_CALL(*rawBleDriver, GetRegisteredDevices)
        .WillOnce(Return(std::vector<DeviceInformation>{bleDev}));

    manager_->UpdateDevices();
    auto allDevices = manager_->GetDevices();

    ASSERT_EQ(allDevices.size(), 2);

    // Find USB and BLE devices
    int64_t usbMidiId = 0;
    int64_t bleMidiId = 0;
    for (const auto &d : allDevices) {
        if (d.midiDeviceInfo.deviceType == DeviceType::DEVICE_TYPE_USB) {
            usbMidiId = d.midiDeviceInfo.deviceId;
        } else if (d.midiDeviceInfo.deviceType == DeviceType::DEVICE_TYPE_BLE) {
            bleMidiId = d.midiDeviceInfo.deviceId;
        }
    }

    // Both must have valid IDs and they must be different
    EXPECT_NE(usbMidiId, 0);
    EXPECT_NE(bleMidiId, 0);
    EXPECT_NE(usbMidiId, bleMidiId)
        << "USB and BLE with same driverDeviceId must map to distinct MIDI IDs";

    // Verify mapping exists for both types independently
    EXPECT_TRUE(manager_->HasDriverMappingForTest(collisionDriverId, DeviceType::DEVICE_TYPE_USB));
    EXPECT_TRUE(manager_->HasDriverMappingForTest(collisionDriverId, DeviceType::DEVICE_TYPE_BLE));
}

/**
 * @tc.name: DeviceLookupAndMappingReuse001
 * @tc.desc: Verify port lookup and stable MIDI ID mapping across repeated discovery
 * @tc.type: FUNC
 */
HWTEST_F(MidiDeviceManagerUnitTest, DeviceLookupAndMappingReuse001, TestSize.Level0)
{
    constexpr int64_t driverId = 707;
    DeviceInformation device = CreateDriverDeviceInfo(driverId, "Reusable Device");
    MidiPortInfo port;
    port.portId = 3;
    port.direction = PortDirection::PORT_DIRECTION_OUTPUT;
    port.name = "Output";
    device.portInfos.push_back(port);

    EXPECT_CALL(*rawUsbDriver_, GetRegisteredDevices())
        .Times(2)
        .WillRepeatedly(Return(std::vector<DeviceInformation>{device}));

    manager_->UpdateDevices();
    const int64_t firstId = manager_->GetDevices().at(0).midiDeviceInfo.deviceId;
    manager_->UpdateDevices();
    const int64_t secondId = manager_->GetDevices().at(0).midiDeviceInfo.deviceId;
    EXPECT_EQ(firstId, secondId);

    std::vector<MidiPortInfo> ports;
    EXPECT_EQ(manager_->GetDevicePorts(firstId, ports), OH_MIDI_STATUS_OK);
    ASSERT_EQ(ports.size(), 1);
    EXPECT_EQ(ports[0].name, "Output");

    ports.clear();
    EXPECT_EQ(manager_->GetDevicePorts(-1, ports), OH_MIDI_STATUS_GENERIC_INVALID_ARGUMENT);
    EXPECT_TRUE(ports.empty());
}

/**
 * @tc.name: DriverFailuresAndOutputOperations001
 * @tc.desc: Verify driver errors and output-port operations are propagated
 * @tc.type: FUNC
 */
HWTEST_F(MidiDeviceManagerUnitTest, DriverFailuresAndOutputOperations001, TestSize.Level0)
{
    constexpr int64_t driverId = 808;
    constexpr uint32_t portIndex = 2;
    EXPECT_CALL(*rawUsbDriver_, GetRegisteredDevices())
        .WillOnce(Return(std::vector<DeviceInformation>{CreateDriverDeviceInfo(driverId, "Failure Device")}));
    manager_->UpdateDevices();
    const int64_t deviceId = manager_->GetDevices().at(0).midiDeviceInfo.deviceId;

    EXPECT_CALL(*rawUsbDriver_, OpenDevice(driverId)).WillOnce(Return(OH_MIDI_STATUS_SYSTEM_ERROR));
    EXPECT_EQ(manager_->OpenDevice(deviceId), OH_MIDI_STATUS_SYSTEM_ERROR);

    std::shared_ptr<DeviceConnectionForInput> inputConnection;
    EXPECT_CALL(*rawUsbDriver_, OpenInputPort(driverId, portIndex, _))
        .WillOnce(Return(OH_MIDI_STATUS_SYSTEM_ERROR));
    EXPECT_EQ(manager_->OpenInputPort(inputConnection, deviceId, portIndex), OH_MIDI_STATUS_INVALID_PORT);
    EXPECT_NE(inputConnection, nullptr);

    std::shared_ptr<DeviceConnectionForOutput> outputConnection;
    EXPECT_CALL(*rawUsbDriver_, OpenOutputPort(driverId, portIndex))
        .WillOnce(Return(OH_MIDI_STATUS_SYSTEM_ERROR));
    EXPECT_EQ(manager_->OpenOutputPort(outputConnection, deviceId, portIndex), OH_MIDI_STATUS_INVALID_PORT);
    EXPECT_NE(outputConnection, nullptr);

    EXPECT_CALL(*rawUsbDriver_, CloseOutputPort(driverId, portIndex)).WillOnce(Return(OH_MIDI_STATUS_OK));
    EXPECT_EQ(manager_->CloseOutputPort(deviceId, portIndex), OH_MIDI_STATUS_OK);

    EXPECT_CALL(*rawUsbDriver_, CloseDevice(driverId)).WillOnce(Return(OH_MIDI_STATUS_SYSTEM_ERROR));
    EXPECT_EQ(manager_->CloseDevice(deviceId), OH_MIDI_STATUS_SYSTEM_ERROR);

    EXPECT_EQ(manager_->OpenOutputPort(outputConnection, -1, portIndex), OH_MIDI_STATUS_SYSTEM_ERROR);
    EXPECT_EQ(manager_->CloseInputPort(-1, portIndex), OH_MIDI_STATUS_SYSTEM_ERROR);
    EXPECT_EQ(manager_->CloseOutputPort(-1, portIndex), OH_MIDI_STATUS_SYSTEM_ERROR);
    EXPECT_EQ(manager_->CloseDevice(-1), OH_MIDI_STATUS_SYSTEM_ERROR);
}

/**
 * @tc.name: MissingDriverOperations001
 * @tc.desc: Verify discovered devices fail safely after their driver becomes unavailable
 * @tc.type: FUNC
 */
HWTEST_F(MidiDeviceManagerUnitTest, MissingDriverOperations001, TestSize.Level0)
{
    auto mockBleDriver = std::make_unique<NiceMock<MockMidiDeviceDriver>>();
    auto *rawBleDriver = mockBleDriver.get();
    manager_->InjectDriverForTest(DeviceType::DEVICE_TYPE_BLE, std::move(mockBleDriver));

    DeviceInformation bleDevice = CreateDriverDeviceInfo(909, "BLE Device");
    bleDevice.midiDeviceInfo.deviceType = DeviceType::DEVICE_TYPE_BLE;
    EXPECT_CALL(*rawUsbDriver_, GetRegisteredDevices()).WillOnce(Return(std::vector<DeviceInformation>{}));
    EXPECT_CALL(*rawBleDriver, GetRegisteredDevices())
        .WillOnce(Return(std::vector<DeviceInformation>{bleDevice}));
    manager_->UpdateDevices();
    const int64_t deviceId = manager_->GetDevices().at(0).midiDeviceInfo.deviceId;

    manager_->InjectDriverForTest(DeviceType::DEVICE_TYPE_BLE, nullptr);
    std::shared_ptr<DeviceConnectionForInput> inputConnection;
    std::shared_ptr<DeviceConnectionForOutput> outputConnection;
    EXPECT_EQ(manager_->OpenDevice(deviceId), OH_MIDI_STATUS_SYSTEM_ERROR);
    EXPECT_EQ(manager_->OpenInputPort(inputConnection, deviceId, 0), OH_MIDI_STATUS_SYSTEM_ERROR);
    EXPECT_EQ(manager_->OpenOutputPort(outputConnection, deviceId, 0), OH_MIDI_STATUS_SYSTEM_ERROR);
    EXPECT_EQ(manager_->CloseInputPort(deviceId, 0), OH_MIDI_STATUS_SYSTEM_ERROR);
    EXPECT_EQ(manager_->CloseOutputPort(deviceId, 0), OH_MIDI_STATUS_SYSTEM_ERROR);
    EXPECT_EQ(manager_->CloseDevice(deviceId), OH_MIDI_STATUS_SYSTEM_ERROR);

    EXPECT_CALL(*rawUsbDriver_, GetRegisteredDevices()).WillOnce(Return(std::vector<DeviceInformation>{}));
    manager_->UpdateDevices();
    EXPECT_TRUE(manager_->GetDevices().empty());
}

/**
 * @tc.name: BleLifecycle001
 * @tc.desc: Verify BLE connect, reconnect, disconnect, and immediate driver failure paths
 * @tc.type: FUNC
 */
HWTEST_F(MidiDeviceManagerUnitTest, BleLifecycle001, TestSize.Level0)
{
    auto mockBleDriver = std::make_unique<NiceMock<MockMidiDeviceDriver>>();
    auto *rawBleDriver = mockBleDriver.get();
    manager_->InjectDriverForTest(DeviceType::DEVICE_TYPE_BLE, std::move(mockBleDriver));

    BleDriverCallback driverCallback;
    EXPECT_CALL(*rawBleDriver, OpenDevice(_, _))
        .WillOnce(DoAll(SaveArg<1>(&driverCallback), Return(OH_MIDI_STATUS_OK)));

    int successCount = 0;
    int failureCount = 0;
    int64_t callbackDeviceId = 0;
    auto callback = [&successCount, &failureCount, &callbackDeviceId](
        bool success, int64_t deviceId, const MidiDeviceInfo &) {
        success ? ++successCount : ++failureCount;
        callbackDeviceId = deviceId;
    };
    EXPECT_EQ(manager_->OpenBleDevice("11:22:33:44:55:66", callback), OH_MIDI_STATUS_OK);
    ASSERT_TRUE(static_cast<bool>(driverCallback));

    DeviceInformation bleDevice = CreateDriverDeviceInfo(1001, "BLE Keyboard");
    bleDevice.midiDeviceInfo.deviceType = DeviceType::DEVICE_TYPE_BLE;
    driverCallback(true, bleDevice);
    ASSERT_NE(callbackDeviceId, 0);
    EXPECT_EQ(successCount, 1);
    EXPECT_EQ(manager_->GetDevices().size(), 1);

    driverCallback(true, bleDevice);
    EXPECT_EQ(successCount, 2);
    EXPECT_EQ(manager_->GetDevices().size(), 1);

    driverCallback(false, bleDevice);
    EXPECT_EQ(failureCount, 0);
    EXPECT_TRUE(manager_->GetDevices().empty());

    driverCallback(false, bleDevice);
    EXPECT_EQ(failureCount, 1);
    EXPECT_EQ(callbackDeviceId, 0);

    manager_->InjectDriverForTest(DeviceType::DEVICE_TYPE_BLE, nullptr);
    EXPECT_EQ(manager_->OpenBleDevice("missing", callback), OH_MIDI_STATUS_SYSTEM_ERROR);

    auto failingDriver = std::make_unique<NiceMock<MockMidiDeviceDriver>>();
    auto *rawFailingDriver = failingDriver.get();
    manager_->InjectDriverForTest(DeviceType::DEVICE_TYPE_BLE, std::move(failingDriver));
    EXPECT_CALL(*rawFailingDriver, OpenDevice(_, _)).WillOnce(Return(OH_MIDI_STATUS_SYSTEM_ERROR));
    EXPECT_EQ(manager_->OpenBleDevice("failure", callback), OH_MIDI_STATUS_SYSTEM_ERROR);
}

/**
 * @tc.name: ConcreteBleDriverPortLifecycle001
 * @tc.desc: Verify concrete BLE driver device discovery and input/output port state transitions.
 * @tc.type: FUNC
 */
HWTEST_F(MidiDeviceManagerUnitTest, ConcreteBleDriverPortLifecycle001, TestSize.Level0)
{
    auto *driver = static_cast<BleMidiTransportDeviceDriver *>(
        manager_->drivers_.at(DeviceType::DEVICE_TYPE_BLE).get());
    ASSERT_NE(driver, nullptr);
    driver->devices_.clear();

    EXPECT_EQ(driver->OpenDevice(8001), -1);
    EXPECT_EQ(driver->OpenInputPort(8001, 0, nullptr), -1);
    EXPECT_EQ(driver->OpenInputPort(8001, 1, nullptr), -1);
    EXPECT_EQ(driver->CloseInputPort(8001, 0), -1);
    EXPECT_EQ(driver->CloseInputPort(8001, 1), -1);
    EXPECT_EQ(driver->OpenOutputPort(8001, 1), -1);
    EXPECT_EQ(driver->OpenOutputPort(8001, 0), -1);
    EXPECT_EQ(driver->CloseOutputPort(8001, 1), -1);
    EXPECT_EQ(driver->CloseOutputPort(8001, 0), -1);
    EXPECT_EQ(driver->CloseDevice(8001), -1);

    DeviceCtx ctx {};
    ctx.id = 8001;
    ctx.address = "11:22:33:44:55:66";
    ctx.deviceName = "BLE Keyboard";
    ctx.productId = 0x1234;
    ctx.vendorId = 0x5678;
    DeviceCtx skippedCtx {};
    skippedCtx.id = 8000;
    driver->devices_.emplace(skippedCtx.id, skippedCtx);
    driver->devices_.emplace(ctx.id, ctx);

    EXPECT_TRUE(driver->GetRegisteredDevices().empty());
    driver->devices_.at(ctx.id).connected = true;
    auto devices = driver->GetRegisteredDevices();
    ASSERT_EQ(devices.size(), 1);
    EXPECT_EQ(devices.front().portInfos.size(), 2);
    EXPECT_EQ(devices.front().midiDeviceInfo.address, ctx.address);

    uint32_t callbackCount = 0;
    auto inputCallback = [&callbackCount](std::vector<MidiEventInner> &) {
        ++callbackCount;
    };
    EXPECT_EQ(driver->OpenInputPort(ctx.id, 1, inputCallback), 0);
    EXPECT_EQ(driver->OpenInputPort(ctx.id, 1, inputCallback), -1);
    EXPECT_EQ(driver->CloseInputPort(ctx.id, 1), 0);
    EXPECT_EQ(driver->CloseInputPort(ctx.id, 1), -1);

    EXPECT_EQ(driver->OpenOutputPort(ctx.id, 0), 0);
    EXPECT_EQ(driver->OpenOutputPort(ctx.id, 0), -1);
    EXPECT_EQ(driver->CloseOutputPort(ctx.id, 0), 0);
    EXPECT_EQ(driver->CloseOutputPort(ctx.id, 0), -1);

    EXPECT_EQ(driver->OpenDevice(ctx.address, nullptr), OH_MIDI_STATUS_DEVICE_ALREADY_OPEN);
    EXPECT_EQ(callbackCount, 0);
    EXPECT_EQ(driver->CloseDevice(ctx.id), 0);
    EXPECT_EQ(driver->devices_.count(ctx.id), 0);
}

/**
 * @tc.name: ConcreteBleDriverUmpInput001
 * @tc.desc: Verify concrete BLE output validation, empty-event, null-data, and conversion paths.
 * @tc.type: FUNC
 */
HWTEST_F(MidiDeviceManagerUnitTest, ConcreteBleDriverUmpInput001, TestSize.Level0)
{
    auto *driver = static_cast<BleMidiTransportDeviceDriver *>(
        manager_->drivers_.at(DeviceType::DEVICE_TYPE_BLE).get());
    ASSERT_NE(driver, nullptr);
    driver->devices_.clear();

    std::vector<MidiEventInner> events;
    EXPECT_EQ(driver->HandleUmpInput(8101, 1, events), -1);
    EXPECT_EQ(driver->HandleUmpInput(8101, 0, events), -1);

    DeviceCtx ctx {};
    ctx.id = 8101;
    driver->devices_.emplace(ctx.id, ctx);
    EXPECT_EQ(driver->HandleUmpInput(ctx.id, 0, events), -1);

    auto &stored = driver->devices_.at(ctx.id);
    stored.outputOpen = true;
    EXPECT_EQ(driver->HandleUmpInput(ctx.id, 0, events), -1);
    stored.connected = true;
    EXPECT_EQ(driver->HandleUmpInput(ctx.id, 0, events), -1);
    stored.serviceReady = true;
    EXPECT_EQ(driver->HandleUmpInput(ctx.id, 0, events), -1);
    stored.outProcessor = std::make_shared<UmpProcessor>();
    EXPECT_EQ(driver->HandleUmpInput(ctx.id, 0, events), 0);

    uint32_t umpWord = 0x20903C40;
    events.push_back(MidiEventInner {.timestamp = 0, .length = 1, .data = nullptr});
    events.push_back(MidiEventInner {.timestamp = 1, .length = 0, .data = &umpWord});
    events.push_back(MidiEventInner {.timestamp = 1000000, .length = 1, .data = &umpWord});
    EXPECT_EQ(driver->HandleUmpInput(ctx.id, 0, events), 0);
}

/**
 * @tc.name: ConcreteBleDriverGattFailureCallbacks001
 * @tc.desc: Verify BLE GATT callback validation and failure cleanup paths.
 * @tc.type: FUNC
 */
HWTEST_F(MidiDeviceManagerUnitTest, ConcreteBleDriverGattFailureCallbacks001, TestSize.Level0)
{
    auto *driver = static_cast<BleMidiTransportDeviceDriver *>(
        manager_->drivers_.at(DeviceType::DEVICE_TYPE_BLE).get());
    ASSERT_NE(driver, nullptr);
    driver->devices_.clear();

    driver->gattCallbacks_.writeCharacteristicCb(8201, nullptr, 0);
    driver->gattCallbacks_.writeCharacteristicCb(8201, nullptr, -1);
    driver->gattCallbacks_.ConnectionStateCb(8201, OHOS_STATE_DISCONNECTED, 0);
    driver->gattCallbacks_.searchServiceCompleteCb(8201, 0);
    driver->gattCallbacks_.registerNotificationCb(8201, 0);
    driver->gattCallbacks_.notificationCb(8201, nullptr, 0);
    BtGattReadData statusFailureData {};
    driver->gattCallbacks_.notificationCb(8201, &statusFailureData, -1);

    DeviceCtx disconnectCtx {};
    disconnectCtx.id = 8202;
    uint32_t disconnectCallbacks = 0;
    disconnectCtx.deviceCallback = [&disconnectCallbacks](bool success, DeviceInformation) {
        EXPECT_FALSE(success);
        ++disconnectCallbacks;
    };
    driver->devices_.emplace(disconnectCtx.id, disconnectCtx);
    driver->gattCallbacks_.ConnectionStateCb(disconnectCtx.id, OHOS_STATE_DISCONNECTED, 0);
    EXPECT_EQ(disconnectCallbacks, 1);
    EXPECT_EQ(driver->devices_.count(disconnectCtx.id), 0);

    DeviceCtx serviceFailureCtx {};
    serviceFailureCtx.id = 8203;
    uint32_t serviceFailureCallbacks = 0;
    serviceFailureCtx.deviceCallback = [&serviceFailureCallbacks](bool success, DeviceInformation) {
        EXPECT_FALSE(success);
        ++serviceFailureCallbacks;
    };
    driver->devices_.emplace(serviceFailureCtx.id, serviceFailureCtx);
    driver->gattCallbacks_.searchServiceCompleteCb(serviceFailureCtx.id, -1);
    EXPECT_EQ(serviceFailureCallbacks, 1);
    EXPECT_EQ(driver->devices_.count(serviceFailureCtx.id), 0);

    DeviceCtx notifyFailureCtx {};
    notifyFailureCtx.id = 8204;
    uint32_t notifyFailureCallbacks = 0;
    notifyFailureCtx.deviceCallback = [&notifyFailureCallbacks](bool success, DeviceInformation) {
        EXPECT_FALSE(success);
        ++notifyFailureCallbacks;
    };
    driver->devices_.emplace(notifyFailureCtx.id, notifyFailureCtx);
    driver->gattCallbacks_.registerNotificationCb(notifyFailureCtx.id, -1);
    EXPECT_EQ(notifyFailureCallbacks, 1);
    EXPECT_EQ(driver->devices_.count(notifyFailureCtx.id), 0);
}

/**
 * @tc.name: ConcreteBleDriverGattNotificationValidation001
 * @tc.desc: Verify BLE GATT notification registration and payload validation paths.
 * @tc.type: FUNC
 */
HWTEST_F(MidiDeviceManagerUnitTest, ConcreteBleDriverGattNotificationValidation001, TestSize.Level0)
{
    auto *driver = static_cast<BleMidiTransportDeviceDriver *>(
        manager_->drivers_.at(DeviceType::DEVICE_TYPE_BLE).get());
    ASSERT_NE(driver, nullptr);
    driver->devices_.clear();
    DeviceCtx notifySuccessCtx {};
    notifySuccessCtx.id = 8206;
    notifySuccessCtx.address = "11:22:33:44:55:66";
    uint32_t notifySuccessCallbacks = 0;
    notifySuccessCtx.deviceCallback = [&notifySuccessCallbacks](bool success, DeviceInformation) {
        EXPECT_TRUE(success);
        ++notifySuccessCallbacks;
    };
    driver->devices_.emplace(notifySuccessCtx.id, notifySuccessCtx);
    driver->gattCallbacks_.registerNotificationCb(notifySuccessCtx.id, 0);
    EXPECT_EQ(notifySuccessCallbacks, 1);
    EXPECT_TRUE(driver->devices_.at(notifySuccessCtx.id).notifyEnabled);

    unsigned char invalidPayload[] = {0x80, 0x90, 0x3C, 0x40};
    char badService[] = "bad-service";
    char midiService[] = "03b80e5a-ede8-4b33-a751-6ce34ec4c700";
    char midiCharacteristic[] = "7772e5db-3868-4112-a1a9-f2669d106bf3";
    BtGattReadData readData {};
    readData.attribute.characteristic.serviceUuid.uuid = badService;
    readData.attribute.characteristic.serviceUuid.uuidLen = sizeof(badService) - 1;
    readData.attribute.characteristic.characteristicUuid.uuid = midiCharacteristic;
    readData.attribute.characteristic.characteristicUuid.uuidLen = sizeof(midiCharacteristic) - 1;
    readData.data = invalidPayload;
    readData.dataLen = sizeof(invalidPayload);
    driver->gattCallbacks_.notificationCb(8205, &readData, 0);

    readData.attribute.characteristic.serviceUuid.uuid = midiService;
    readData.attribute.characteristic.serviceUuid.uuidLen = sizeof(midiService) - 1;
    readData.attribute.characteristic.characteristicUuid.uuid = nullptr;
    driver->gattCallbacks_.notificationCb(8205, &readData, 0);
    readData.attribute.characteristic.characteristicUuid.uuid = midiCharacteristic;
    readData.attribute.characteristic.characteristicUuid.uuidLen = sizeof(midiCharacteristic) - 2;
    driver->gattCallbacks_.notificationCb(8205, &readData, 0);
    readData.attribute.characteristic.characteristicUuid.uuidLen = sizeof(midiCharacteristic) - 1;
    readData.data = nullptr;
    readData.dataLen = 0;
    driver->gattCallbacks_.notificationCb(8205, &readData, 0);
    readData.data = invalidPayload;
    readData.dataLen = 1;
    driver->gattCallbacks_.notificationCb(8205, &readData, 0);
    readData.dataLen = 513;
    driver->gattCallbacks_.notificationCb(8205, &readData, 0);

    readData.data = invalidPayload;
    readData.dataLen = sizeof(invalidPayload);
    driver->gattCallbacks_.notificationCb(8205, &readData, 0);
}

/**
 * @tc.name: ConcreteBleDriverGattNotificationDelivery001
 * @tc.desc: Verify BLE GATT notification delivery with missing and registered callbacks.
 * @tc.type: FUNC
 */
HWTEST_F(MidiDeviceManagerUnitTest, ConcreteBleDriverGattNotificationDelivery001, TestSize.Level0)
{
    auto *driver = static_cast<BleMidiTransportDeviceDriver *>(
        manager_->drivers_.at(DeviceType::DEVICE_TYPE_BLE).get());
    ASSERT_NE(driver, nullptr);
    driver->devices_.clear();
    unsigned char payload[] = {0x80, 0x90, 0x3C, 0x40};
    char midiService[] = "03b80e5a-ede8-4b33-a751-6ce34ec4c700";
    char midiCharacteristic[] = "7772e5db-3868-4112-a1a9-f2669d106bf3";
    BtGattReadData readData {};
    readData.attribute.characteristic.serviceUuid.uuid = midiService;
    readData.attribute.characteristic.serviceUuid.uuidLen = sizeof(midiService) - 1;
    readData.attribute.characteristic.characteristicUuid.uuid = midiCharacteristic;
    readData.attribute.characteristic.characteristicUuid.uuidLen = sizeof(midiCharacteristic) - 1;
    readData.data = payload;
    readData.dataLen = sizeof(payload);

    DeviceCtx callbackMissingCtx {};
    callbackMissingCtx.id = 8205;
    callbackMissingCtx.inputOpen = true;
    callbackMissingCtx.notifyEnabled = true;
    callbackMissingCtx.processor = std::make_shared<UmpProcessor>();
    driver->devices_.emplace(callbackMissingCtx.id, callbackMissingCtx);
    driver->gattCallbacks_.notificationCb(callbackMissingCtx.id, &readData, 0);
    driver->devices_.erase(callbackMissingCtx.id);

    DeviceCtx notificationCtx {};
    notificationCtx.id = 8205;
    notificationCtx.inputOpen = true;
    notificationCtx.notifyEnabled = true;
    notificationCtx.processor = std::make_shared<UmpProcessor>();
    uint32_t inputCallbacks = 0;
    notificationCtx.inputCallback = [&inputCallbacks](std::vector<MidiEventInner> &inputEvents) {
        EXPECT_FALSE(inputEvents.empty());
        ++inputCallbacks;
    };
    driver->devices_.emplace(notificationCtx.id, notificationCtx);
    driver->gattCallbacks_.notificationCb(notificationCtx.id, &readData, 0);
    EXPECT_EQ(inputCallbacks, 1);
}

/**
 * @tc.name: ConcreteBleDriverOpenAndDestroyedCallbackBranches001
 * @tc.desc: Verify invalid-address opening and callback guards after the singleton instance is cleared.
 * @tc.type: FUNC
 */
HWTEST_F(MidiDeviceManagerUnitTest, ConcreteBleDriverOpenAndDestroyedCallbackBranches001, TestSize.Level0)
{
    auto *driver = static_cast<BleMidiTransportDeviceDriver *>(
        manager_->drivers_.at(DeviceType::DEVICE_TYPE_BLE).get());
    ASSERT_NE(driver, nullptr);
    driver->devices_.clear();

    EXPECT_NE(driver->OpenDevice("invalid", nullptr), 0);
    EXPECT_NE(driver->OpenDevice("11:22:33:44:55:GG", nullptr), 0);

    {
        BleMidiTransportDeviceDriver duplicateInstance;
    }
    driver->gattCallbacks_.ConnectionStateCb(8301, OHOS_STATE_DISCONNECTED, 0);
    driver->gattCallbacks_.searchServiceCompleteCb(8301, 0);
    driver->gattCallbacks_.registerNotificationCb(8301, 0);
    driver->gattCallbacks_.notificationCb(8301, nullptr, 0);
}

/**
 * @tc.name: ManagerCallbackAndSuccessBranches001
 * @tc.desc: Verify expired callbacks and the remaining successful port paths.
 * @tc.type: FUNC
 */
HWTEST_F(MidiDeviceManagerUnitTest, ManagerCallbackAndSuccessBranches001, TestSize.Level0)
{
    constexpr int64_t driverId = 8401;
    constexpr uint32_t portIndex = 3;
    EXPECT_CALL(*rawUsbDriver_, GetRegisteredDevices())
        .WillOnce(Return(std::vector<DeviceInformation>{CreateDriverDeviceInfo(driverId, "Callback Device")}));
    manager_->UpdateDevices();
    const int64_t deviceId = manager_->GetDevices().at(0).midiDeviceInfo.deviceId;

    UmpInputCallback inputCallback;
    EXPECT_CALL(*rawUsbDriver_, OpenInputPort(driverId, portIndex, _))
        .WillOnce(DoAll(SaveArg<2>(&inputCallback), Return(OH_MIDI_STATUS_OK)));
    std::shared_ptr<DeviceConnectionForInput> inputConnection;
    EXPECT_EQ(manager_->OpenInputPort(inputConnection, deviceId, portIndex), OH_MIDI_STATUS_OK);
    ASSERT_TRUE(static_cast<bool>(inputCallback));
    inputConnection.reset();
    std::vector<MidiEventInner> events;
    inputCallback(events);

    EXPECT_CALL(*rawUsbDriver_, OpenOutputPort(driverId, portIndex)).WillOnce(Return(OH_MIDI_STATUS_OK));
    std::shared_ptr<DeviceConnectionForOutput> outputConnection;
    EXPECT_EQ(manager_->OpenOutputPort(outputConnection, deviceId, portIndex), OH_MIDI_STATUS_OK);
    EXPECT_NE(outputConnection, nullptr);

    EXPECT_EQ(manager_->GetDriverForDeviceType(static_cast<DeviceType>(99)), nullptr);

    DeviceInformation bleDevice = CreateDriverDeviceInfo(8402, "Direct BLE");
    bleDevice.midiDeviceInfo.deviceType = DeviceType::DEVICE_TYPE_BLE;
    manager_->HandleBleConnect(bleDevice, nullptr);
    manager_->HandleBleConnect(bleDevice, nullptr);
    manager_->HandleBleDisconnect(CreateDriverDeviceInfo(9999, "Missing BLE"), nullptr);
}

/**
 * @tc.name: ExpiredBleManagerCallback001
 * @tc.desc: Verify an asynchronous BLE callback is ignored after manager destruction.
 * @tc.type: FUNC
 */
HWTEST_F(MidiDeviceManagerUnitTest, ExpiredBleManagerCallback001, TestSize.Level0)
{
    auto localManager = std::make_shared<MidiDeviceManager>();
    auto bleDriver = std::make_unique<NiceMock<MockMidiDeviceDriver>>();
    auto *rawBleDriver = bleDriver.get();
    localManager->InjectDriverForTest(DeviceType::DEVICE_TYPE_BLE, std::move(bleDriver));

    BleDriverCallback staleCallback;
    EXPECT_CALL(*rawBleDriver, OpenDevice("stale", _))
        .WillOnce(DoAll(SaveArg<1>(&staleCallback), Return(OH_MIDI_STATUS_OK)));
    EXPECT_EQ(localManager->OpenBleDevice("stale", nullptr), OH_MIDI_STATUS_OK);
    ASSERT_TRUE(static_cast<bool>(staleCallback));

    localManager.reset();
    DeviceInformation device = CreateDriverDeviceInfo(8501, "Expired BLE");
    device.midiDeviceInfo.deviceType = DeviceType::DEVICE_TYPE_BLE;
    staleCallback(true, device);
}

/**
 * @tc.name: EventSubscriberValidation001
 * @tc.desc: Verify unknown and recognized events with missing payload are rejected.
 * @tc.type: FUNC
 */
HWTEST_F(MidiDeviceManagerUnitTest, EventSubscriberValidation001, TestSize.Level0)
{
    EventFwk::MatchingSkills skills;
    EventFwk::CommonEventSubscribeInfo subscribeInfo(skills);
    uint32_t callbackCount = 0;
    EventSubscriber subscriber(subscribeInfo, [&callbackCount]() {
        ++callbackCount;
    });

    EventFwk::CommonEventData unknownEvent;
    subscriber.OnReceiveEvent(unknownEvent);

    AAFwk::Want want;
    want.SetAction(EventFwk::CommonEventSupport::COMMON_EVENT_USB_DEVICE_ATTACHED);
    EventFwk::CommonEventData emptyEvent(want);
    subscriber.OnReceiveEvent(emptyEvent);

    const std::string invalidJson = "{";
    EventFwk::CommonEventData invalidJsonEvent(want, 0, invalidJson);
    subscriber.OnReceiveEvent(invalidJsonEvent);

    const std::string emptyDeviceJson = R"({"configs":[]})";
    EventFwk::CommonEventData emptyDeviceEvent(want, 0, emptyDeviceJson);
    subscriber.OnReceiveEvent(emptyDeviceEvent);

    const std::string nonMidiJson =
        R"({"configs":[{"interfaces":[{"clazz":2,"subClass":0,"endpoints":[]}]}]})";
    EventFwk::CommonEventData nonMidiEvent(want, 0, nonMidiJson);
    subscriber.OnReceiveEvent(nonMidiEvent);

    const std::string midiJson =
        R"({"configs":[{"interfaces":[{"clazz":1,"subClass":3,"endpoints":[]}]}]})";
    EventFwk::CommonEventData midiEvent(want, 0, midiJson);
    subscriber.OnReceiveEvent(midiEvent);
    EXPECT_EQ(callbackCount, 1);

    EventSubscriber emptySubscriber(subscribeInfo, nullptr);
    emptySubscriber.OnReceiveEvent(midiEvent);

    want.SetAction(EventFwk::CommonEventSupport::COMMON_EVENT_USB_DEVICE_DETACHED);
    EventFwk::CommonEventData detachEvent(want, 0, midiJson);
    subscriber.OnReceiveEvent(detachEvent);
    EXPECT_EQ(callbackCount, 2);
}
