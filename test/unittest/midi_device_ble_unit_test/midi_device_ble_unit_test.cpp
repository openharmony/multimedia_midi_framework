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
#include "midi_device_ble.h"
#include "midi_test_common.h"
#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace OHOS;
using namespace MIDI;
using namespace testing;
using namespace testing::ext;

class MidiDeviceBleUnitTest : public testing::Test {
public:
    void SetUp() override
    {
        driver_ = std::make_shared<BleMidiTransportDeviceDriver>();
    }

    void TearDown() override
    {
        driver_->devices_.clear();
        driver_.reset();
    }

    // Inject a device context keyed by clientId; ctx.id matches the key so port APIs can find it.
    DeviceCtx &InjectDevice(int32_t clientId, const std::string &address, bool connected,
        bool inputOpen = false, bool outputOpen = false)
    {
        DeviceCtx ctx;
        ctx.id = clientId;
        ctx.address = address;
        ctx.connected = connected;
        ctx.inputOpen = inputOpen;
        ctx.outputOpen = outputOpen;
        driver_->devices_[clientId] = ctx;
        return driver_->devices_[clientId];
    }

protected:
    std::shared_ptr<BleMidiTransportDeviceDriver> driver_;
};

/**
 * @tc.name: GetRegisteredDevicesBranches001
 * @tc.desc: Verify connected filtering and field mapping in GetRegisteredDevices.
 * @tc.type: FUNC
 */
HWTEST_F(MidiDeviceBleUnitTest, GetRegisteredDevicesBranches001, TestSize.Level0)
{
    EXPECT_TRUE(driver_->GetRegisteredDevices().empty());

    DeviceCtx &offline = InjectDevice(101, "00:11:22:33:44:01", false);
    offline.deviceName = "Offline Ble";
    offline.productId = 0x11;
    offline.vendorId = 0x22;
    EXPECT_TRUE(driver_->GetRegisteredDevices().empty());

    DeviceCtx &online = InjectDevice(102, "00:11:22:33:44:02", true);
    online.deviceName = "Online Ble";
    online.productId = 0x1234;
    online.vendorId = 0x5678;
    auto devices = driver_->GetRegisteredDevices();
    ASSERT_EQ(devices.size(), 1);
    EXPECT_EQ(devices[0].midiDeviceInfo.driverDeviceId, 102);
    EXPECT_EQ(devices[0].midiDeviceInfo.deviceType, DeviceType::DEVICE_TYPE_BLE);
    EXPECT_EQ(devices[0].midiDeviceInfo.transportProtocol, TransportProtocol::PROTOCOL_1_0);
    EXPECT_EQ(devices[0].midiDeviceInfo.address, "00:11:22:33:44:02");
    EXPECT_EQ(devices[0].midiDeviceInfo.productId, 0x1234);
    EXPECT_EQ(devices[0].midiDeviceInfo.vendorId, 0x5678);
}

/**
 * @tc.name: CloseDeviceBranches001
 * @tc.desc: Verify CloseDevice returns -1 for unknown ids and 0 for injected devices.
 * @tc.type: FUNC
 */
HWTEST_F(MidiDeviceBleUnitTest, CloseDeviceBranches001, TestSize.Level0)
{
    constexpr int64_t unknownId = 987654;
    EXPECT_EQ(driver_->CloseDevice(unknownId), -1);

    InjectDevice(103, "00:11:22:33:44:03", true);
    EXPECT_EQ(driver_->CloseDevice(103), 0);
    EXPECT_EQ(driver_->devices_.count(103), 0);
}

/**
 * @tc.name: OpenDeviceAddressBranches001
 * @tc.desc: Verify OpenDevice(address) rejects an address that already has a context.
 * @tc.type: FUNC
 */
HWTEST_F(MidiDeviceBleUnitTest, OpenDeviceAddressBranches001, TestSize.Level0)
{
    InjectDevice(104, "00:11:22:33:44:04", true);
    BleDriverCallback callback = [](bool connected, DeviceInformation info) {
        (void)connected;
        (void)info;
    };
    EXPECT_EQ(driver_->OpenDevice("00:11:22:33:44:04", callback), OH_MIDI_STATUS_DEVICE_ALREADY_OPEN);
}

/**
 * @tc.name: OpenInputPortBranches001
 * @tc.desc: Verify OpenInputPort branches: port guard, not found, success, and already-open.
 * @tc.type: FUNC
 */
HWTEST_F(MidiDeviceBleUnitTest, OpenInputPortBranches001, TestSize.Level0)
{
    UmpInputCallback callback = [](std::vector<MidiEventInner> &events) {
        (void)events;
    };
    constexpr int64_t deviceId = 105;
    constexpr uint32_t bleInputPort = 1;
    EXPECT_EQ(driver_->OpenInputPort(deviceId, 0, callback), -1);

    EXPECT_EQ(driver_->OpenInputPort(deviceId, bleInputPort, callback), -1);

    DeviceCtx &device = InjectDevice(105, "00:11:22:33:44:05", true);
    EXPECT_EQ(driver_->OpenInputPort(deviceId, bleInputPort, callback), 0);
    EXPECT_TRUE(device.inputOpen);
    EXPECT_NE(device.inputCallback, nullptr);
    EXPECT_NE(device.processor, nullptr);

    EXPECT_EQ(driver_->OpenInputPort(deviceId, bleInputPort, callback), -1);
}

/**
 * @tc.name: CloseInputPortBranches001
 * @tc.desc: Verify CloseInputPort branches: port guard, not found, not-open rejection, and success.
 * @tc.type: FUNC
 */
HWTEST_F(MidiDeviceBleUnitTest, CloseInputPortBranches001, TestSize.Level0)
{
    constexpr int64_t deviceId = 106;
    constexpr uint32_t bleInputPort = 1;
    EXPECT_EQ(driver_->CloseInputPort(deviceId, 0), -1);

    EXPECT_EQ(driver_->CloseInputPort(deviceId, bleInputPort), -1);

    DeviceCtx &device = InjectDevice(106, "00:11:22:33:44:06", true);
    EXPECT_EQ(driver_->CloseInputPort(deviceId, bleInputPort), -1);

    device.inputOpen = true;
    device.processor = std::make_shared<UmpProcessor>();
    EXPECT_EQ(driver_->CloseInputPort(deviceId, bleInputPort), 0);
    EXPECT_FALSE(device.inputOpen);
    EXPECT_EQ(device.inputCallback, nullptr);
    EXPECT_EQ(device.processor, nullptr);
}

/**
 * @tc.name: OpenOutputPortBranches001
 * @tc.desc: Verify OpenOutputPort branches: not found, success, and already-open rejection.
 * @tc.type: FUNC
 */
HWTEST_F(MidiDeviceBleUnitTest, OpenOutputPortBranches001, TestSize.Level0)
{
    constexpr int64_t deviceId = 107;
    constexpr uint32_t bleOutputPort = 0;
    EXPECT_EQ(driver_->OpenOutputPort(deviceId, 1), -1);

    EXPECT_EQ(driver_->OpenOutputPort(deviceId, bleOutputPort), -1);

    DeviceCtx &device = InjectDevice(107, "00:11:22:33:44:07", true);
    EXPECT_EQ(driver_->OpenOutputPort(deviceId, bleOutputPort), 0);
    EXPECT_TRUE(device.outputOpen);
    EXPECT_NE(device.outProcessor, nullptr);

    EXPECT_EQ(driver_->OpenOutputPort(deviceId, bleOutputPort), -1);
}

/**
 * @tc.name: CloseOutputPortBranches001
 * @tc.desc: Verify CloseOutputPort branches: port guard, not found, not-open rejection, and success.
 * @tc.type: FUNC
 */
HWTEST_F(MidiDeviceBleUnitTest, CloseOutputPortBranches001, TestSize.Level0)
{
    constexpr int64_t deviceId = 108;
    constexpr uint32_t bleOutputPort = 0;
    EXPECT_EQ(driver_->CloseOutputPort(deviceId, 1), -1);

    EXPECT_EQ(driver_->CloseOutputPort(deviceId, bleOutputPort), -1);

    DeviceCtx &device = InjectDevice(108, "00:11:22:33:44:08", true);
    EXPECT_EQ(driver_->CloseOutputPort(deviceId, bleOutputPort), -1);

    device.outputOpen = true;
    device.outProcessor = std::make_shared<UmpProcessor>();
    EXPECT_EQ(driver_->CloseOutputPort(deviceId, bleOutputPort), 0);
    EXPECT_FALSE(device.outputOpen);
    EXPECT_EQ(device.outProcessor, nullptr);
}
