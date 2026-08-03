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
#include "midi_device_open_callback_stub.h"
#include "midi_in_server.h"
#include "midi_service_controller.h"
#include "midi_test_common.h"
#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace OHOS;
using namespace MIDI;
using namespace testing;
using namespace testing::ext;

class RecordingMidiDeviceOpenCallbackStub : public MidiDeviceOpenCallbackStub {
public:
    int32_t NotifyDeviceOpened(bool success, const MidiDeviceInfo &deviceInfo) override
    {
        callCount++;
        lastSuccess = success;
        lastDevice = deviceInfo;
        return OH_MIDI_STATUS_OK;
    }

    int callCount = 0;
    bool lastSuccess = false;
    MidiDeviceInfo lastDevice;
};

class RejectDeathRecipientMidiCallbackStub : public MockMidiCallbackStub {
public:
    bool AddDeathRecipient(const sptr<DeathRecipient> &) override
    {
        return false;
    }
};

class MidiServiceControllerUnitTest : public testing::Test {
public:
    void SetUp() override
    {
        constexpr int64_t unloadDelayMs = 10000;
        controller_ = MidiServiceController::GetInstance();
        // Keep the worker pending until TearDown cancels and joins it. A zero delay can
        // finish before cancellation while leaving std::thread joinable.
        controller_->SetUnloadDelay(unloadDelayMs);
        mockDriver_ = std::make_unique<NiceMock<MockMidiDeviceDriver>>();
        rawMockDriver_ = mockDriver_.get();
        // Use test helper to inject mock driver
        controller_->GetDeviceManagerForTest()->InjectDriverForTest(DeviceType::DEVICE_TYPE_USB,
            std::move(mockDriver_));
        mockCallback_ = new MockMidiCallbackStub();
        sptr<IRemoteObject> clientObj;
        controller_->CreateMidiInServer(mockCallback_->AsObject(), clientObj, clientId_);
    }

    void TearDown() override
    {
        controller_->DestroyMidiClient(clientId_);
        // Use test helper to clear state
        controller_->ClearStateForTest();
    }

    /**
     * Helper to simulate a device being connected and discovered by the manager
     */
    int64_t SimulateDeviceConnection(int64_t driverId, const std::string &name)
    {
        DeviceInformation info;
        info.midiDeviceInfo.driverDeviceId = driverId;
        info.midiDeviceInfo.deviceType = DeviceType::DEVICE_TYPE_USB;
        info.midiDeviceInfo.deviceName = name;
        info.midiDeviceInfo.productId = 0x1234;
        info.midiDeviceInfo.vendorId = 0x5678;
        info.midiDeviceInfo.transportProtocol = TransportProtocol::PROTOCOL_1_0;

        // Port info
        MidiPortInfo port;
        port.portId = 0;
        port.direction = PortDirection::PORT_DIRECTION_INPUT;
        port.name = "Test Port";
        info.portInfos.push_back(port);

        MidiPortInfo outputPort;
        outputPort.portId = 1;
        outputPort.direction = PortDirection::PORT_DIRECTION_OUTPUT;
        outputPort.name = "Output Port";
        info.portInfos.push_back(outputPort);

        std::vector<DeviceInformation> devices = {info};

        EXPECT_CALL(*rawMockDriver_, GetRegisteredDevices).WillOnce(Return(devices));

        controller_->GetDeviceManagerForTest()->UpdateDevices();

        auto allDevices = controller_->GetDeviceManagerForTest()->GetDevices();
        if (allDevices.empty()) {
            return -1;
        }
        return allDevices[0].midiDeviceInfo.deviceId;
    }

protected:
    std::shared_ptr<MidiServiceController> controller_ = nullptr;
    MockMidiDeviceDriver *rawMockDriver_ = nullptr;
    std::unique_ptr<MockMidiDeviceDriver> mockDriver_;
    sptr<MockMidiCallbackStub> mockCallback_;
    uint32_t clientId_ = 0;
};

/**
 * @tc.name: CallbackSlotLifecycleBranches001
 * @tc.desc: Verify callback-slot acquire, move, release, close, and empty branches.
 * @tc.type: FUNC
 */
HWTEST_F(MidiServiceControllerUnitTest, CallbackSlotLifecycleBranches001, TestSize.Level0)
{
    auto callback = std::make_shared<NiceMock<MockMidiServiceCallback>>();
    CallbackSlot slot(callback);

    auto first = slot.Acquire();
    ASSERT_TRUE(first);
    CallbackSlot::Guard moved(std::move(first));
    EXPECT_FALSE(first);
    CallbackSlot::Guard assigned {};
    assigned = std::move(moved);
    CallbackSlot::Guard *sameGuard = &assigned;
    assigned = std::move(*sameGuard);
    EXPECT_TRUE(assigned);

    slot.closing_ = true;
    assigned = CallbackSlot::Guard();
    EXPECT_EQ(slot.activeCallbacks_, 0);
    slot.CloseAndDrain();
    EXPECT_FALSE(slot.Acquire());

    CallbackSlot emptySlot(nullptr);
    EXPECT_FALSE(emptySlot.Acquire());
    emptySlot.CloseAndDrain();
    emptySlot.CloseAndDrain();

    auto activeSlot = std::make_unique<CallbackSlot>(callback);
    activeSlot->activeCallbacks_ = 1;
    activeSlot.reset();

    CallbackSlot destroyedSlot(callback);
    auto destroyedGuard = destroyedSlot.Acquire();
    destroyedSlot.destroyed_.store(true);
    destroyedGuard = CallbackSlot::Guard();

    CallbackSlot multiGuardSlot(callback);
    auto guardOne = multiGuardSlot.Acquire();
    auto guardTwo = multiGuardSlot.Acquire();
    multiGuardSlot.closing_ = true;
    guardOne = CallbackSlot::Guard();
    EXPECT_EQ(multiGuardSlot.activeCallbacks_, 1);
    guardTwo = CallbackSlot::Guard();
    EXPECT_EQ(multiGuardSlot.activeCallbacks_, 0);
}

/**
 * @tc.name: MidiInServerCallbackBranches001
 * @tc.desc: Verify callback delivery, BLE classification, permission refresh, and closed-callback guards.
 * @tc.type: FUNC
 */
HWTEST_F(MidiServiceControllerUnitTest, MidiInServerCallbackBranches001, TestSize.Level0)
{
    auto callback = std::make_shared<NiceMock<MockMidiServiceCallback>>();
    MidiInServer server(9101, callback);

    MidiDeviceInfo usbDevice;
    usbDevice.deviceType = DeviceType::DEVICE_TYPE_USB;
    MidiDeviceInfo bleDevice;
    bleDevice.deviceType = DeviceType::DEVICE_TYPE_BLE;
    EXPECT_FALSE(server.IsBluetoothDevice(usbDevice));
    EXPECT_TRUE(server.IsBluetoothDevice(bleDevice));

    EXPECT_CALL(*callback, NotifyError(OH_MIDI_STATUS_SYSTEM_ERROR)).Times(1);
    server.NotifyError(OH_MIDI_STATUS_SYSTEM_ERROR);

    server.callerTokenId_ = 0;
    EXPECT_CALL(*callback, NotifyDeviceChange(DeviceChangeType::ADD, _)).Times(1);
    server.NotifyDeviceChange(DeviceChangeType::ADD, usbDevice);
    server.NotifyDeviceChange(DeviceChangeType::ADD, bleDevice);
    server.UpdateBluetoothPermission(true);
    server.UpdateBluetoothPermission(false);

    DeviceInformation usbInfo;
    usbInfo.midiDeviceInfo.deviceId = 9201;
    usbInfo.midiDeviceInfo.deviceType = DeviceType::DEVICE_TYPE_USB;
    DeviceInformation bleInfo;
    bleInfo.midiDeviceInfo.deviceId = 9202;
    bleInfo.midiDeviceInfo.deviceType = DeviceType::DEVICE_TYPE_BLE;
    auto manager = controller_->GetDeviceManagerForTest();
    manager->devices_ = {usbInfo, bleInfo};
    std::vector<MidiDeviceInfo> devices;
    EXPECT_EQ(server.GetDevices(devices), OH_MIDI_STATUS_OK);
    EXPECT_FALSE(devices.empty());

    sptr<IRemoteObject> object;
    EXPECT_NE(server.OpenBleDevice("invalid", object), OH_MIDI_STATUS_OK);

    server.ClearCallback();
    server.NotifyError(OH_MIDI_STATUS_SYSTEM_ERROR);
    server.NotifyDeviceChange(DeviceChangeType::ADD, usbDevice);
    server.ClearCallback();
}

/**
 * @tc.name: CreateClient001
 * @tc.desc: Verify client creation generates a valid ID
 * @tc.type: FUNC
 */
HWTEST_F(MidiServiceControllerUnitTest, CreateClient001, TestSize.Level0)
{
    uint32_t newClientId = 0;
    sptr<IRemoteObject> clientObj;
    sptr<MockMidiCallbackStub> cb = new MockMidiCallbackStub();
    int32_t ret = controller_->CreateMidiInServer(cb->AsObject(), clientObj, newClientId);
    EXPECT_EQ(ret, OH_MIDI_STATUS_OK);
    EXPECT_GT(newClientId, 0);
    EXPECT_NE(newClientId, clientId_);
    ret = controller_->DestroyMidiClient(newClientId);
    EXPECT_EQ(ret, OH_MIDI_STATUS_OK);
}

/**
 * @tc.name: DestroyMidiClient001
 * @tc.desc: Verify client creation generates a valid ID
 * @tc.type: FUNC
 */
HWTEST_F(MidiServiceControllerUnitTest, DestroyMidiClient001, TestSize.Level0)
{
    int64_t invalidClientId = 99999;
    sptr<IRemoteObject> clientObj;
    int32_t ret = controller_->DestroyMidiClient(invalidClientId);
    EXPECT_EQ(ret, OH_MIDI_STATUS_INVALID_CLIENT);
}

/**
 * @tc.name: GetDevices001
 * @tc.desc: Verify GetDevices returns mapped information correctly
 * @tc.type: FUNC
 */
HWTEST_F(MidiServiceControllerUnitTest, GetDevices001, TestSize.Level0)
{
    int64_t deviceId = SimulateDeviceConnection(1001, "Yamaha Keyboard");

    auto result = controller_->GetDevices();
    ASSERT_EQ(result.size(), 1);

    EXPECT_EQ(result[0].deviceId, deviceId);
    EXPECT_EQ(result[0].deviceType, DeviceType::DEVICE_TYPE_USB);
    EXPECT_EQ(result[0].transportProtocol, TransportProtocol::PROTOCOL_1_0);
    EXPECT_EQ(result[0].deviceName, "Yamaha Keyboard");
    EXPECT_EQ(result[0].productId, 0x1234);
    EXPECT_EQ(result[0].vendorId, 0x5678);
}

/**
 * @tc.name: OpenDevice001
 * @tc.desc: Successfully open a device
 * @tc.type: FUNC
 */
HWTEST_F(MidiServiceControllerUnitTest, OpenDevice001, TestSize.Level0)
{
    int64_t driverId = 555;
    int64_t deviceId = SimulateDeviceConnection(driverId, "Test Device");

    EXPECT_CALL(*rawMockDriver_, OpenDevice(driverId)).WillOnce(Return(OH_MIDI_STATUS_OK));

    int32_t ret = controller_->OpenDevice(clientId_, deviceId);
    EXPECT_EQ(ret, OH_MIDI_STATUS_OK);
    ASSERT_TRUE(controller_->HasDeviceContextForTest(deviceId));
    EXPECT_TRUE(controller_->HasClientForDeviceForTest(deviceId, clientId_));
}

/**
 * @tc.name: OpenDevice002
 * @tc.desc: Fail to open device with Invalid Device ID
 * @tc.type: FUNC
 */
HWTEST_F(MidiServiceControllerUnitTest, OpenDevice002, TestSize.Level0)
{
    int64_t invalidDeviceId = 99999;

    // Driver should NOT be called
    EXPECT_CALL(*rawMockDriver_, OpenDevice(_)).Times(0);

    int32_t ret = controller_->OpenDevice(clientId_, invalidDeviceId);
    EXPECT_NE(ret, OH_MIDI_STATUS_OK);
}

/**
 * @tc.name: OpenDevice003
 * @tc.desc: Fail to open device when Driver fails
 * @tc.type: FUNC
 */
HWTEST_F(MidiServiceControllerUnitTest, OpenDevice003, TestSize.Level0)
{
    int64_t driverId = 666;
    int64_t deviceId = SimulateDeviceConnection(driverId, "Broken Device");

    // Driver returns internal error
    EXPECT_CALL(*rawMockDriver_, OpenDevice(driverId)).WillOnce(Return(OH_MIDI_STATUS_SYSTEM_ERROR));

    int32_t ret = controller_->OpenDevice(clientId_, deviceId);
    EXPECT_EQ(ret, OH_MIDI_STATUS_GENERIC_INVALID_ARGUMENT);
    EXPECT_FALSE(controller_->HasDeviceContextForTest(deviceId));
}

/**
 * @tc.name: OpenDevice004
 * @tc.desc: Open the same device twice with the same client (Duplicate Open)
 * @tc.type: FUNC
 */
HWTEST_F(MidiServiceControllerUnitTest, OpenDevice004, TestSize.Level0)
{
    int64_t driverId = 777;
    int64_t deviceId = SimulateDeviceConnection(driverId, "Device");

    EXPECT_CALL(*rawMockDriver_, OpenDevice(driverId)).WillOnce(Return(OH_MIDI_STATUS_OK));

    // First Open
    ASSERT_EQ(controller_->OpenDevice(clientId_, deviceId), OH_MIDI_STATUS_OK);

    // Second Open (Same Client)
    int32_t ret = controller_->OpenDevice(clientId_, deviceId);
    EXPECT_EQ(ret, OH_MIDI_STATUS_DEVICE_ALREADY_OPEN);
    ASSERT_TRUE(controller_->HasDeviceContextForTest(deviceId));
    EXPECT_TRUE(controller_->HasClientForDeviceForTest(deviceId, clientId_));
}

/**
 * @tc.name: OpenDevice005
 * @tc.desc: Two different clients open the same device (Should succeed shared)
 * @tc.type: FUNC
 */
HWTEST_F(MidiServiceControllerUnitTest, OpenDevice005, TestSize.Level0)
{
    int64_t driverId = 888;
    int64_t deviceId = SimulateDeviceConnection(driverId, "Shared Device");

    // Create a second client
    uint32_t clientId2 = 0;
    sptr<IRemoteObject> clientObj;
    sptr<MockMidiCallbackStub> cb2 = new MockMidiCallbackStub();
    controller_->CreateMidiInServer(cb2->AsObject(), clientObj, clientId2);

    EXPECT_CALL(*rawMockDriver_, OpenDevice(driverId)).WillOnce(Return(OH_MIDI_STATUS_OK));

    EXPECT_EQ(controller_->OpenDevice(clientId_, deviceId), OH_MIDI_STATUS_OK);

    EXPECT_EQ(controller_->OpenDevice(clientId2, deviceId), OH_MIDI_STATUS_OK);
    ASSERT_TRUE(controller_->HasDeviceContextForTest(deviceId));
    EXPECT_TRUE(controller_->HasClientForDeviceForTest(deviceId, clientId_));
    EXPECT_TRUE(controller_->HasClientForDeviceForTest(deviceId, clientId2));
    controller_->DestroyMidiClient(clientId2);
}

/**
 * @tc.name: OpenDevice006
 * @tc.desc: Open device with Invalid Client ID
 * @tc.type: FUNC
 */
HWTEST_F(MidiServiceControllerUnitTest, OpenDevice006, TestSize.Level0)
{
    int64_t driverId = 111;
    int64_t deviceId = SimulateDeviceConnection(driverId, "Device");
    uint32_t invalidClientId = 99999;

    EXPECT_CALL(*rawMockDriver_, OpenDevice(_)).Times(0);

    int32_t ret = controller_->OpenDevice(invalidClientId, deviceId);
    EXPECT_EQ(ret, OH_MIDI_STATUS_INVALID_CLIENT);
    EXPECT_FALSE(controller_->HasDeviceContextForTest(deviceId));
}

/**
 * @tc.name: CloseDevice001
 * @tc.desc: Close device successfully
 * @tc.type: FUNC
 */
HWTEST_F(MidiServiceControllerUnitTest, CloseDevice001, TestSize.Level0)
{
    int64_t driverId = 123;
    int64_t deviceId = SimulateDeviceConnection(driverId, "Device To Close");

    EXPECT_CALL(*rawMockDriver_, OpenDevice(driverId)).WillOnce(Return(OH_MIDI_STATUS_OK));
    controller_->OpenDevice(clientId_, deviceId);

    EXPECT_CALL(*rawMockDriver_, CloseDevice(driverId)).WillOnce(Return(OH_MIDI_STATUS_OK));

    int32_t ret = controller_->CloseDevice(clientId_, deviceId);
    EXPECT_EQ(ret, OH_MIDI_STATUS_OK);
    EXPECT_FALSE(controller_->HasDeviceContextForTest(deviceId));
}

/**
 * @tc.name: CloseDevice002
 * @tc.desc: Close device that was not opened by this client
 * @tc.type: FUNC
 */
HWTEST_F(MidiServiceControllerUnitTest, CloseDevice002, TestSize.Level0)
{
    int64_t driverId = 124;
    int64_t deviceId = SimulateDeviceConnection(driverId, "Device Unopened");

    EXPECT_CALL(*rawMockDriver_, CloseDevice(_)).Times(0);

    int32_t ret = controller_->CloseDevice(clientId_, deviceId);
    EXPECT_NE(ret, OH_MIDI_STATUS_OK);
}

/**
 * @tc.name: CloseDevice003
 * @tc.desc: Two different clients open and Close the same device
 * @tc.type: FUNC
 */
HWTEST_F(MidiServiceControllerUnitTest, CloseDevice003, TestSize.Level0)
{
    int64_t driverId = 888;
    int64_t deviceId = SimulateDeviceConnection(driverId, "Shared Device");

    // Create a second client
    uint32_t clientId2 = 0;
    sptr<IRemoteObject> clientObj;
    sptr<MockMidiCallbackStub> cb2 = new MockMidiCallbackStub();
    controller_->CreateMidiInServer(cb2->AsObject(), clientObj, clientId2);

    EXPECT_CALL(*rawMockDriver_, OpenDevice(driverId)).WillOnce(Return(OH_MIDI_STATUS_OK));

    controller_->OpenDevice(clientId_, deviceId);
    controller_->OpenDevice(clientId2, deviceId);

    int32_t ret = controller_->CloseDevice(clientId_, deviceId);
    EXPECT_EQ(ret, OH_MIDI_STATUS_OK);
    ASSERT_TRUE(controller_->HasDeviceContextForTest(deviceId));
    EXPECT_FALSE(controller_->HasClientForDeviceForTest(deviceId, clientId_));
    EXPECT_TRUE(controller_->HasClientForDeviceForTest(deviceId, clientId2));

    EXPECT_CALL(*rawMockDriver_, CloseDevice(driverId)).WillOnce(Return(OH_MIDI_STATUS_OK));

    ret = controller_->CloseDevice(clientId2, deviceId);
    EXPECT_EQ(ret, OH_MIDI_STATUS_OK);
    EXPECT_FALSE(controller_->HasDeviceContextForTest(deviceId));
    controller_->DestroyMidiClient(clientId2);
}

/**
 * @tc.name: OpenInputPort001
 * @tc.desc: Open Input Port successfully
 * @tc.type: FUNC
 */
HWTEST_F(MidiServiceControllerUnitTest, OpenInputPort001, TestSize.Level0)
{
    int64_t driverId = 200;
    int64_t deviceId = SimulateDeviceConnection(driverId, "Midi Controller");
    uint32_t portIndex = 0;

    EXPECT_CALL(*rawMockDriver_, OpenDevice(driverId)).WillOnce(Return(OH_MIDI_STATUS_OK));
    controller_->OpenDevice(clientId_, deviceId);

    EXPECT_CALL(*rawMockDriver_, OpenInputPort(driverId, portIndex, _)).WillOnce(Return(OH_MIDI_STATUS_OK));

    std::shared_ptr<MidiSharedRing> buffer;
    int32_t ret = controller_->OpenInputPort(clientId_, buffer, deviceId, portIndex);
    EXPECT_EQ(ret, OH_MIDI_STATUS_OK);
    ASSERT_TRUE(controller_->HasDeviceContextForTest(deviceId));
}

/**
 * @tc.name: OpenInputPort002
 * @tc.desc: Fail to Open Input Port if Device not opened first
 * @tc.type: FUNC
 */
HWTEST_F(MidiServiceControllerUnitTest, OpenInputPort002, TestSize.Level0)
{
    int64_t driverId = 201;
    int64_t deviceId = SimulateDeviceConnection(driverId, "Midi Controller");
    uint32_t portIndex = 0;

    // Device not opened via OpenDevice
    std::shared_ptr<MidiSharedRing> buffer;
    int32_t ret = controller_->OpenInputPort(clientId_, buffer, deviceId, portIndex);
    EXPECT_NE(ret, OH_MIDI_STATUS_OK);
}

/**
 * @tc.name: OpenInputPort003
 * @tc.desc: Two different clients open Input Port, but one of them don't open device;
 * @tc.type: FUNC
 */
HWTEST_F(MidiServiceControllerUnitTest, OpenInputPort003, TestSize.Level0)
{
    int64_t driverId = 201;
    int64_t deviceId = SimulateDeviceConnection(driverId, "Midi Controller");
    uint32_t portIndex = 0;

    uint32_t clientId2 = 0;
    sptr<IRemoteObject> clientObj;
    sptr<MockMidiCallbackStub> cb2 = new MockMidiCallbackStub();
    controller_->CreateMidiInServer(cb2->AsObject(), clientObj, clientId2);

    EXPECT_CALL(*rawMockDriver_, OpenDevice(driverId)).WillOnce(Return(OH_MIDI_STATUS_OK));
    controller_->OpenDevice(clientId_, deviceId);

    EXPECT_CALL(*rawMockDriver_, OpenInputPort(driverId, portIndex, _)).WillOnce(Return(OH_MIDI_STATUS_OK));

    std::shared_ptr<MidiSharedRing> buffer;
    int32_t ret = controller_->OpenInputPort(clientId_, buffer, deviceId, portIndex);
    EXPECT_EQ(ret, OH_MIDI_STATUS_OK);
    std::shared_ptr<MidiSharedRing> buffer2;
    ret = controller_->OpenInputPort(clientId2, buffer2, deviceId, portIndex);
    EXPECT_EQ(ret, OH_MIDI_STATUS_SYSTEM_ERROR);
    controller_->DestroyMidiClient(clientId2);
}

/**
 * @tc.name: OpenInputPort004
 * @tc.desc: Two different clients open Input Port
 * @tc.type: FUNC
 */
HWTEST_F(MidiServiceControllerUnitTest, OpenInputPort004, TestSize.Level0)
{
    int64_t driverId = 201;
    int64_t deviceId = SimulateDeviceConnection(driverId, "Midi Controller");
    uint32_t portIndex = 0;

    uint32_t clientId2 = 0;
    sptr<IRemoteObject> clientObj;
    sptr<MockMidiCallbackStub> cb2 = new MockMidiCallbackStub();
    controller_->CreateMidiInServer(cb2->AsObject(), clientObj, clientId2);

    EXPECT_CALL(*rawMockDriver_, OpenDevice(driverId)).WillOnce(Return(OH_MIDI_STATUS_OK));
    controller_->OpenDevice(clientId_, deviceId);
    controller_->OpenDevice(clientId2, deviceId);

    EXPECT_CALL(*rawMockDriver_, OpenInputPort(driverId, portIndex, _)).WillOnce(Return(OH_MIDI_STATUS_OK));

    std::shared_ptr<MidiSharedRing> buffer;
    int32_t ret = controller_->OpenInputPort(clientId_, buffer, deviceId, portIndex);
    EXPECT_EQ(ret, OH_MIDI_STATUS_OK);
    std::shared_ptr<MidiSharedRing> buffer2;
    ret = controller_->OpenInputPort(clientId2, buffer2, deviceId, portIndex);
    EXPECT_EQ(ret, OH_MIDI_STATUS_OK);
    ASSERT_TRUE(controller_->HasDeviceContextForTest(deviceId));
    controller_->DestroyMidiClient(clientId2);
}

/**
 * @tc.name: CloseInputPort001
 * @tc.desc: Close Input Port successfully
 * @tc.type: FUNC
 */
HWTEST_F(MidiServiceControllerUnitTest, CloseInputPort001, TestSize.Level0)
{
    int64_t driverId = 300;
    int64_t deviceId = SimulateDeviceConnection(driverId, "Midi Key");
    uint32_t portIndex = 0;

    // Setup: Open Device -> Open Port
    EXPECT_CALL(*rawMockDriver_, OpenDevice(driverId)).WillOnce(Return(OH_MIDI_STATUS_OK));
    controller_->OpenDevice(clientId_, deviceId);

    EXPECT_CALL(*rawMockDriver_, OpenInputPort(driverId, portIndex, _)).WillOnce(Return(OH_MIDI_STATUS_OK));
    std::shared_ptr<MidiSharedRing> buffer;
    controller_->OpenInputPort(clientId_, buffer, deviceId, portIndex);

    EXPECT_CALL(*rawMockDriver_, CloseInputPort(driverId, portIndex)).WillOnce(Return(OH_MIDI_STATUS_OK));

    int32_t ret = controller_->CloseInputPort(clientId_, deviceId, portIndex);
    EXPECT_EQ(ret, OH_MIDI_STATUS_OK);
    ASSERT_TRUE(controller_->HasDeviceContextForTest(deviceId));
}

/**
 * @tc.name: CloseInputPort002
 * @tc.desc: Two different clients open and close Input Port
 * @tc.type: FUNC
 */
HWTEST_F(MidiServiceControllerUnitTest, CloseInputPort002, TestSize.Level0)
{
    int64_t driverId = 300;
    int64_t deviceId = SimulateDeviceConnection(driverId, "Midi Key");
    uint32_t portIndex = 0;

    uint32_t clientId2 = 0;
    sptr<IRemoteObject> clientObj;
    sptr<MockMidiCallbackStub> cb2 = new MockMidiCallbackStub();
    controller_->CreateMidiInServer(cb2->AsObject(), clientObj, clientId2);
    EXPECT_CALL(*rawMockDriver_, OpenDevice(driverId)).WillOnce(Return(OH_MIDI_STATUS_OK));
    controller_->OpenDevice(clientId_, deviceId);
    controller_->OpenDevice(clientId2, deviceId);

    EXPECT_CALL(*rawMockDriver_, OpenInputPort(driverId, portIndex, _)).WillOnce(Return(OH_MIDI_STATUS_OK));
    std::shared_ptr<MidiSharedRing> buffer;
    controller_->OpenInputPort(clientId_, buffer, deviceId, portIndex);
    std::shared_ptr<MidiSharedRing> buffer2;
    int32_t ret = controller_->OpenInputPort(clientId2, buffer2, deviceId, portIndex);
    ret = controller_->CloseInputPort(clientId_, deviceId, portIndex);
    EXPECT_EQ(ret, OH_MIDI_STATUS_OK);
    ASSERT_TRUE(controller_->HasDeviceContextForTest(deviceId));
    EXPECT_CALL(*rawMockDriver_, CloseInputPort(driverId, portIndex)).WillOnce(Return(OH_MIDI_STATUS_OK));
    ret = controller_->CloseInputPort(clientId2, deviceId, portIndex);
    EXPECT_EQ(ret, OH_MIDI_STATUS_OK);

    ASSERT_TRUE(controller_->HasDeviceContextForTest(deviceId));
    controller_->DestroyMidiClient(clientId2);
}

/**
 * @tc.name: DestroyClient001
 * @tc.desc: Destroying a client should close associated ports and devices
 * @tc.type: FUNC
 */
HWTEST_F(MidiServiceControllerUnitTest, DestroyClient001, TestSize.Level0)
{
    int64_t driverId = 400;
    int64_t deviceId = SimulateDeviceConnection(driverId, "Cleanup Device");
    uint32_t portIndex = 0;

    // Setup: Open Device, Open Port
    EXPECT_CALL(*rawMockDriver_, OpenDevice(driverId)).WillOnce(Return(OH_MIDI_STATUS_OK));
    EXPECT_CALL(*rawMockDriver_, OpenInputPort(driverId, portIndex, _)).WillOnce(Return(OH_MIDI_STATUS_OK));

    controller_->OpenDevice(clientId_, deviceId);
    std::shared_ptr<MidiSharedRing> buffer = std::make_shared<MidiSharedRing>(2048);
    controller_->OpenInputPort(clientId_, buffer, deviceId, portIndex);

    EXPECT_CALL(*rawMockDriver_, CloseInputPort(driverId, portIndex)).WillOnce(Return(OH_MIDI_STATUS_OK));
    EXPECT_CALL(*rawMockDriver_, CloseDevice(driverId)).WillOnce(Return(OH_MIDI_STATUS_OK));
    int32_t ret = controller_->DestroyMidiClient(clientId_);
    EXPECT_EQ(ret, OH_MIDI_STATUS_OK);
}

/**
 * @tc.name: NotifyDeviceChange_ResourceCleanup001
 * @tc.desc: Verify that client's openDevices is cleaned up when device is removed
 * @tc.type: FUNC
 */
HWTEST_F(MidiServiceControllerUnitTest, NotifyDeviceChange_ResourceCleanup001, TestSize.Level0)
{
    int64_t driverId = 600;
    int64_t deviceId = SimulateDeviceConnection(driverId, "Test Device");

    // Open device
    EXPECT_CALL(*rawMockDriver_, OpenDevice(driverId)).WillOnce(Return(OH_MIDI_STATUS_OK));
    ASSERT_EQ(controller_->OpenDevice(clientId_, deviceId), OH_MIDI_STATUS_OK);

    // Simulate device removal
    DeviceInformation deviceInfo;
    deviceInfo.midiDeviceInfo.deviceId = deviceId;
    deviceInfo.midiDeviceInfo.deviceType = DeviceType::DEVICE_TYPE_USB;

    controller_->NotifyDeviceChange(DeviceChangeType::REMOVED, deviceInfo);

    // Verify device context is removed
    EXPECT_FALSE(controller_->HasDeviceContextForTest(deviceId));
}

/**
 * @tc.name: NotifyDeviceChange_PortCountCleanup001
 * @tc.desc: Verify that openPortCount is decremented when device is removed
 * @tc.type: FUNC
 */
HWTEST_F(MidiServiceControllerUnitTest, NotifyDeviceChange_PortCountCleanup001, TestSize.Level0)
{
    int64_t driverId = 601;
    int64_t deviceId = SimulateDeviceConnection(driverId, "Port Device");
    uint32_t portIndex = 0;

    // Open device and port
    EXPECT_CALL(*rawMockDriver_, OpenDevice(driverId)).WillOnce(Return(OH_MIDI_STATUS_OK));
    ASSERT_EQ(controller_->OpenDevice(clientId_, deviceId), OH_MIDI_STATUS_OK);

    EXPECT_CALL(*rawMockDriver_, OpenInputPort(driverId, portIndex, _)).WillOnce(Return(OH_MIDI_STATUS_OK));
    std::shared_ptr<MidiSharedRing> buffer = std::make_shared<MidiSharedRing>(2048);
    ASSERT_EQ(controller_->OpenInputPort(clientId_, buffer, deviceId, portIndex), OH_MIDI_STATUS_OK);

    // Simulate device removal
    DeviceInformation deviceInfo;
    deviceInfo.midiDeviceInfo.deviceId = deviceId;
    deviceInfo.midiDeviceInfo.deviceType = DeviceType::DEVICE_TYPE_USB;

    controller_->NotifyDeviceChange(DeviceChangeType::REMOVED, deviceInfo);

    // Verify resources are cleaned
    EXPECT_FALSE(controller_->HasDeviceContextForTest(deviceId));
}

/**
 * @tc.name: NotifyDeviceChange_MultiClient001
 * @tc.desc: Verify that multi-client resources are cleaned when device is removed
 * @tc.type: FUNC
 */
HWTEST_F(MidiServiceControllerUnitTest, NotifyDeviceChange_MultiClient001, TestSize.Level0)
{
    int64_t driverId = 602;
    int64_t deviceId = SimulateDeviceConnection(driverId, "Shared Device");

    // Create second client
    uint32_t clientId2 = 0;
    sptr<IRemoteObject> clientObj;
    sptr<MockMidiCallbackStub> cb2 = new MockMidiCallbackStub();
    controller_->CreateMidiInServer(cb2->AsObject(), clientObj, clientId2);

    // Both clients open the same device
    EXPECT_CALL(*rawMockDriver_, OpenDevice(driverId)).WillOnce(Return(OH_MIDI_STATUS_OK));
    ASSERT_EQ(controller_->OpenDevice(clientId_, deviceId), OH_MIDI_STATUS_OK);
    ASSERT_EQ(controller_->OpenDevice(clientId2, deviceId), OH_MIDI_STATUS_OK);

    // Simulate device removal
    DeviceInformation deviceInfo;
    deviceInfo.midiDeviceInfo.deviceId = deviceId;
    deviceInfo.midiDeviceInfo.deviceType = DeviceType::DEVICE_TYPE_USB;

    controller_->NotifyDeviceChange(DeviceChangeType::REMOVED, deviceInfo);

    // Verify cleanup
    EXPECT_FALSE(controller_->HasDeviceContextForTest(deviceId));

    // Cleanup second client
    controller_->DestroyMidiClient(clientId2);
}

/**
 * @tc.name: NotifyDeviceChange_NonExistentDevice001
 * @tc.desc: Verify that removing non-existent device does not crash
 * @tc.type: FUNC
 */
HWTEST_F(MidiServiceControllerUnitTest, NotifyDeviceChange_NonExistentDevice001, TestSize.Level0)
{
    DeviceInformation deviceInfo;
    deviceInfo.midiDeviceInfo.deviceId = 99999;
    deviceInfo.midiDeviceInfo.deviceType = DeviceType::DEVICE_TYPE_USB;

    // Should not crash
    controller_->NotifyDeviceChange(DeviceChangeType::REMOVED, deviceInfo);
    EXPECT_TRUE(true);
}

/**
 * @tc.name: NotifyDeviceChange_NoCleanupOnAdd001
 * @tc.desc: Verify that ADD event does not trigger cleanup logic
 * @tc.type: FUNC
 */
HWTEST_F(MidiServiceControllerUnitTest, NotifyDeviceChange_NoCleanupOnAdd001, TestSize.Level0)
{
    int64_t driverId = 603;
    int64_t deviceId = SimulateDeviceConnection(driverId, "Add Test Device");

    EXPECT_CALL(*rawMockDriver_, OpenDevice(driverId)).WillOnce(Return(OH_MIDI_STATUS_OK));
    ASSERT_EQ(controller_->OpenDevice(clientId_, deviceId), OH_MIDI_STATUS_OK);

    // Simulate device ADD (should not trigger cleanup)
    DeviceInformation deviceInfo;
    deviceInfo.midiDeviceInfo.deviceId = deviceId;
    deviceInfo.midiDeviceInfo.deviceType = DeviceType::DEVICE_TYPE_USB;

    controller_->NotifyDeviceChange(DeviceChangeType::ADD, deviceInfo);

    // Device context should still exist
    EXPECT_TRUE(controller_->HasDeviceContextForTest(deviceId));

    // Cleanup
    EXPECT_CALL(*rawMockDriver_, CloseDevice(driverId)).WillOnce(Return(OH_MIDI_STATUS_OK));
    controller_->CloseDevice(clientId_, deviceId);
}

/**
 * @tc.name: OpenDevice_ResourceTrackingForSecondClient
 * @tc.desc: Verify that openDevices is correctly updated when second client opens the same device
 * @tc.type: FUNC
 * @tc.require: Bug fix - OpenDevice() should update openDevices for all clients
 */
HWTEST_F(MidiServiceControllerUnitTest, OpenDevice_ResourceTrackingForSecondClient, TestSize.Level0)
{
    int64_t driverId = 700;
    int64_t deviceId = SimulateDeviceConnection(driverId, "Shared Device");

    // Create a second client
    uint32_t clientId2 = 0;
    sptr<IRemoteObject> clientObj;
    sptr<MockMidiCallbackStub> cb2 = new MockMidiCallbackStub();
    controller_->CreateMidiInServer(cb2->AsObject(), clientObj, clientId2);

    // First client opens device
    EXPECT_CALL(*rawMockDriver_, OpenDevice(driverId)).WillOnce(Return(OH_MIDI_STATUS_OK));
    ASSERT_EQ(controller_->OpenDevice(clientId_, deviceId), OH_MIDI_STATUS_OK);

    // Verify first client's openDevices
    auto openDevices1 = controller_->GetOpenDevicesForTest(clientId_);
    EXPECT_EQ(openDevices1.size(), 1);
    EXPECT_NE(openDevices1.find(deviceId), openDevices1.end());

    // Second client opens the same device (should NOT call driver again)
    EXPECT_CALL(*rawMockDriver_, OpenDevice(_)).Times(0);
    ASSERT_EQ(controller_->OpenDevice(clientId2, deviceId), OH_MIDI_STATUS_OK);

    // Verify second client's openDevices is also updated (BUG FIX VERIFICATION)
    auto openDevices2 = controller_->GetOpenDevicesForTest(clientId2);
    EXPECT_EQ(openDevices2.size(), 1);
    EXPECT_NE(openDevices2.find(deviceId), openDevices2.end());

    // Cleanup
    controller_->DestroyMidiClient(clientId2);
}

/**
 * @tc.name: OpenInputPort_ResourceTrackingForSecondClient
 * @tc.desc: Verify that openPortCount is correctly updated when second client opens the same input port
 * @tc.type: FUNC
 * @tc.require: Bug fix - OpenInputPort() should update openPortCount for all clients
 */
HWTEST_F(MidiServiceControllerUnitTest, OpenInputPort_ResourceTrackingForSecondClient, TestSize.Level0)
{
    int64_t driverId = 701;
    int64_t deviceId = SimulateDeviceConnection(driverId, "Shared Port Device");
    uint32_t portIndex = 0;

    // Create a second client
    uint32_t clientId2 = 0;
    sptr<IRemoteObject> clientObj;
    sptr<MockMidiCallbackStub> cb2 = new MockMidiCallbackStub();
    controller_->CreateMidiInServer(cb2->AsObject(), clientObj, clientId2);

    // Both clients open device
    EXPECT_CALL(*rawMockDriver_, OpenDevice(driverId)).WillOnce(Return(OH_MIDI_STATUS_OK));
    ASSERT_EQ(controller_->OpenDevice(clientId_, deviceId), OH_MIDI_STATUS_OK);
    ASSERT_EQ(controller_->OpenDevice(clientId2, deviceId), OH_MIDI_STATUS_OK);

    // First client opens input port
    EXPECT_CALL(*rawMockDriver_, OpenInputPort(driverId, portIndex, _)).WillOnce(Return(OH_MIDI_STATUS_OK));
    std::shared_ptr<MidiSharedRing> buffer1;
    ASSERT_EQ(controller_->OpenInputPort(clientId_, buffer1, deviceId, portIndex), OH_MIDI_STATUS_OK);

    // Verify first client's openPortCount
    EXPECT_EQ(controller_->GetOpenPortCountForTest(clientId_), 1);

    // Second client opens the same input port (should NOT call driver again)
    EXPECT_CALL(*rawMockDriver_, OpenInputPort(_, _, _)).Times(0);
    std::shared_ptr<MidiSharedRing> buffer2;
    ASSERT_EQ(controller_->OpenInputPort(clientId2, buffer2, deviceId, portIndex), OH_MIDI_STATUS_OK);

    // Verify second client's openPortCount is also updated (BUG FIX VERIFICATION)
    EXPECT_EQ(controller_->GetOpenPortCountForTest(clientId2), 1);

    // Cleanup
    controller_->DestroyMidiClient(clientId2);
}

/**
 * @tc.name: OpenOutputPort_ResourceTrackingForSecondClient
 * @tc.desc: Verify that openPortCount is correctly updated when second client opens the same output port
 * @tc.type: FUNC
 * @tc.require: Bug fix - OpenOutputPort() should update openPortCount for all clients
 */
HWTEST_F(MidiServiceControllerUnitTest, OpenOutputPort_ResourceTrackingForSecondClient, TestSize.Level0)
{
    int64_t driverId = 702;
    int64_t deviceId = SimulateDeviceConnection(driverId, "Shared Output Port Device");
    uint32_t portIndex = 0;

    // Create a second client
    uint32_t clientId2 = 0;
    sptr<IRemoteObject> clientObj;
    sptr<MockMidiCallbackStub> cb2 = new MockMidiCallbackStub();
    controller_->CreateMidiInServer(cb2->AsObject(), clientObj, clientId2);

    // Both clients open device
    EXPECT_CALL(*rawMockDriver_, OpenDevice(driverId)).WillOnce(Return(OH_MIDI_STATUS_OK));
    ASSERT_EQ(controller_->OpenDevice(clientId_, deviceId), OH_MIDI_STATUS_OK);
    ASSERT_EQ(controller_->OpenDevice(clientId2, deviceId), OH_MIDI_STATUS_OK);

    // First client opens output port
    EXPECT_CALL(*rawMockDriver_, OpenOutputPort(driverId, portIndex)).WillOnce(Return(OH_MIDI_STATUS_OK));
    std::shared_ptr<MidiSharedRing> buffer1;
    ASSERT_EQ(controller_->OpenOutputPort(clientId_, buffer1, deviceId, portIndex), OH_MIDI_STATUS_OK);

    // Verify first client's openPortCount
    EXPECT_EQ(controller_->GetOpenPortCountForTest(clientId_), 1);

    // Second client opens the same output port (should NOT call driver again)
    EXPECT_CALL(*rawMockDriver_, OpenOutputPort(_, _)).Times(0);
    std::shared_ptr<MidiSharedRing> buffer2;
    ASSERT_EQ(controller_->OpenOutputPort(clientId2, buffer2, deviceId, portIndex), OH_MIDI_STATUS_OK);

    // Verify second client's openPortCount is also updated (BUG FIX VERIFICATION)
    EXPECT_EQ(controller_->GetOpenPortCountForTest(clientId2), 1);

    // Cleanup
    controller_->DestroyMidiClient(clientId2);
}

/**
 * @tc.name: CloseInputPort_ResourceTrackingForSecondClient
 * @tc.desc: Verify that openPortCount is correctly decremented when each client closes the same input port
 * @tc.type: FUNC
 * @tc.require: Bug fix - CloseInputPortInner() should decrement openPortCount for all clients
 */
HWTEST_F(MidiServiceControllerUnitTest, CloseInputPort_ResourceTrackingForSecondClient, TestSize.Level0)
{
    int64_t driverId = 800;
    int64_t deviceId = SimulateDeviceConnection(driverId, "Shared Input Port Device");
    uint32_t portIndex = 0;

    // Create a second client
    uint32_t clientId2 = 0;
    sptr<IRemoteObject> clientObj;
    sptr<MockMidiCallbackStub> cb2 = new MockMidiCallbackStub();
    controller_->CreateMidiInServer(cb2->AsObject(), clientObj, clientId2);

    // Both clients open device
    EXPECT_CALL(*rawMockDriver_, OpenDevice(driverId)).WillOnce(Return(OH_MIDI_STATUS_OK));
    ASSERT_EQ(controller_->OpenDevice(clientId_, deviceId), OH_MIDI_STATUS_OK);
    ASSERT_EQ(controller_->OpenDevice(clientId2, deviceId), OH_MIDI_STATUS_OK);

    // Both clients open input port
    EXPECT_CALL(*rawMockDriver_, OpenInputPort(driverId, portIndex, _)).WillOnce(Return(OH_MIDI_STATUS_OK));
    std::shared_ptr<MidiSharedRing> buffer1;
    ASSERT_EQ(controller_->OpenInputPort(clientId_, buffer1, deviceId, portIndex), OH_MIDI_STATUS_OK);
    std::shared_ptr<MidiSharedRing> buffer2;
    ASSERT_EQ(controller_->OpenInputPort(clientId2, buffer2, deviceId, portIndex), OH_MIDI_STATUS_OK);

    // Verify both clients have openPortCount = 1
    EXPECT_EQ(controller_->GetOpenPortCountForTest(clientId_), 1);
    EXPECT_EQ(controller_->GetOpenPortCountForTest(clientId2), 1);

    // First client closes input port (should NOT call driver close because client2 still connected)
    EXPECT_CALL(*rawMockDriver_, CloseInputPort(_, _)).Times(0);
    ASSERT_EQ(controller_->CloseInputPort(clientId_, deviceId, portIndex), OH_MIDI_STATUS_OK);

    // BUG FIX VERIFICATION: First client's openPortCount should be decremented
    EXPECT_EQ(controller_->GetOpenPortCountForTest(clientId_), 0);
    // Second client's openPortCount should still be 1
    EXPECT_EQ(controller_->GetOpenPortCountForTest(clientId2), 1);

    // Second client closes input port (should call driver close because all clients disconnected)
    EXPECT_CALL(*rawMockDriver_, CloseInputPort(driverId, portIndex)).WillOnce(Return(OH_MIDI_STATUS_OK));
    ASSERT_EQ(controller_->CloseInputPort(clientId2, deviceId, portIndex), OH_MIDI_STATUS_OK);

    // BUG FIX VERIFICATION: Second client's openPortCount should also be decremented
    EXPECT_EQ(controller_->GetOpenPortCountForTest(clientId2), 0);

    // Cleanup
    controller_->DestroyMidiClient(clientId2);
}

/**
 * @tc.name: CloseOutputPort_ResourceTrackingForSecondClient
 * @tc.desc: Verify that openPortCount is correctly decremented when each client closes the same output port
 * @tc.type: FUNC
 * @tc.require: Bug fix - CloseOutputPortInner() should decrement openPortCount for all clients
 */
HWTEST_F(MidiServiceControllerUnitTest, CloseOutputPort_ResourceTrackingForSecondClient, TestSize.Level0)
{
    int64_t driverId = 801;
    int64_t deviceId = SimulateDeviceConnection(driverId, "Shared Output Port Device");
    uint32_t portIndex = 0;

    // Create a second client
    uint32_t clientId2 = 0;
    sptr<IRemoteObject> clientObj;
    sptr<MockMidiCallbackStub> cb2 = new MockMidiCallbackStub();
    controller_->CreateMidiInServer(cb2->AsObject(), clientObj, clientId2);

    // Both clients open device
    EXPECT_CALL(*rawMockDriver_, OpenDevice(driverId)).WillOnce(Return(OH_MIDI_STATUS_OK));
    ASSERT_EQ(controller_->OpenDevice(clientId_, deviceId), OH_MIDI_STATUS_OK);
    ASSERT_EQ(controller_->OpenDevice(clientId2, deviceId), OH_MIDI_STATUS_OK);

    // Both clients open output port
    EXPECT_CALL(*rawMockDriver_, OpenOutputPort(driverId, portIndex)).WillOnce(Return(OH_MIDI_STATUS_OK));
    std::shared_ptr<MidiSharedRing> buffer1;
    ASSERT_EQ(controller_->OpenOutputPort(clientId_, buffer1, deviceId, portIndex), OH_MIDI_STATUS_OK);
    std::shared_ptr<MidiSharedRing> buffer2;
    ASSERT_EQ(controller_->OpenOutputPort(clientId2, buffer2, deviceId, portIndex), OH_MIDI_STATUS_OK);

    // Verify both clients have openPortCount = 1
    EXPECT_EQ(controller_->GetOpenPortCountForTest(clientId_), 1);
    EXPECT_EQ(controller_->GetOpenPortCountForTest(clientId2), 1);

    // First client closes output port (should NOT call driver close because client2 still connected)
    EXPECT_CALL(*rawMockDriver_, CloseOutputPort(_, _)).Times(0);
    ASSERT_EQ(controller_->CloseOutputPort(clientId_, deviceId, portIndex), OH_MIDI_STATUS_OK);

    // BUG FIX VERIFICATION: First client's openPortCount should be decremented
    EXPECT_EQ(controller_->GetOpenPortCountForTest(clientId_), 0);
    // Second client's openPortCount should still be 1
    EXPECT_EQ(controller_->GetOpenPortCountForTest(clientId2), 1);

    // Second client closes output port (should call driver close because all clients disconnected)
    EXPECT_CALL(*rawMockDriver_, CloseOutputPort(driverId, portIndex)).WillOnce(Return(OH_MIDI_STATUS_OK));
    ASSERT_EQ(controller_->CloseOutputPort(clientId2, deviceId, portIndex), OH_MIDI_STATUS_OK);

    // BUG FIX VERIFICATION: Second client's openPortCount should also be decremented
    EXPECT_EQ(controller_->GetOpenPortCountForTest(clientId2), 0);

    // Cleanup
    controller_->DestroyMidiClient(clientId2);
}

/**
 * @tc.name: ClosePort_MultiClientIntegration
 * @tc.desc: Integration test - verify all resource tracking works correctly for two clients with multiple ports
 * @tc.type: FUNC
 * @tc.note: MAX_CLIENTS_PER_APP = 2, so only 2 clients per app
 */
HWTEST_F(MidiServiceControllerUnitTest, ClosePort_MultiClientIntegration, TestSize.Level0)
{
    int64_t driverId = 802;
    int64_t deviceId = SimulateDeviceConnection(driverId, "Integration Device");
    uint32_t inputPortIndex = 0;
    uint32_t outputPortIndex = 1;

    // Create second client (MAX_CLIENTS_PER_APP = 2, so max 2 clients)
    uint32_t clientId2 = 0;
    sptr<IRemoteObject> clientObj;
    sptr<MockMidiCallbackStub> cb2 = new MockMidiCallbackStub();
    controller_->CreateMidiInServer(cb2->AsObject(), clientObj, clientId2);

    // Both clients open device
    EXPECT_CALL(*rawMockDriver_, OpenDevice(driverId)).WillOnce(Return(OH_MIDI_STATUS_OK));
    ASSERT_EQ(controller_->OpenDevice(clientId_, deviceId), OH_MIDI_STATUS_OK);
    ASSERT_EQ(controller_->OpenDevice(clientId2, deviceId), OH_MIDI_STATUS_OK);

    // Both clients open input port
    EXPECT_CALL(*rawMockDriver_, OpenInputPort(driverId, inputPortIndex, _)).WillOnce(Return(OH_MIDI_STATUS_OK));
    std::shared_ptr<MidiSharedRing> inBuffer1, inBuffer2;
    ASSERT_EQ(controller_->OpenInputPort(clientId_, inBuffer1, deviceId, inputPortIndex), OH_MIDI_STATUS_OK);
    ASSERT_EQ(controller_->OpenInputPort(clientId2, inBuffer2, deviceId, inputPortIndex), OH_MIDI_STATUS_OK);

    // Both clients open output port
    EXPECT_CALL(*rawMockDriver_, OpenOutputPort(driverId, outputPortIndex)).WillOnce(Return(OH_MIDI_STATUS_OK));
    std::shared_ptr<MidiSharedRing> outBuffer1, outBuffer2;
    ASSERT_EQ(controller_->OpenOutputPort(clientId_, outBuffer1, deviceId, outputPortIndex), OH_MIDI_STATUS_OK);
    ASSERT_EQ(controller_->OpenOutputPort(clientId2, outBuffer2, deviceId, outputPortIndex), OH_MIDI_STATUS_OK);

    // Verify initial port counts
    EXPECT_EQ(controller_->GetOpenPortCountForTest(clientId_), 2);  // 1 input + 1 output
    EXPECT_EQ(controller_->GetOpenPortCountForTest(clientId2), 2);  // 1 input + 1 output

    // Client1 closes output port (driver should NOT close because Client2 still has it open)
    EXPECT_CALL(*rawMockDriver_, CloseOutputPort(_, _)).Times(0);
    ASSERT_EQ(controller_->CloseOutputPort(clientId_, deviceId, outputPortIndex), OH_MIDI_STATUS_OK);
    EXPECT_EQ(controller_->GetOpenPortCountForTest(clientId_), 1);  // Now only 1 input
    EXPECT_EQ(controller_->GetOpenPortCountForTest(clientId2), 2);  // Still 1 input + 1 output

    // Client2 closes output port (driver close should be called now, all clients disconnected)
    EXPECT_CALL(*rawMockDriver_, CloseOutputPort(driverId, outputPortIndex)).WillOnce(Return(OH_MIDI_STATUS_OK));
    ASSERT_EQ(controller_->CloseOutputPort(clientId2, deviceId, outputPortIndex), OH_MIDI_STATUS_OK);
    EXPECT_EQ(controller_->GetOpenPortCountForTest(clientId2), 1);  // Now only 1 input

    // Client1 closes input port (driver should NOT close because Client2 still has it open)
    EXPECT_CALL(*rawMockDriver_, CloseInputPort(_, _)).Times(0);
    ASSERT_EQ(controller_->CloseInputPort(clientId_, deviceId, inputPortIndex), OH_MIDI_STATUS_OK);
    EXPECT_EQ(controller_->GetOpenPortCountForTest(clientId_), 0);  // All ports closed

    // Client2 closes input port (driver close should be called now, all clients disconnected)
    EXPECT_CALL(*rawMockDriver_, CloseInputPort(driverId, inputPortIndex)).WillOnce(Return(OH_MIDI_STATUS_OK));
    ASSERT_EQ(controller_->CloseInputPort(clientId2, deviceId, inputPortIndex), OH_MIDI_STATUS_OK);
    EXPECT_EQ(controller_->GetOpenPortCountForTest(clientId2), 0);  // All ports closed

    // Cleanup
    controller_->DestroyMidiClient(clientId2);
}

/**
 * @tc.name: CloseInputPort_PortNotOwnedByClient
 * @tc.desc: Verify that closing an input port the client never opened is rejected and does NOT
 *           corrupt the caller's openPortCount (previously the count was wrongly decremented)
 * @tc.type: FUNC
 * @tc.require: Bug fix - CloseInputPortInner() must verify the client owns the port
 */
HWTEST_F(MidiServiceControllerUnitTest, CloseInputPort_PortNotOwnedByClient, TestSize.Level0)
{
    int64_t driverId = 803;
    int64_t deviceId = SimulateDeviceConnection(driverId, "Mixed Owner Input Device");

    // Create a second client (MAX_CLIENTS_PER_APP = 2)
    uint32_t clientId2 = 0;
    sptr<IRemoteObject> clientObj;
    sptr<MockMidiCallbackStub> cb2 = new MockMidiCallbackStub();
    controller_->CreateMidiInServer(cb2->AsObject(), clientObj, clientId2);

    // Both clients open device (driver OpenDevice fires only for the first opener)
    EXPECT_CALL(*rawMockDriver_, OpenDevice(driverId)).WillOnce(Return(OH_MIDI_STATUS_OK));
    ASSERT_EQ(controller_->OpenDevice(clientId_, deviceId), OH_MIDI_STATUS_OK);
    ASSERT_EQ(controller_->OpenDevice(clientId2, deviceId), OH_MIDI_STATUS_OK);

    // client1 owns input port 0; client2 owns a different input port 1
    EXPECT_CALL(*rawMockDriver_, OpenInputPort(driverId, 0, _)).WillOnce(Return(OH_MIDI_STATUS_OK));
    EXPECT_CALL(*rawMockDriver_, OpenInputPort(driverId, 1, _)).WillOnce(Return(OH_MIDI_STATUS_OK));
    std::shared_ptr<MidiSharedRing> buffer1;
    std::shared_ptr<MidiSharedRing> buffer2;
    ASSERT_EQ(controller_->OpenInputPort(clientId_, buffer1, deviceId, 0), OH_MIDI_STATUS_OK);
    ASSERT_EQ(controller_->OpenInputPort(clientId2, buffer2, deviceId, 1), OH_MIDI_STATUS_OK);
    EXPECT_EQ(controller_->GetOpenPortCountForTest(clientId_), 1);
    EXPECT_EQ(controller_->GetOpenPortCountForTest(clientId2), 1);

    // BUG FIX VERIFICATION: client2 closes port 0 which it never opened. The call must be rejected
    // and openPortCount must NOT be decremented. Pre-fix, the count was corrupted (1 -> 0) even
    // though client2 still held port 1 open.
    EXPECT_EQ(controller_->CloseInputPort(clientId2, deviceId, 0), OH_MIDI_STATUS_INVALID_PORT);
    EXPECT_EQ(controller_->GetOpenPortCountForTest(clientId2), 1);  // unchanged - bug fix
    EXPECT_EQ(controller_->GetOpenPortCountForTest(clientId_), 1);  // client1 unaffected

    // client2 closes the port it actually owns -> succeeds; driver closes port 1 (last owner)
    EXPECT_CALL(*rawMockDriver_, CloseInputPort(driverId, 1)).WillOnce(Return(OH_MIDI_STATUS_OK));
    ASSERT_EQ(controller_->CloseInputPort(clientId2, deviceId, 1), OH_MIDI_STATUS_OK);
    EXPECT_EQ(controller_->GetOpenPortCountForTest(clientId2), 0);

    // client1 closes the port it owns -> succeeds; driver closes port 0 (last owner)
    EXPECT_CALL(*rawMockDriver_, CloseInputPort(driverId, 0)).WillOnce(Return(OH_MIDI_STATUS_OK));
    ASSERT_EQ(controller_->CloseInputPort(clientId_, deviceId, 0), OH_MIDI_STATUS_OK);
    EXPECT_EQ(controller_->GetOpenPortCountForTest(clientId_), 0);

    // Cleanup
    controller_->DestroyMidiClient(clientId2);
}

/**
 * @tc.name: CloseOutputPort_PortNotOwnedByClient
 * @tc.desc: Verify that closing an output port the client never opened is rejected and does NOT
 *           corrupt the caller's openPortCount
 * @tc.type: FUNC
 * @tc.require: Bug fix - CloseOutputPortInner() must verify the client owns the port
 */
HWTEST_F(MidiServiceControllerUnitTest, CloseOutputPort_PortNotOwnedByClient, TestSize.Level0)
{
    int64_t driverId = 804;
    int64_t deviceId = SimulateDeviceConnection(driverId, "Mixed Owner Output Device");

    // Create a second client (MAX_CLIENTS_PER_APP = 2)
    uint32_t clientId2 = 0;
    sptr<IRemoteObject> clientObj;
    sptr<MockMidiCallbackStub> cb2 = new MockMidiCallbackStub();
    controller_->CreateMidiInServer(cb2->AsObject(), clientObj, clientId2);

    EXPECT_CALL(*rawMockDriver_, OpenDevice(driverId)).WillOnce(Return(OH_MIDI_STATUS_OK));
    ASSERT_EQ(controller_->OpenDevice(clientId_, deviceId), OH_MIDI_STATUS_OK);
    ASSERT_EQ(controller_->OpenDevice(clientId2, deviceId), OH_MIDI_STATUS_OK);

    // client1 owns output port 0; client2 owns a different output port 1
    EXPECT_CALL(*rawMockDriver_, OpenOutputPort(driverId, 0)).WillOnce(Return(OH_MIDI_STATUS_OK));
    EXPECT_CALL(*rawMockDriver_, OpenOutputPort(driverId, 1)).WillOnce(Return(OH_MIDI_STATUS_OK));
    std::shared_ptr<MidiSharedRing> buffer1;
    std::shared_ptr<MidiSharedRing> buffer2;
    ASSERT_EQ(controller_->OpenOutputPort(clientId_, buffer1, deviceId, 0), OH_MIDI_STATUS_OK);
    ASSERT_EQ(controller_->OpenOutputPort(clientId2, buffer2, deviceId, 1), OH_MIDI_STATUS_OK);
    EXPECT_EQ(controller_->GetOpenPortCountForTest(clientId_), 1);
    EXPECT_EQ(controller_->GetOpenPortCountForTest(clientId2), 1);

    // BUG FIX VERIFICATION: client2 closes port 0 which it never opened -> rejected, count unchanged
    EXPECT_EQ(controller_->CloseOutputPort(clientId2, deviceId, 0), OH_MIDI_STATUS_INVALID_PORT);
    EXPECT_EQ(controller_->GetOpenPortCountForTest(clientId2), 1);  // unchanged - bug fix
    EXPECT_EQ(controller_->GetOpenPortCountForTest(clientId_), 1);  // client1 unaffected

    // client2 closes the port it actually owns -> succeeds
    EXPECT_CALL(*rawMockDriver_, CloseOutputPort(driverId, 1)).WillOnce(Return(OH_MIDI_STATUS_OK));
    ASSERT_EQ(controller_->CloseOutputPort(clientId2, deviceId, 1), OH_MIDI_STATUS_OK);
    EXPECT_EQ(controller_->GetOpenPortCountForTest(clientId2), 0);

    // client1 closes the port it owns -> succeeds
    EXPECT_CALL(*rawMockDriver_, CloseOutputPort(driverId, 0)).WillOnce(Return(OH_MIDI_STATUS_OK));
    ASSERT_EQ(controller_->CloseOutputPort(clientId_, deviceId, 0), OH_MIDI_STATUS_OK);
    EXPECT_EQ(controller_->GetOpenPortCountForTest(clientId_), 0);

    // Cleanup
    controller_->DestroyMidiClient(clientId2);
}

/**
 * @tc.name: CloseDevice_PortNotOwnedByClient
 * @tc.desc: Verify CloseDevice (which internally iterates all device ports via ClosePortforDevice)
 *           succeeds and does not corrupt counts when the device has ports owned by another client.
 *           The new guard early-returns for ports the closing client does not own; ClosePortforDevice
 *           ignores that return value and continues, so only the caller's own ports are released.
 * @tc.type: FUNC
 * @tc.require: Bug fix - CloseInputPortInner() guard must not break the CloseDevice cleanup path
 */
HWTEST_F(MidiServiceControllerUnitTest, CloseDevice_PortNotOwnedByClient, TestSize.Level0)
{
    int64_t driverId = 805;
    int64_t deviceId = SimulateDeviceConnection(driverId, "CloseDevice Mixed Owner Device");

    // Create a second client (MAX_CLIENTS_PER_APP = 2)
    uint32_t clientId2 = 0;
    sptr<IRemoteObject> clientObj;
    sptr<MockMidiCallbackStub> cb2 = new MockMidiCallbackStub();
    controller_->CreateMidiInServer(cb2->AsObject(), clientObj, clientId2);

    EXPECT_CALL(*rawMockDriver_, OpenDevice(driverId)).WillOnce(Return(OH_MIDI_STATUS_OK));
    ASSERT_EQ(controller_->OpenDevice(clientId_, deviceId), OH_MIDI_STATUS_OK);
    ASSERT_EQ(controller_->OpenDevice(clientId2, deviceId), OH_MIDI_STATUS_OK);

    // client1 owns input port 0; client2 owns a different input port 1
    EXPECT_CALL(*rawMockDriver_, OpenInputPort(driverId, 0, _)).WillOnce(Return(OH_MIDI_STATUS_OK));
    EXPECT_CALL(*rawMockDriver_, OpenInputPort(driverId, 1, _)).WillOnce(Return(OH_MIDI_STATUS_OK));
    std::shared_ptr<MidiSharedRing> buffer1;
    std::shared_ptr<MidiSharedRing> buffer2;
    ASSERT_EQ(controller_->OpenInputPort(clientId_, buffer1, deviceId, 0), OH_MIDI_STATUS_OK);
    ASSERT_EQ(controller_->OpenInputPort(clientId2, buffer2, deviceId, 1), OH_MIDI_STATUS_OK);
    EXPECT_EQ(controller_->GetOpenPortCountForTest(clientId_), 1);
    EXPECT_EQ(controller_->GetOpenPortCountForTest(clientId2), 1);

    // client1 closes the device. ClosePortforDevice iterates both ports {0, 1}; port 1 is NOT owned
    // by client1 -> new guard early-returns and the loop ignores it. Only port 0 (client1's) is
    // released at the driver level. client2's port 1 must remain untouched.
    EXPECT_CALL(*rawMockDriver_, CloseInputPort(driverId, 0)).WillOnce(Return(OH_MIDI_STATUS_OK));
    ASSERT_EQ(controller_->CloseDevice(clientId_, deviceId), OH_MIDI_STATUS_OK);
    EXPECT_EQ(controller_->GetOpenPortCountForTest(clientId_), 0);   // client1's port cleaned
    EXPECT_EQ(controller_->GetOpenPortCountForTest(clientId2), 1);   // client2 unaffected

    // client2 still owns port 1 -> normal close still works
    EXPECT_CALL(*rawMockDriver_, CloseInputPort(driverId, 1)).WillOnce(Return(OH_MIDI_STATUS_OK));
    ASSERT_EQ(controller_->CloseInputPort(clientId2, deviceId, 1), OH_MIDI_STATUS_OK);
    EXPECT_EQ(controller_->GetOpenPortCountForTest(clientId2), 0);

    // Cleanup
    controller_->DestroyMidiClient(clientId2);
}

/**
 * @tc.name: CreateClientValidationAndLimits001
 * @tc.desc: Verify invalid callback, death-recipient failure, ID rollover, and client limits
 * @tc.type: FUNC
 */
HWTEST_F(MidiServiceControllerUnitTest, CreateClientValidationAndLimits001, TestSize.Level0)
{
    sptr<IRemoteObject> clientObj;
    uint32_t newClientId = 0;
    sptr<IRemoteObject> nullObject;
    EXPECT_EQ(controller_->CreateMidiInServer(nullObject, clientObj, newClientId), OH_MIDI_STATUS_SYSTEM_ERROR);

    sptr<RecordingMidiDeviceOpenCallbackStub> wrongInterface =
        sptr<RecordingMidiDeviceOpenCallbackStub>::MakeSptr();
    ASSERT_NE(wrongInterface, nullptr);
    EXPECT_EQ(controller_->CreateMidiInServer(wrongInterface->AsObject(), clientObj, newClientId),
        OH_MIDI_STATUS_SYSTEM_ERROR);

    sptr<RejectDeathRecipientMidiCallbackStub> rejectingCallback =
        sptr<RejectDeathRecipientMidiCallbackStub>::MakeSptr();
    ASSERT_NE(rejectingCallback, nullptr);
    EXPECT_EQ(controller_->CreateMidiInServer(rejectingCallback->AsObject(), clientObj, newClientId),
        OH_MIDI_STATUS_SYSTEM_ERROR);

    MidiServiceController::currentClientId_.store(UINT32_MAX);
    sptr<MockMidiCallbackStub> secondCallback = new MockMidiCallbackStub();
    ASSERT_EQ(controller_->CreateMidiInServer(secondCallback->AsObject(), clientObj, newClientId), OH_MIDI_STATUS_OK);
    EXPECT_NE(newClientId, clientId_);

    sptr<MockMidiCallbackStub> thirdCallback = new MockMidiCallbackStub();
    uint32_t rejectedClientId = 0;
    EXPECT_EQ(controller_->CreateMidiInServer(thirdCallback->AsObject(), clientObj, rejectedClientId),
        OH_MIDI_STATUS_TOO_MANY_CLIENTS);
    ASSERT_EQ(controller_->DestroyMidiClient(newClientId), OH_MIDI_STATUS_OK);

    for (uint32_t id = 100; controller_->clients_.size() < MidiServiceController::MAX_CLIENTS; ++id) {
        controller_->clients_[id] = nullptr;
    }
    EXPECT_EQ(controller_->CreateMidiInServer(thirdCallback->AsObject(), clientObj, rejectedClientId),
        OH_MIDI_STATUS_TOO_MANY_CLIENTS);
}

/**
 * @tc.name: ResourceLimitBranches001
 * @tc.desc: Verify device and port limits, including defensive existing-port helpers
 * @tc.type: FUNC
 */
HWTEST_F(MidiServiceControllerUnitTest, ResourceLimitBranches001, TestSize.Level0)
{
    constexpr int64_t driverId = 1200;
    constexpr int64_t firstSyntheticDeviceId = 10000;
    const int64_t deviceId = SimulateDeviceConnection(driverId, "Limit Device");

    auto &resource = controller_->clientResourceInfo_[clientId_];
    for (uint32_t index = 0; index < MidiServiceController::MAX_DEVICES_PER_CLIENT; ++index) {
        resource.openDevices.insert(firstSyntheticDeviceId + index);
    }
    EXPECT_EQ(controller_->OpenDevice(clientId_, deviceId), OH_MIDI_STATUS_TOO_MANY_OPEN_DEVICES);
    resource.openDevices.clear();

    EXPECT_CALL(*rawMockDriver_, OpenDevice(driverId)).WillOnce(Return(OH_MIDI_STATUS_OK));
    ASSERT_EQ(controller_->OpenDevice(clientId_, deviceId), OH_MIDI_STATUS_OK);

    EXPECT_CALL(*rawMockDriver_, OpenInputPort(driverId, 0, _)).WillOnce(Return(OH_MIDI_STATUS_OK));
    std::shared_ptr<MidiSharedRing> inputBuffer;
    ASSERT_EQ(controller_->OpenInputPort(clientId_, inputBuffer, deviceId, 0), OH_MIDI_STATUS_OK);

    EXPECT_CALL(*rawMockDriver_, OpenOutputPort(driverId, 1)).WillOnce(Return(OH_MIDI_STATUS_OK));
    std::shared_ptr<MidiSharedRing> outputBuffer;
    ASSERT_EQ(controller_->OpenOutputPort(clientId_, outputBuffer, deviceId, 1), OH_MIDI_STATUS_OK);

    resource.openPortCount = MidiServiceController::MAX_PORTS_PER_CLIENT;
    EXPECT_EQ(controller_->OpenInputPort(clientId_, inputBuffer, deviceId, 2), OH_MIDI_STATUS_TOO_MANY_OPEN_PORTS);
    EXPECT_EQ(controller_->OpenOutputPort(clientId_, outputBuffer, deviceId, 2), OH_MIDI_STATUS_TOO_MANY_OPEN_PORTS);

    auto context = controller_->deviceClientContexts_.at(deviceId);
    MidiServiceController::ClientResourceInfo defensiveResource {};
    defensiveResource.openPortCount = MidiServiceController::MAX_PORTS_PER_CLIENT;
    std::shared_ptr<MidiSharedRing> defensiveBuffer;
    EXPECT_EQ(controller_->ConnectToExistingInputPort(
        999, deviceId, defensiveBuffer, context->inputDeviceconnections_.at(0), defensiveResource),
        OH_MIDI_STATUS_TOO_MANY_OPEN_PORTS);
    EXPECT_EQ(controller_->ConnectToExistingOutputPort(
        999, deviceId, defensiveBuffer, context->outputDeviceconnections_.at(1), defensiveResource),
        OH_MIDI_STATUS_TOO_MANY_OPEN_PORTS);
}

/**
 * @tc.name: DumpPopulatedState001
 * @tc.desc: Verify dump branches for multiple clients, active/inactive ports, and traffic totals
 * @tc.type: FUNC
 */
HWTEST_F(MidiServiceControllerUnitTest, DumpPopulatedState001, TestSize.Level0)
{
    constexpr int64_t driverId = 1300;
    const int64_t deviceId = SimulateDeviceConnection(driverId, "Dump Device");
    uint32_t clientId2 = 0;
    sptr<IRemoteObject> clientObj;
    sptr<MockMidiCallbackStub> callback2 = new MockMidiCallbackStub();
    ASSERT_EQ(controller_->CreateMidiInServer(callback2->AsObject(), clientObj, clientId2), OH_MIDI_STATUS_OK);

    EXPECT_CALL(*rawMockDriver_, OpenDevice(driverId)).WillOnce(Return(OH_MIDI_STATUS_OK));
    ASSERT_EQ(controller_->OpenDevice(clientId_, deviceId), OH_MIDI_STATUS_OK);
    ASSERT_EQ(controller_->OpenDevice(clientId2, deviceId), OH_MIDI_STATUS_OK);

    EXPECT_CALL(*rawMockDriver_, OpenInputPort(driverId, 0, _)).WillOnce(Return(OH_MIDI_STATUS_OK));
    std::shared_ptr<MidiSharedRing> inputBuffer1;
    std::shared_ptr<MidiSharedRing> inputBuffer2;
    ASSERT_EQ(controller_->OpenInputPort(clientId_, inputBuffer1, deviceId, 0), OH_MIDI_STATUS_OK);
    ASSERT_EQ(controller_->OpenInputPort(clientId2, inputBuffer2, deviceId, 0), OH_MIDI_STATUS_OK);

    EXPECT_CALL(*rawMockDriver_, OpenInputPort(driverId, 5, _)).WillOnce(Return(OH_MIDI_STATUS_OK));
    std::shared_ptr<MidiSharedRing> unnamedInputBuffer;
    ASSERT_EQ(controller_->OpenInputPort(clientId_, unnamedInputBuffer, deviceId, 5), OH_MIDI_STATUS_OK);

    EXPECT_CALL(*rawMockDriver_, OpenOutputPort(driverId, 1)).WillOnce(Return(OH_MIDI_STATUS_OK));
    std::shared_ptr<MidiSharedRing> outputBuffer1;
    std::shared_ptr<MidiSharedRing> outputBuffer2;
    ASSERT_EQ(controller_->OpenOutputPort(clientId_, outputBuffer1, deviceId, 1), OH_MIDI_STATUS_OK);
    ASSERT_EQ(controller_->OpenOutputPort(clientId2, outputBuffer2, deviceId, 1), OH_MIDI_STATUS_OK);

    auto context = controller_->deviceClientContexts_.at(deviceId);
    context->inputDeviceconnections_[99] = nullptr;
    context->outputDeviceconnections_[99] = nullptr;

    std::string dump;
    controller_->DumpClientInfo(dump);
    controller_->DumpDeviceOpenStatus(dump, -1);
    controller_->DumpDeviceOpenStatus(dump, deviceId);
    controller_->DumpPortMapping(dump);
    controller_->DumpStatistics(dump);
    controller_->DumpSingleClientInfo(dump, 99999);

    EXPECT_NE(dump.find("[Client Information]"), std::string::npos);
    EXPECT_NE(dump.find("Opened by 2 client(s)"), std::string::npos);
    EXPECT_NE(dump.find("inactive"), std::string::npos);
    EXPECT_NE(dump.find("System Totals"), std::string::npos);

    context->inputDeviceconnections_.erase(99);
    context->outputDeviceconnections_.erase(99);
    ASSERT_EQ(controller_->DestroyMidiClient(clientId2), OH_MIDI_STATUS_OK);
}

/**
 * @tc.name: BleQueueAndSuccessCompletion001
 * @tc.desc: Verify BLE pending queues, successful attach, and active-device reuse
 * @tc.type: FUNC
 */
HWTEST_F(MidiServiceControllerUnitTest, BleQueueAndSuccessCompletion001, TestSize.Level0)
{
    auto bleDriver = std::make_unique<NiceMock<MockMidiDeviceDriver>>();
    auto *rawBleDriver = bleDriver.get();
    controller_->GetDeviceManagerForTest()->InjectDriverForTest(
        DeviceType::DEVICE_TYPE_BLE, std::move(bleDriver));

    uint32_t clientId2 = 0;
    sptr<IRemoteObject> clientObj;
    sptr<MockMidiCallbackStub> callback2 = new MockMidiCallbackStub();
    ASSERT_EQ(controller_->CreateMidiInServer(callback2->AsObject(), clientObj, clientId2), OH_MIDI_STATUS_OK);

    sptr<RecordingMidiDeviceOpenCallbackStub> openCallback1 =
        sptr<RecordingMidiDeviceOpenCallbackStub>::MakeSptr();
    sptr<RecordingMidiDeviceOpenCallbackStub> openCallback2 =
        sptr<RecordingMidiDeviceOpenCallbackStub>::MakeSptr();
    ASSERT_NE(openCallback1, nullptr);
    ASSERT_NE(openCallback2, nullptr);

    BleDriverCallback successCallback;
    EXPECT_CALL(*rawBleDriver, OpenDevice(_, _))
        .WillOnce(DoAll(SaveArg<1>(&successCallback), Return(OH_MIDI_STATUS_OK)));
    const std::string address = "11:22:33:44:55:66";
    ASSERT_EQ(controller_->OpenBleDevice(clientId_, address, openCallback1->AsObject()), OH_MIDI_STATUS_OK);
    ASSERT_EQ(controller_->OpenBleDevice(clientId2, address, openCallback2->AsObject()), OH_MIDI_STATUS_OK);

    DeviceInformation bleDevice;
    bleDevice.midiDeviceInfo.driverDeviceId = 1400;
    bleDevice.midiDeviceInfo.deviceType = DeviceType::DEVICE_TYPE_BLE;
    bleDevice.midiDeviceInfo.deviceName = "BLE Dump Device";
    ASSERT_TRUE(static_cast<bool>(successCallback));
    successCallback(true, bleDevice);
    EXPECT_EQ(openCallback1->callCount, 1);
    EXPECT_EQ(openCallback2->callCount, 1);
    EXPECT_TRUE(openCallback1->lastSuccess);

    ASSERT_EQ(controller_->OpenBleDevice(clientId_, address, openCallback1->AsObject()), OH_MIDI_STATUS_OK);
    EXPECT_EQ(openCallback1->callCount, 2);
    ASSERT_EQ(controller_->DestroyMidiClient(clientId2), OH_MIDI_STATUS_OK);
}

/**
 * @tc.name: BleFailureCompletionAndCleanup001
 * @tc.desc: Verify BLE failure callback, immediate error, and stale pending-client cleanup
 * @tc.type: FUNC
 */
HWTEST_F(MidiServiceControllerUnitTest, BleFailureCompletionAndCleanup001, TestSize.Level0)
{
    auto bleDriver = std::make_unique<NiceMock<MockMidiDeviceDriver>>();
    auto *rawBleDriver = bleDriver.get();
    controller_->GetDeviceManagerForTest()->InjectDriverForTest(
        DeviceType::DEVICE_TYPE_BLE, std::move(bleDriver));
    sptr<RecordingMidiDeviceOpenCallbackStub> openCallback =
        sptr<RecordingMidiDeviceOpenCallbackStub>::MakeSptr();
    ASSERT_NE(openCallback, nullptr);
    BleDriverCallback failureCallback;
    EXPECT_CALL(*rawBleDriver, OpenDevice(_, _))
        .WillOnce(DoAll(SaveArg<1>(&failureCallback), Return(OH_MIDI_STATUS_OK)));
    ASSERT_EQ(controller_->OpenBleDevice(clientId_, "failure-address", openCallback->AsObject()),
        OH_MIDI_STATUS_OK);
    ASSERT_TRUE(static_cast<bool>(failureCallback));
    DeviceInformation failedBleDevice;
    failedBleDevice.midiDeviceInfo.driverDeviceId = 2400;
    failedBleDevice.midiDeviceInfo.deviceType = DeviceType::DEVICE_TYPE_BLE;
    failureCallback(false, failedBleDevice);
    EXPECT_EQ(openCallback->callCount, 1);
    EXPECT_FALSE(openCallback->lastSuccess);

    EXPECT_CALL(*rawBleDriver, OpenDevice(_, _)).WillOnce(Return(OH_MIDI_STATUS_SYSTEM_ERROR));
    EXPECT_EQ(controller_->OpenBleDevice(clientId_, "immediate-error", openCallback->AsObject()),
        OH_MIDI_STATUS_SYSTEM_ERROR);
    EXPECT_EQ(controller_->pendingBleConnections_.count("immediate-error"), 0);

    MidiDeviceInfo unused;
    controller_->activeBleDevices_["orphan-active"] = 99999;
    EXPECT_FALSE(controller_->TryAttachToActiveBleDevice(clientId_, "orphan-active", unused));
    controller_->activeBleDevices_.erase("orphan-active");

    std::list<PendingBleConnection> deadClients = {{99999, openCallback}};
    EXPECT_FALSE(controller_->RegisterBleDeviceForPendingClientsLocked("dead", 99998, deadClients));
    controller_->activeBleDevices_.erase("dead");

    controller_->pendingBleConnections_["null-callback"].push_back({clientId_, nullptr});
    controller_->HandleBleOpenComplete("null-callback", false, 0, unused);
}

/**
 * @tc.name: PortValidationMatrix001
 * @tc.desc: Verify invalid client, device ownership, and absent-port branches for port APIs
 * @tc.type: FUNC
 */
HWTEST_F(MidiServiceControllerUnitTest, PortValidationMatrix001, TestSize.Level0)
{
    std::shared_ptr<MidiSharedRing> buffer;
    constexpr uint32_t invalidClientId = 99999;
    constexpr int64_t deviceId = 3100;

    EXPECT_EQ(controller_->OpenInputPort(invalidClientId, buffer, deviceId, 0), OH_MIDI_STATUS_INVALID_CLIENT);
    EXPECT_EQ(controller_->OpenOutputPort(invalidClientId, buffer, deviceId, 0), OH_MIDI_STATUS_INVALID_CLIENT);
    EXPECT_EQ(controller_->FlushOutputPort(invalidClientId, deviceId, 0), OH_MIDI_STATUS_INVALID_CLIENT);
    EXPECT_EQ(controller_->CloseInputPort(invalidClientId, deviceId, 0), OH_MIDI_STATUS_INVALID_CLIENT);
    EXPECT_EQ(controller_->CloseOutputPort(invalidClientId, deviceId, 0), OH_MIDI_STATUS_INVALID_CLIENT);

    EXPECT_EQ(controller_->FlushOutputPort(clientId_, deviceId, 0), OH_MIDI_STATUS_INVALID_DEVICE_HANDLE);
    EXPECT_EQ(controller_->CloseInputPort(clientId_, deviceId, 0), OH_MIDI_STATUS_INVALID_DEVICE_HANDLE);
    EXPECT_EQ(controller_->CloseOutputPort(clientId_, deviceId, 0), OH_MIDI_STATUS_INVALID_DEVICE_HANDLE);

    auto context = std::make_shared<DeviceClientContext>(deviceId, std::unordered_set<int32_t>{invalidClientId});
    controller_->deviceClientContexts_[deviceId] = context;
    EXPECT_EQ(controller_->FlushOutputPort(clientId_, deviceId, 0), OH_MIDI_STATUS_GENERIC_INVALID_ARGUMENT);
    EXPECT_EQ(controller_->CloseInputPort(clientId_, deviceId, 0), OH_MIDI_STATUS_GENERIC_INVALID_ARGUMENT);
    EXPECT_EQ(controller_->CloseOutputPort(clientId_, deviceId, 0), OH_MIDI_STATUS_GENERIC_INVALID_ARGUMENT);

    context->clients.insert(clientId_);
    EXPECT_EQ(controller_->FlushOutputPort(clientId_, deviceId, 0), OH_MIDI_STATUS_OK);
    EXPECT_EQ(controller_->CloseInputPort(clientId_, deviceId, 0), OH_MIDI_STATUS_INVALID_PORT);
    EXPECT_EQ(controller_->CloseOutputPort(clientId_, deviceId, 0), OH_MIDI_STATUS_INVALID_PORT);
    context->clients.erase(invalidClientId);
    EXPECT_EQ(controller_->CloseDevice(clientId_, deviceId), OH_MIDI_STATUS_SYSTEM_ERROR);
}

/**
 * @tc.name: InternalResourceAndDeviceCleanupBranches001
 * @tc.desc: Verify resource and device cleanup with missing, retained, erased, and underflow-safe state
 * @tc.type: FUNC
 */
HWTEST_F(MidiServiceControllerUnitTest, InternalResourceAndDeviceCleanupBranches001, TestSize.Level0)
{
    constexpr uint32_t fakeClientId = 3200;
    constexpr uint32_t fakeUid = 4200;
    constexpr int64_t deviceId = 5200;

    controller_->CleanupClientResourceForDevice(fakeClientId, deviceId, 1);
    auto &resource = controller_->clientResourceInfo_[fakeClientId];
    resource.uid = fakeUid;
    resource.openDevices.insert(deviceId);
    resource.openPortCount = 1;
    controller_->CleanupClientResourceForDevice(fakeClientId, deviceId, 2);
    EXPECT_EQ(resource.openPortCount, 0);
    resource.openDevices.insert(deviceId);
    resource.openPortCount = 3;
    controller_->CleanupClientResourceForDevice(fakeClientId, deviceId, 1);
    EXPECT_EQ(resource.openPortCount, 2);

    controller_->CleanupDeviceForClient(fakeClientId, 99999);
    controller_->CleanupDeviceContext(99999);
    controller_->RemoveFromActiveBleDevices(99999);
    controller_->activeBleDevices_["kept"] = 1;
    controller_->activeBleDevices_["removed"] = deviceId;
    controller_->RemoveFromActiveBleDevices(deviceId);
    EXPECT_EQ(controller_->activeBleDevices_.count("removed"), 0);
    EXPECT_EQ(controller_->activeBleDevices_.count("kept"), 1);
    EXPECT_TRUE(controller_->IsBluetoothDevice(1));
    EXPECT_FALSE(controller_->IsBluetoothDevice(2));
}

/**
 * @tc.name: InternalPendingAndClientCleanupBranches001
 * @tc.desc: Verify pending BLE, app-client, null callback, and test-helper cleanup branches
 * @tc.type: FUNC
 */
HWTEST_F(MidiServiceControllerUnitTest, InternalPendingAndClientCleanupBranches001, TestSize.Level0)
{
    constexpr uint32_t fakeClientId = 3200;
    constexpr uint32_t retainedClientId = 3201;
    constexpr uint32_t fakeUid = 4200;
    controller_->pendingBleConnections_["erase"] = {
        {fakeClientId, nullptr},
    };
    controller_->pendingBleConnections_["retain"] = {
        {fakeClientId, nullptr},
        {retainedClientId, nullptr},
    };
    controller_->RemovePendingBleConnectionsForClient(fakeClientId);
    EXPECT_EQ(controller_->pendingBleConnections_.count("erase"), 0);
    ASSERT_EQ(controller_->pendingBleConnections_.count("retain"), 1);
    EXPECT_EQ(controller_->pendingBleConnections_.at("retain").size(), 1);

    controller_->appClientMap_[fakeUid] = {fakeClientId, retainedClientId};
    controller_->CleanupClientResources(fakeClientId, fakeUid);
    EXPECT_EQ(controller_->appClientMap_.at(fakeUid).count(fakeClientId), 0);
    controller_->CleanupClientResources(retainedClientId, fakeUid);
    EXPECT_EQ(controller_->appClientMap_.count(fakeUid), 0);
    controller_->CleanupClientResources(99998, 99998);

    controller_->clients_[fakeClientId] = nullptr;
    auto clients = controller_->CollectClientsToNotify();
    EXPECT_FALSE(clients.empty());
    controller_->NotifyError(OH_MIDI_STATUS_SYSTEM_ERROR);
    controller_->clients_.erase(fakeClientId);

    EXPECT_FALSE(controller_->HasDeviceContextForTest(99999));
    EXPECT_FALSE(controller_->HasClientForDeviceForTest(99999, clientId_));
    EXPECT_TRUE(controller_->GetOpenDevicesForTest(99999).empty());
    EXPECT_EQ(controller_->GetOpenPortCountForTest(99999), 0);
}

/**
 * @tc.name: DumpEmptyAndTrafficBranches001
 * @tc.desc: Verify empty port lists, named multi-client ports, and non-zero traffic aggregation
 * @tc.type: FUNC
 */
HWTEST_F(MidiServiceControllerUnitTest, DumpEmptyAndTrafficBranches001, TestSize.Level0)
{
    constexpr uint64_t inputEventCount = 2;
    constexpr uint64_t inputByteCount = 8;
    constexpr uint64_t outputEventCount = 3;
    constexpr uint64_t outputByteCount = 12;
    constexpr int64_t deviceId = 5300;
    auto context = std::make_shared<DeviceClientContext>(
        deviceId, std::unordered_set<int32_t>{static_cast<int32_t>(clientId_), 5301});
    controller_->deviceClientContexts_[deviceId] = context;

    std::string dump;
    controller_->DumpPortMapping(dump);
    EXPECT_NE(dump.find("(none)"), std::string::npos);

    DeviceConnectionInfo inputInfo{};
    inputInfo.deviceId = deviceId;
    inputInfo.direction = MidiPortDirection::INPUT;
    inputInfo.portIndex = 0;
    auto input = std::make_shared<DeviceConnectionForInput>(inputInfo);
    input->clients_.push_back(std::make_shared<ClientConnectionInServer>(clientId_, deviceId, 0));
    input->clients_.push_back(std::make_shared<ClientConnectionInServer>(5301, deviceId, 0));
    input->eventCount_.store(inputEventCount);
    input->byteCount_.store(inputByteCount);

    DeviceConnectionInfo outputInfo{};
    outputInfo.deviceId = deviceId;
    outputInfo.direction = MidiPortDirection::OUTPUT;
    outputInfo.portIndex = 1;
    auto output = std::make_shared<DeviceConnectionForOutput>(outputInfo);
    output->clients_.push_back(std::make_shared<ClientConnectionInServer>(clientId_, deviceId, 1));
    output->eventCount_.store(outputEventCount);
    output->byteCount_.store(outputByteCount);
    context->inputDeviceconnections_[0] = input;
    context->outputDeviceconnections_[1] = output;

    auto &resource = controller_->clientResourceInfo_[clientId_];
    resource.openDevices = {deviceId, deviceId + 1};
    controller_->DumpClientInfo(dump);
    controller_->DumpDeviceOpenStatus(dump, deviceId);
    controller_->DumpPortMapping(dump);
    controller_->DumpStatistics(dump);
    EXPECT_NE(dump.find("Total Input Events: 2"), std::string::npos);
    EXPECT_NE(dump.find("Total Output Events: 3"), std::string::npos);
    EXPECT_NE(dump.find(", "), std::string::npos);
}

/**
 * @tc.name: BleValidationAndLateCompletion001
 * @tc.desc: Verify callback casting, invalid clients, limits, and late BLE completion branches
 * @tc.type: FUNC
 */
HWTEST_F(MidiServiceControllerUnitTest, BleValidationAndLateCompletion001, TestSize.Level0)
{
    sptr<MockMidiCallbackStub> wrongCallback = new MockMidiCallbackStub();
    EXPECT_EQ(controller_->OpenBleDevice(clientId_, "wrong", wrongCallback->AsObject()),
        OH_MIDI_STATUS_SYSTEM_ERROR);

    sptr<RecordingMidiDeviceOpenCallbackStub> callback =
        sptr<RecordingMidiDeviceOpenCallbackStub>::MakeSptr();
    ASSERT_NE(callback, nullptr);
    EXPECT_EQ(controller_->OpenBleDevice(99999, "invalid-client", callback->AsObject()),
        OH_MIDI_STATUS_INVALID_CLIENT);

    auto &resource = controller_->clientResourceInfo_[clientId_];
    for (uint32_t index = 0; index < MidiServiceController::MAX_DEVICES_PER_CLIENT; ++index) {
        resource.openDevices.insert(6000 + index);
    }
    EXPECT_EQ(controller_->OpenBleDevice(clientId_, "limited", callback->AsObject()),
        OH_MIDI_STATUS_TOO_MANY_OPEN_DEVICES);
    resource.openDevices.clear();

    MidiDeviceInfo info;
    controller_->HandleBleOpenComplete("late-failure", false, 0, info);
    controller_->HandleBleOpenComplete("late-success", true, 99998, info);
    EXPECT_EQ(controller_->activeBleDevices_.count("late-success"), 1);
    controller_->activeBleDevices_.erase("late-success");
}

/**
 * @tc.name: ControllerDestructorBranches001
 * @tc.desc: Verify destructor cancellation and death-recipient cleanup for null and live callback objects
 * @tc.type: FUNC
 */
HWTEST_F(MidiServiceControllerUnitTest, ControllerDestructorBranches001, TestSize.Level0)
{
    constexpr int64_t unloadDelayMs = 10000;
    auto local = std::make_shared<MidiServiceController>();
    local->SetUnloadDelay(unloadDelayMs);
    local->ScheduleUnloadTask();

    sptr<MidiServiceDeathRecipient> recipient = new MidiServiceDeathRecipient(1);
    sptr<IRemoteObject> object = mockCallback_->AsObject();
    ASSERT_TRUE(object->AddDeathRecipient(recipient));
    local->clientCallbackObjects_[1] = object;
    local->deathRecipients_[1] = recipient;
    local->clientCallbackObjects_[2] = nullptr;
    local->deathRecipients_[2] = recipient;
    local.reset();
}

/**
 * @tc.name: UnloadTaskLifecycle001
 * @tc.desc: Verify rescheduling and cancellation of the delayed unload worker
 * @tc.type: FUNC
 */
HWTEST_F(MidiServiceControllerUnitTest, UnloadTaskLifecycle001, TestSize.Level0)
{
    constexpr int64_t unloadDelayMs = 10000;
    controller_->SetUnloadDelay(unloadDelayMs);
    controller_->ScheduleUnloadTask();
    controller_->ScheduleUnloadTask();
    controller_->CancelUnloadTask();
    EXPECT_FALSE(controller_->isUnloadPending_.load());
    controller_->SetUnloadDelay(0);
}

/**
 * @tc.name: RemainingLifecycleAndCleanupBranches001
 * @tc.desc: Verify expired death callbacks, sparse cleanup state, worker-less cancellation, and null manager cleanup.
 * @tc.type: FUNC
 */
HWTEST_F(MidiServiceControllerUnitTest, RemainingLifecycleAndCleanupBranches001, TestSize.Level0)
{
    sptr<MidiServiceDeathRecipient> retainedRecipient;
    {
        auto local = std::make_shared<MidiServiceController>();
        sptr<MockMidiCallbackStub> callback = new MockMidiCallbackStub();
        sptr<IRemoteObject> client;
        uint32_t clientId = 0;
        ASSERT_EQ(local->CreateMidiInServer(callback->AsObject(), client, clientId), OH_MIDI_STATUS_OK);
        retainedRecipient = local->deathRecipients_.at(clientId);
        local->clientCallbackObjects_[clientId + 1] = callback->AsObject();
        local.reset();
    }
    ASSERT_NE(retainedRecipient, nullptr);
    wptr<IRemoteObject> remote;
    retainedRecipient->OnRemoteDied(remote);

    {
        auto local = std::make_shared<MidiServiceController>();
        constexpr uint32_t sparseClientId = 8100;
        local->clients_[sparseClientId] = nullptr;
        EXPECT_EQ(local->DestroyMidiClient(sparseClientId), OH_MIDI_STATUS_OK);
    }

    {
        auto local = std::make_shared<MidiServiceController>();
        constexpr uint32_t sparseClientId = 8200;
        local->clients_[sparseClientId] = nullptr;
        local->deathRecipients_[sparseClientId] = new MidiServiceDeathRecipient(sparseClientId);
        EXPECT_EQ(local->DestroyMidiClient(sparseClientId), OH_MIDI_STATUS_OK);
    }

    {
        constexpr int64_t unloadDelayMs = 10000;
        auto local = std::make_shared<MidiServiceController>();
        local->isUnloadPending_.store(true);
        local->CancelUnloadTask();
        EXPECT_FALSE(local->isUnloadPending_.load());
        local->isUnloadPending_.store(true);
        local->SetUnloadDelay(unloadDelayMs);
        local->ScheduleUnloadTask();
        local->CancelUnloadTask();
    }

    {
        auto local = std::make_shared<MidiServiceController>();
        local->deviceManager_ = nullptr;
        local->ClearStateForTest();
    }
}

/**
 * @tc.name: RemainingConnectionCleanupBranches001
 * @tc.desc: Verify null, unmatched, and owned input/output connections during client cleanup.
 * @tc.type: FUNC
 */
HWTEST_F(MidiServiceControllerUnitTest, RemainingConnectionCleanupBranches001, TestSize.Level0)
{
    constexpr uint32_t targetClientId = 8300;
    constexpr uint32_t otherClientId = 8301;
    auto context = std::make_shared<DeviceClientContext>(
        8400, std::unordered_set<int32_t>{static_cast<int32_t>(targetClientId)});

    DeviceConnectionInfo inputInfo {};
    inputInfo.direction = MidiPortDirection::INPUT;
    auto ownedInput = std::make_shared<DeviceConnectionForInput>(inputInfo);
    ownedInput->clients_.push_back(std::make_shared<ClientConnectionInServer>(targetClientId, 1, 0));
    auto otherInput = std::make_shared<DeviceConnectionForInput>(inputInfo);
    otherInput->clients_.push_back(std::make_shared<ClientConnectionInServer>(otherClientId, 1, 0));
    context->inputDeviceconnections_[0] = nullptr;
    context->inputDeviceconnections_[1] = ownedInput;
    context->inputDeviceconnections_[2] = otherInput;

    DeviceConnectionInfo outputInfo {};
    outputInfo.direction = MidiPortDirection::OUTPUT;
    auto ownedOutput = std::make_shared<DeviceConnectionForOutput>(outputInfo);
    ownedOutput->clients_.push_back(std::make_shared<ClientConnectionInServer>(targetClientId, 1, 0));
    auto otherOutput = std::make_shared<DeviceConnectionForOutput>(outputInfo);
    otherOutput->clients_.push_back(std::make_shared<ClientConnectionInServer>(otherClientId, 1, 0));
    context->outputDeviceconnections_[0] = nullptr;
    context->outputDeviceconnections_[1] = ownedOutput;
    context->outputDeviceconnections_[2] = otherOutput;

    EXPECT_EQ(controller_->CalculateClientPortCountAndStopWorkers(targetClientId, context), 2);
}

/**
 * @tc.name: CloseDeviceActiveBleAlias001
 * @tc.desc: Verify closing the last client removes the matching active BLE address entry.
 * @tc.type: FUNC
 */
HWTEST_F(MidiServiceControllerUnitTest, CloseDeviceActiveBleAlias001, TestSize.Level0)
{
    constexpr int64_t driverId = 8500;
    const int64_t deviceId = SimulateDeviceConnection(driverId, "Aliased Device");
    EXPECT_CALL(*rawMockDriver_, OpenDevice(driverId)).WillOnce(Return(OH_MIDI_STATUS_OK));
    ASSERT_EQ(controller_->OpenDevice(clientId_, deviceId), OH_MIDI_STATUS_OK);
    controller_->activeBleDevices_["alias"] = deviceId;

    EXPECT_CALL(*rawMockDriver_, CloseDevice(driverId)).WillOnce(Return(OH_MIDI_STATUS_OK));
    EXPECT_EQ(controller_->CloseDevice(clientId_, deviceId), OH_MIDI_STATUS_OK);
    EXPECT_EQ(controller_->activeBleDevices_.count("alias"), 0);
}
