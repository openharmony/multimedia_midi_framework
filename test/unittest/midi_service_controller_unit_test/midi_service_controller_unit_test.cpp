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
#include "midi_service_controller.h"
#include "midi_test_common.h"
#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace OHOS;
using namespace MIDI;
using namespace testing;
using namespace testing::ext;

class MidiServiceControllerUnitTest : public testing::Test {
public:
    void SetUp() override
    {
        controller_ = MidiServiceController::GetInstance();
        controller_->Init();
        mockDriver_ = std::make_unique<MockMidiDeviceDriver>();
        rawMockDriver_ = mockDriver_.get();
        controller_->deviceManager_->drivers_.clear();
        controller_->deviceManager_->drivers_.emplace(DeviceType::DEVICE_TYPE_USB, std::move(mockDriver_));
        mockCallback_ = new MockMidiCallbackStub();
        sptr<IRemoteObject> clientObj;
        controller_->CreateMidiInServer(mockCallback_->AsObject(), clientObj, clientId_);
    }

    void TearDown() override
    {
        controller_->DestroyMidiClient(clientId_);
        controller_->deviceManager_->devices_.clear();
        controller_->deviceManager_->driverIdToMidiId_.clear();
        controller_->deviceManager_->drivers_.clear();
    }

    /**
     * Helper to simulate a device being connected and discovered by the manager
     */
    int64_t SimulateDeviceConnection(int64_t driverId, const std::string &name)
    {
        DeviceInformation info;
        info.driverDeviceId = driverId;
        info.deviceType = DeviceType::DEVICE_TYPE_USB;
        info.productName = name;
        info.vendorName = "Test";
        info.transportProtocol = TransportProtocol::PROTOCOL_1_0;

        // Port info
        PortInformation port;
        port.portId = 0;
        port.direction = PortDirection::PORT_DIRECTION_INPUT;
        port.name = "Test Port";
        info.portInfos.push_back(port);

        std::vector<DeviceInformation> devices = {info};

        EXPECT_CALL(*rawMockDriver_, GetRegisteredDevices()).WillOnce(Return(devices));

        controller_->deviceManager_->UpdateDevices();

        auto allDevices = controller_->deviceManager_->GetDevices();
        if (allDevices.empty()) {
            return -1;
        }
        return allDevices[0].deviceId;
    }

protected:
    std::shared_ptr<MidiServiceController> controller_ = nullptr;
    MockMidiDeviceDriver *rawMockDriver_ = nullptr;
    std::unique_ptr<MockMidiDeviceDriver> mockDriver_;
    sptr<MockMidiCallbackStub> mockCallback_;
    uint32_t clientId_ = 0;
};

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
    EXPECT_EQ(ret, MIDI_STATUS_OK);
    EXPECT_GT(newClientId, 0);
    EXPECT_NE(newClientId, clientId_);
    ret = controller_->DestroyMidiClient(newClientId);
    EXPECT_EQ(ret, MIDI_STATUS_OK);
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
    EXPECT_EQ(ret, MIDI_STATUS_INVALID_CLIENT);
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

    EXPECT_EQ(result[0][DEVICE_ID], std::to_string(deviceId));
    EXPECT_EQ(result[0][DEVICE_TYPE], std::to_string(DeviceType::DEVICE_TYPE_USB));
    EXPECT_EQ(result[0][MIDI_PROTOCOL], std::to_string(TransportProtocol::PROTOCOL_1_0));
    EXPECT_EQ(result[0][PRODUCT_NAME], "Yamaha Keyboard");
    EXPECT_EQ(result[0][VENDOR_NAME], "Test");
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

    EXPECT_CALL(*rawMockDriver_, OpenDevice(driverId)).WillOnce(Return(MIDI_STATUS_OK));

    int32_t ret = controller_->OpenDevice(clientId_, deviceId);
    EXPECT_EQ(ret, MIDI_STATUS_OK);
    auto it = controller_->deviceClientContexts_.find(deviceId);
    ASSERT_NE(it, controller_->deviceClientContexts_.end());
    EXPECT_NE(it->second->clients.find(clientId_), it->second->clients.end());
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
    EXPECT_NE(ret, MIDI_STATUS_OK);
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
    EXPECT_CALL(*rawMockDriver_, OpenDevice(driverId)).WillOnce(Return(MIDI_STATUS_UNKNOWN_ERROR));

    int32_t ret = controller_->OpenDevice(clientId_, deviceId);
    EXPECT_EQ(ret, MIDI_STATUS_UNKNOWN_ERROR);
    auto it = controller_->deviceClientContexts_.find(deviceId);
    EXPECT_EQ(it, controller_->deviceClientContexts_.end());
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

    EXPECT_CALL(*rawMockDriver_, OpenDevice(driverId)).WillOnce(Return(MIDI_STATUS_OK));

    // First Open
    ASSERT_EQ(controller_->OpenDevice(clientId_, deviceId), MIDI_STATUS_OK);

    // Second Open (Same Client)
    int32_t ret = controller_->OpenDevice(clientId_, deviceId);
    EXPECT_EQ(ret, MIDI_STATUS_DEVICE_ALREADY_OPEN);
    auto it = controller_->deviceClientContexts_.find(deviceId);
    ASSERT_NE(it, controller_->deviceClientContexts_.end());
    EXPECT_NE(it->second->clients.find(clientId_), it->second->clients.end());
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

    EXPECT_CALL(*rawMockDriver_, OpenDevice(driverId)).WillOnce(Return(MIDI_STATUS_OK));

    EXPECT_EQ(controller_->OpenDevice(clientId_, deviceId), MIDI_STATUS_OK);

    EXPECT_EQ(controller_->OpenDevice(clientId2, deviceId), MIDI_STATUS_OK);
    auto it = controller_->deviceClientContexts_.find(deviceId);
    ASSERT_NE(it, controller_->deviceClientContexts_.end());
    EXPECT_NE(it->second->clients.find(clientId_), it->second->clients.end());
    EXPECT_NE(it->second->clients.find(clientId2), it->second->clients.end());
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
    EXPECT_EQ(ret, MIDI_STATUS_INVALID_CLIENT);
    auto it = controller_->deviceClientContexts_.find(deviceId);
    EXPECT_EQ(it, controller_->deviceClientContexts_.end());
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

    EXPECT_CALL(*rawMockDriver_, OpenDevice(driverId)).WillOnce(Return(MIDI_STATUS_OK));
    controller_->OpenDevice(clientId_, deviceId);

    EXPECT_CALL(*rawMockDriver_, CloseDevice(driverId)).WillOnce(Return(MIDI_STATUS_OK));

    int32_t ret = controller_->CloseDevice(clientId_, deviceId);
    EXPECT_EQ(ret, MIDI_STATUS_OK);
    auto it = controller_->deviceClientContexts_.find(deviceId);
    EXPECT_EQ(it, controller_->deviceClientContexts_.end());
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
    EXPECT_NE(ret, MIDI_STATUS_OK);
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

    EXPECT_CALL(*rawMockDriver_, OpenDevice(driverId)).WillOnce(Return(MIDI_STATUS_OK));

    controller_->OpenDevice(clientId_, deviceId);
    controller_->OpenDevice(clientId2, deviceId);

    int32_t ret = controller_->CloseDevice(clientId_, deviceId);
    EXPECT_EQ(ret, MIDI_STATUS_OK);
    auto it = controller_->deviceClientContexts_.find(deviceId);
    ASSERT_NE(it, controller_->deviceClientContexts_.end());
    EXPECT_EQ(it->second->clients.find(clientId_), it->second->clients.end());
    EXPECT_NE(it->second->clients.find(clientId2), it->second->clients.end());

    EXPECT_CALL(*rawMockDriver_, CloseDevice(driverId)).WillOnce(Return(MIDI_STATUS_OK));

    ret = controller_->CloseDevice(clientId2, deviceId);
    EXPECT_EQ(ret, MIDI_STATUS_OK);
    auto it2 = controller_->deviceClientContexts_.find(deviceId);
    EXPECT_EQ(it2, controller_->deviceClientContexts_.end());
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

    EXPECT_CALL(*rawMockDriver_, OpenDevice(driverId)).WillOnce(Return(MIDI_STATUS_OK));
    controller_->OpenDevice(clientId_, deviceId);

    EXPECT_CALL(*rawMockDriver_, OpenInputPort(driverId, portIndex, _)).WillOnce(Return(MIDI_STATUS_OK));

    std::shared_ptr<MidiSharedRing> buffer;
    int32_t ret = controller_->OpenInputPort(clientId_, buffer, deviceId, portIndex);
    EXPECT_EQ(ret, MIDI_STATUS_OK);
    auto it = controller_->deviceClientContexts_.find(deviceId);
    auto &inputPortConnections = it->second->inputDeviceconnections_;
    auto inputPort = inputPortConnections.find(portIndex);
    EXPECT_NE(inputPort, inputPortConnections.end());
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
    EXPECT_NE(ret, MIDI_STATUS_OK);
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

    EXPECT_CALL(*rawMockDriver_, OpenDevice(driverId)).WillOnce(Return(MIDI_STATUS_OK));
    controller_->OpenDevice(clientId_, deviceId);

    EXPECT_CALL(*rawMockDriver_, OpenInputPort(driverId, portIndex, _)).WillOnce(Return(MIDI_STATUS_OK));

    std::shared_ptr<MidiSharedRing> buffer;
    int32_t ret = controller_->OpenInputPort(clientId_, buffer, deviceId, portIndex);
    EXPECT_EQ(ret, MIDI_STATUS_OK);
    std::shared_ptr<MidiSharedRing> buffer2;
    ret = controller_->OpenInputPort(clientId2, buffer2, deviceId, portIndex);
    EXPECT_EQ(ret, MIDI_STATUS_UNKNOWN_ERROR);
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

    EXPECT_CALL(*rawMockDriver_, OpenDevice(driverId)).WillOnce(Return(MIDI_STATUS_OK));
    controller_->OpenDevice(clientId_, deviceId);
    controller_->OpenDevice(clientId2, deviceId);

    EXPECT_CALL(*rawMockDriver_, OpenInputPort(driverId, portIndex, _)).WillOnce(Return(MIDI_STATUS_OK));

    std::shared_ptr<MidiSharedRing> buffer;
    int32_t ret = controller_->OpenInputPort(clientId_, buffer, deviceId, portIndex);
    EXPECT_EQ(ret, MIDI_STATUS_OK);
    std::shared_ptr<MidiSharedRing> buffer2;
    ret = controller_->OpenInputPort(clientId2, buffer2, deviceId, portIndex);
    EXPECT_EQ(ret, MIDI_STATUS_OK);
    auto it = controller_->deviceClientContexts_.find(deviceId);
    auto &inputPortConnections = it->second->inputDeviceconnections_;
    auto inputPort = inputPortConnections.find(portIndex);
    EXPECT_NE(inputPort, inputPortConnections.end());
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
    EXPECT_CALL(*rawMockDriver_, OpenDevice(driverId)).WillOnce(Return(MIDI_STATUS_OK));
    controller_->OpenDevice(clientId_, deviceId);

    EXPECT_CALL(*rawMockDriver_, OpenInputPort(driverId, portIndex, _)).WillOnce(Return(MIDI_STATUS_OK));
    std::shared_ptr<MidiSharedRing> buffer;
    controller_->OpenInputPort(clientId_, buffer, deviceId, portIndex);

    EXPECT_CALL(*rawMockDriver_, CloseInputPort(driverId, portIndex)).WillOnce(Return(MIDI_STATUS_OK));

    int32_t ret = controller_->CloseInputPort(clientId_, deviceId, portIndex);
    EXPECT_EQ(ret, MIDI_STATUS_OK);
    auto it = controller_->deviceClientContexts_.find(deviceId);
    auto &inputPortConnections = it->second->inputDeviceconnections_;
    auto inputPort = inputPortConnections.find(portIndex);
    EXPECT_EQ(inputPort, inputPortConnections.end());
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
    EXPECT_CALL(*rawMockDriver_, OpenDevice(driverId)).WillOnce(Return(MIDI_STATUS_OK));
    controller_->OpenDevice(clientId_, deviceId);
    controller_->OpenDevice(clientId2, deviceId);

    EXPECT_CALL(*rawMockDriver_, OpenInputPort(driverId, portIndex, _)).WillOnce(Return(MIDI_STATUS_OK));
    std::shared_ptr<MidiSharedRing> buffer;
    controller_->OpenInputPort(clientId_, buffer, deviceId, portIndex);
    std::shared_ptr<MidiSharedRing> buffer2;
    int32_t ret = controller_->OpenInputPort(clientId2, buffer2, deviceId, portIndex);
    ret = controller_->CloseInputPort(clientId_, deviceId, portIndex);
    EXPECT_EQ(ret, MIDI_STATUS_OK);
    auto it = controller_->deviceClientContexts_.find(deviceId);
    auto &inputPortConnections = it->second->inputDeviceconnections_;
    auto inputPort = inputPortConnections.find(portIndex);
    EXPECT_NE(inputPort, inputPortConnections.end());
    EXPECT_CALL(*rawMockDriver_, CloseInputPort(driverId, portIndex)).WillOnce(Return(MIDI_STATUS_OK));
    ret = controller_->CloseInputPort(clientId2, deviceId, portIndex);
    EXPECT_EQ(ret, MIDI_STATUS_OK);

    auto it2 = controller_->deviceClientContexts_.find(deviceId);
    auto &inputPortConnections2 = it2->second->inputDeviceconnections_;
    auto inputPort2 = inputPortConnections2.find(portIndex);
    EXPECT_EQ(inputPort2, inputPortConnections2.end());
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
    EXPECT_CALL(*rawMockDriver_, OpenDevice(driverId)).WillOnce(Return(MIDI_STATUS_OK));
    EXPECT_CALL(*rawMockDriver_, OpenInputPort(driverId, portIndex, _)).WillOnce(Return(MIDI_STATUS_OK));

    controller_->OpenDevice(clientId_, deviceId);
    std::shared_ptr<MidiSharedRing> buffer = std::make_shared<MidiSharedRing>(2048);
    controller_->OpenInputPort(clientId_, buffer, deviceId, portIndex);

    EXPECT_CALL(*rawMockDriver_, CloseInputPort(driverId, portIndex)).WillOnce(Return(MIDI_STATUS_OK));
    EXPECT_CALL(*rawMockDriver_, CloseDevice(driverId)).WillOnce(Return(MIDI_STATUS_OK));
    int32_t ret = controller_->DestroyMidiClient(clientId_);
    EXPECT_EQ(ret, MIDI_STATUS_OK);
}

/**
 * @tc.name: MaxClientsPerApp001
 * @tc.desc: Verify that an application can create up to 2 clients
 * @tc.type: FUNC
 */
HWTEST_F(MidiServiceControllerUnitTest, MaxClientsPerApp001, TestSize.Level0)
{
    // Create second client from same app (should succeed)
    uint32_t clientId2 = 0;
    sptr<IRemoteObject> clientObj2;
    sptr<MockMidiCallbackStub> cb2 = new MockMidiCallbackStub();
    int32_t ret = controller_->CreateMidiInServer(cb2->AsObject(), clientObj2, clientId2);
    EXPECT_EQ(ret, MIDI_STATUS_OK);
    EXPECT_GT(clientId2, 0);
    EXPECT_NE(clientId2, clientId_);

    // Clean up second client
    controller_->DestroyMidiClient(clientId2);
}

/**
 * @tc.name: MaxClientsPerApp002
 * @tc.desc: Verify that creating a 3rd client from the same app fails
 * @tc.type: FUNC
 */
HWTEST_F(MidiServiceControllerUnitTest, MaxClientsPerApp002, TestSize.Level0)
{
    // Create second client from same app (should succeed)
    uint32_t clientId2 = 0;
    sptr<IRemoteObject> clientObj2;
    sptr<MockMidiCallbackStub> cb2 = new MockMidiCallbackStub();
    ASSERT_EQ(controller_->CreateMidiInServer(cb2->AsObject(), clientObj2, clientId2), MIDI_STATUS_OK);

    // Create third client from same app (should fail)
    uint32_t clientId3 = 0;
    sptr<IRemoteObject> clientObj3;
    sptr<MockMidiCallbackStub> cb3 = new MockMidiCallbackStub();
    int32_t ret = controller_->CreateMidiInServer(cb3->AsObject(), clientObj3, clientId3);
    EXPECT_EQ(ret, MIDI_STATUS_TOO_MANY_CLIENTS);

    // Clean up second client
    controller_->DestroyMidiClient(clientId2);
}

/**
 * @tc.name: MaxClientsPerApp003
 * @tc.desc: Verify that after destroying a client, a new one can be created
 * @tc.type: FUNC
 */
HWTEST_F(MidiServiceControllerUnitTest, MaxClientsPerApp003, TestSize.Level0)
{
    // Create second client from same app (should succeed)
    uint32_t clientId2 = 0;
    sptr<IRemoteObject> clientObj2;
    sptr<MockMidiCallbackStub> cb2 = new MockMidiCallbackStub();
    ASSERT_EQ(controller_->CreateMidiInServer(cb2->AsObject(), clientObj2, clientId2), MIDI_STATUS_OK);

    // Destroy second client
    ASSERT_EQ(controller_->DestroyMidiClient(clientId2), MIDI_STATUS_OK);

    // Now creating a third client should succeed
    uint32_t clientId3 = 0;
    sptr<IRemoteObject> clientObj3;
    sptr<MockMidiCallbackStub> cb3 = new MockMidiCallbackStub();
    int32_t ret = controller_->CreateMidiInServer(cb3->AsObject(), clientObj3, clientId3);
    EXPECT_EQ(ret, MIDI_STATUS_OK);
    EXPECT_GT(clientId3, 0);

    // Clean up
    controller_->DestroyMidiClient(clientId3);
}

/**
 * @tc.name: MaxDevicesPerClient001
 * @tc.desc: Verify that a client can open up to 16 devices
 * @tc.type: FUNC
 */
HWTEST_F(MidiServiceControllerUnitTest, MaxDevicesPerClient001, TestSize.Level0)
{
    std::vector<int64_t> deviceIds;
    std::vector<int64_t> driverIds;

    // Open 16 devices
    for (int i = 0; i < 16; i++) {
        int64_t driverId = 1000 + i;
        driverIds.push_back(driverId);
        int64_t deviceId = SimulateDeviceConnection(driverId, "Device " + std::to_string(i));
        ASSERT_GT(deviceId, 0);
        deviceIds.push_back(deviceId);

        EXPECT_CALL(*rawMockDriver_, OpenDevice(driverId)).WillOnce(Return(MIDI_STATUS_OK));
        ASSERT_EQ(controller_->OpenDevice(clientId_, deviceId), MIDI_STATUS_OK);
    }

    // Verify all devices are tracked
    auto &resourceInfo = controller_->clientResourceInfo_[clientId_];
    EXPECT_EQ(resourceInfo.openDevices.size(), 16);

    // Clean up
    for (int i = 0; i < 16; i++) {
        EXPECT_CALL(*rawMockDriver_, CloseDevice(driverIds[i])).WillOnce(Return(MIDI_STATUS_OK));
        controller_->CloseDevice(clientId_, deviceIds[i]);
    }
}

/**
 * @tc.name: MaxDevicesPerClient002
 * @tc.desc: Verify that opening a 17th device fails
 * @tc.type: FUNC
 */
HWTEST_F(MidiServiceControllerUnitTest, MaxDevicesPerClient002, TestSize.Level0)
{
    std::vector<int64_t> deviceIds;
    std::vector<int64_t> driverIds;

    // Open 16 devices
    for (int i = 0; i < 16; i++) {
        int64_t driverId = 2000 + i;
        driverIds.push_back(driverId);
        int64_t deviceId = SimulateDeviceConnection(driverId, "Device " + std::to_string(i));
        deviceIds.push_back(deviceId);

        EXPECT_CALL(*rawMockDriver_, OpenDevice(driverId)).WillOnce(Return(MIDI_STATUS_OK));
        ASSERT_EQ(controller_->OpenDevice(clientId_, deviceId), MIDI_STATUS_OK);
    }

    // Try to open 17th device (should fail)
    int64_t driverId17 = 2016;
    int64_t deviceId17 = SimulateDeviceConnection(driverId17, "Device 17");
    int32_t ret = controller_->OpenDevice(clientId_, deviceId17);
    EXPECT_EQ(ret, MIDI_STATUS_TOO_MANY_OPEN_DEVICES);

    // Clean up
    for (int i = 0; i < 16; i++) {
        EXPECT_CALL(*rawMockDriver_, CloseDevice(driverIds[i])).WillOnce(Return(MIDI_STATUS_OK));
        controller_->CloseDevice(clientId_, deviceIds[i]);
    }
}

/**
 * @tc.name: MaxDevicesPerClient003
 * @tc.desc: Verify that after closing a device, a new one can be opened
 * @tc.type: FUNC
 */
HWTEST_F(MidiServiceControllerUnitTest, MaxDevicesPerClient003, TestSize.Level0)
{
    std::vector<int64_t> deviceIds;
    std::vector<int64_t> driverIds;

    // Open 16 devices
    for (int i = 0; i < 16; i++) {
        int64_t driverId = 3000 + i;
        driverIds.push_back(driverId);
        int64_t deviceId = SimulateDeviceConnection(driverId, "Device " + std::to_string(i));
        deviceIds.push_back(deviceId);

        EXPECT_CALL(*rawMockDriver_, OpenDevice(driverId)).WillOnce(Return(MIDI_STATUS_OK));
        ASSERT_EQ(controller_->OpenDevice(clientId_, deviceId), MIDI_STATUS_OK);
    }

    // Close one device
    EXPECT_CALL(*rawMockDriver_, CloseDevice(driverIds[0])).WillOnce(Return(MIDI_STATUS_OK));
    ASSERT_EQ(controller_->CloseDevice(clientId_, deviceIds[0]), MIDI_STATUS_OK);

    // Now opening a 17th device should succeed
    int64_t driverId17 = 3016;
    int64_t deviceId17 = SimulateDeviceConnection(driverId17, "Device 17");
    EXPECT_CALL(*rawMockDriver_, OpenDevice(driverId17)).WillOnce(Return(MIDI_STATUS_OK));
    int32_t ret = controller_->OpenDevice(clientId_, deviceId17);
    EXPECT_EQ(ret, MIDI_STATUS_OK);

    // Clean up
    for (int i = 1; i < 16; i++) {
        EXPECT_CALL(*rawMockDriver_, CloseDevice(driverIds[i])).WillOnce(Return(MIDI_STATUS_OK));
        controller_->CloseDevice(clientId_, deviceIds[i]);
    }
    EXPECT_CALL(*rawMockDriver_, CloseDevice(driverId17)).WillOnce(Return(MIDI_STATUS_OK));
    controller_->CloseDevice(clientId_, deviceId17);
}

/**
 * @tc.name: MaxPortsPerClient001
 * @tc.desc: Verify that a client can open up to 64 ports
 * @tc.type: FUNC
 */
HWTEST_F(MidiServiceControllerUnitTest, MaxPortsPerClient001, TestSize.Level0)
{
    int64_t driverId = 4000;
    int64_t deviceId = SimulateDeviceConnection(driverId, "MultiPort Device");

    EXPECT_CALL(*rawMockDriver_, OpenDevice(driverId)).WillOnce(Return(MIDI_STATUS_OK));
    ASSERT_EQ(controller_->OpenDevice(clientId_, deviceId), MIDI_STATUS_OK);

    // Open 64 input ports
    for (uint32_t i = 0; i < 64; i++) {
        EXPECT_CALL(*rawMockDriver_, OpenInputPort(driverId, i, _)).WillOnce(Return(MIDI_STATUS_OK));
        std::shared_ptr<MidiSharedRing> buffer;
        ASSERT_EQ(controller_->OpenInputPort(clientId_, buffer, deviceId, i), MIDI_STATUS_OK);
    }

    // Verify port count
    auto &resourceInfo = controller_->clientResourceInfo_[clientId_];
    EXPECT_EQ(resourceInfo.openPortCount, 64);

    // Clean up
    for (uint32_t i = 0; i < 64; i++) {
        EXPECT_CALL(*rawMockDriver_, CloseInputPort(driverId, i)).WillOnce(Return(MIDI_STATUS_OK));
        controller_->CloseInputPort(clientId_, deviceId, i);
    }
    EXPECT_CALL(*rawMockDriver_, CloseDevice(driverId)).WillOnce(Return(MIDI_STATUS_OK));
    controller_->CloseDevice(clientId_, deviceId);
}

/**
 * @tc.name: MaxPortsPerClient002
 * @tc.desc: Verify that opening a 65th port fails
 * @tc.type: FUNC
 */
HWTEST_F(MidiServiceControllerUnitTest, MaxPortsPerClient002, TestSize.Level0)
{
    int64_t driverId = 5000;
    int64_t deviceId = SimulateDeviceConnection(driverId, "MultiPort Device");

    EXPECT_CALL(*rawMockDriver_, OpenDevice(driverId)).WillOnce(Return(MIDI_STATUS_OK));
    ASSERT_EQ(controller_->OpenDevice(clientId_, deviceId), MIDI_STATUS_OK);

    // Open 64 input ports
    for (uint32_t i = 0; i < 64; i++) {
        EXPECT_CALL(*rawMockDriver_, OpenInputPort(driverId, i, _)).WillOnce(Return(MIDI_STATUS_OK));
        std::shared_ptr<MidiSharedRing> buffer;
        ASSERT_EQ(controller_->OpenInputPort(clientId_, buffer, deviceId, i), MIDI_STATUS_OK);
    }

    // Try to open 65th port (should fail)
    EXPECT_CALL(*rawMockDriver_, OpenInputPort(driverId, 64, _)).Times(0);
    std::shared_ptr<MidiSharedRing> buffer;
    int32_t ret = controller_->OpenInputPort(clientId_, buffer, deviceId, 64);
    EXPECT_EQ(ret, MIDI_STATUS_TOO_MANY_OPEN_PORTS);

    // Clean up
    for (uint32_t i = 0; i < 64; i++) {
        EXPECT_CALL(*rawMockDriver_, CloseInputPort(driverId, i)).WillOnce(Return(MIDI_STATUS_OK));
        controller_->CloseInputPort(clientId_, deviceId, i);
    }
    EXPECT_CALL(*rawMockDriver_, CloseDevice(driverId)).WillOnce(Return(MIDI_STATUS_OK));
    controller_->CloseDevice(clientId_, deviceId);
}

/**
 * @tc.name: MaxPortsPerClient003
 * @tc.desc: Verify that port count includes both input and output ports
 * @tc.type: FUNC
 */
HWTEST_F(MidiServiceControllerUnitTest, MaxPortsPerClient003, TestSize.Level0)
{
    int64_t driverId = 6000;
    int64_t deviceId = SimulateDeviceConnection(driverId, "Mixed Device");

    EXPECT_CALL(*rawMockDriver_, OpenDevice(driverId)).WillOnce(Return(MIDI_STATUS_OK));
    ASSERT_EQ(controller_->OpenDevice(clientId_, deviceId), MIDI_STATUS_OK);

    // Open 32 input ports and 32 output ports
    for (uint32_t i = 0; i < 32; i++) {
        EXPECT_CALL(*rawMockDriver_, OpenInputPort(driverId, i, _)).WillOnce(Return(MIDI_STATUS_OK));
        std::shared_ptr<MidiSharedRing> buffer;
        ASSERT_EQ(controller_->OpenInputPort(clientId_, buffer, deviceId, i), MIDI_STATUS_OK);

        EXPECT_CALL(*rawMockDriver_, OpenOutputPort(driverId, i, _)).WillOnce(Return(MIDI_STATUS_OK));
        std::shared_ptr<MidiSharedRing> bufferOut;
        ASSERT_EQ(controller_->OpenOutputPort(clientId_, bufferOut, deviceId, i), MIDI_STATUS_OK);
    }

    // Try to open 65th port (should fail)
    EXPECT_CALL(*rawMockDriver_, OpenInputPort(driverId, 32, _)).Times(0);
    std::shared_ptr<MidiSharedRing> buffer;
    int32_t ret = controller_->OpenInputPort(clientId_, buffer, deviceId, 32);
    EXPECT_EQ(ret, MIDI_STATUS_TOO_MANY_OPEN_PORTS);

    // Clean up
    for (uint32_t i = 0; i < 32; i++) {
        EXPECT_CALL(*rawMockDriver_, CloseInputPort(driverId, i)).WillOnce(Return(MIDI_STATUS_OK));
        controller_->CloseInputPort(clientId_, deviceId, i);
        EXPECT_CALL(*rawMockDriver_, CloseOutputPort(driverId, i)).WillOnce(Return(MIDI_STATUS_OK));
        controller_->CloseOutputPort(clientId_, deviceId, i);
    }
    EXPECT_CALL(*rawMockDriver_, CloseDevice(driverId)).WillOnce(Return(MIDI_STATUS_OK));
    controller_->CloseDevice(clientId_, deviceId);
}

/**
 * @tc.name: MaxPortsPerClient004
 * @tc.desc: Verify that after closing a port, a new one can be opened
 * @tc.type: FUNC
 */
HWTEST_F(MidiServiceControllerUnitTest, MaxPortsPerClient004, TestSize.Level0)
{
    int64_t driverId = 7000;
    int64_t deviceId = SimulateDeviceConnection(driverId, "MultiPort Device");

    EXPECT_CALL(*rawMockDriver_, OpenDevice(driverId)).WillOnce(Return(MIDI_STATUS_OK));
    ASSERT_EQ(controller_->OpenDevice(clientId_, deviceId), MIDI_STATUS_OK);

    // Open 64 input ports
    for (uint32_t i = 0; i < 64; i++) {
        EXPECT_CALL(*rawMockDriver_, OpenInputPort(driverId, i, _)).WillOnce(Return(MIDI_STATUS_OK));
        std::shared_ptr<MidiSharedRing> buffer;
        ASSERT_EQ(controller_->OpenInputPort(clientId_, buffer, deviceId, i), MIDI_STATUS_OK);
    }

    // Close one port
    EXPECT_CALL(*rawMockDriver_, CloseInputPort(driverId, 0)).WillOnce(Return(MIDI_STATUS_OK));
    ASSERT_EQ(controller_->CloseInputPort(clientId_, deviceId, 0), MIDI_STATUS_OK);

    // Now opening a 65th port should succeed
    EXPECT_CALL(*rawMockDriver_, OpenInputPort(driverId, 64, _)).WillOnce(Return(MIDI_STATUS_OK));
    std::shared_ptr<MidiSharedRing> buffer;
    int32_t ret = controller_->OpenInputPort(clientId_, buffer, deviceId, 64);
    EXPECT_EQ(ret, MIDI_STATUS_OK);

    // Clean up
    for (uint32_t i = 1; i < 65; i++) {
        EXPECT_CALL(*rawMockDriver_, CloseInputPort(driverId, i)).WillOnce(Return(MIDI_STATUS_OK));
        controller_->CloseInputPort(clientId_, deviceId, i);
    }
    EXPECT_CALL(*rawMockDriver_, CloseDevice(driverId)).WillOnce(Return(MIDI_STATUS_OK));
    controller_->CloseDevice(clientId_, deviceId);
}

/**
 * @tc.name: ResourceTrackingCleanup001
 * @tc.desc: Verify that resource tracking is properly cleaned up when client is destroyed
 * @tc.type: FUNC
 */
HWTEST_F(MidiServiceControllerUnitTest, ResourceTrackingCleanup001, TestSize.Level0)
{
    // Create second client
    uint32_t clientId2 = 0;
    sptr<IRemoteObject> clientObj2;
    sptr<MockMidiCallbackStub> cb2 = new MockMidiCallbackStub();
    ASSERT_EQ(controller_->CreateMidiInServer(cb2->AsObject(), clientObj2, clientId2), MIDI_STATUS_OK);

    // Open devices and ports for both clients
    int64_t driverId = 8000;
    int64_t deviceId = SimulateDeviceConnection(driverId, "Test Device");

    EXPECT_CALL(*rawMockDriver_, OpenDevice(driverId)).WillOnce(Return(MIDI_STATUS_OK));
    ASSERT_EQ(controller_->OpenDevice(clientId_, deviceId), MIDI_STATUS_OK);
    ASSERT_EQ(controller_->OpenDevice(clientId2, deviceId), MIDI_STATUS_OK);

    EXPECT_CALL(*rawMockDriver_, OpenInputPort(driverId, 0, _)).WillOnce(Return(MIDI_STATUS_OK));
    std::shared_ptr<MidiSharedRing> buffer;
    ASSERT_EQ(controller_->OpenInputPort(clientId_, buffer, deviceId, 0), MIDI_STATUS_OK);

    EXPECT_CALL(*rawMockDriver_, OpenInputPort(driverId, 1, _)).WillOnce(Return(MIDI_STATUS_OK));
    std::shared_ptr<MidiSharedRing> buffer2;
    ASSERT_EQ(controller_->OpenInputPort(clientId2, buffer2, deviceId, 1), MIDI_STATUS_OK);

    // Verify resource info exists for both clients
    ASSERT_NE(controller_->clientResourceInfo_.find(clientId_), controller_->clientResourceInfo_.end());
    ASSERT_NE(controller_->clientResourceInfo_.find(clientId2), controller_->clientResourceInfo_.end());

    // Destroy first client
    EXPECT_CALL(*rawMockDriver_, CloseInputPort(driverId, 0)).WillOnce(Return(MIDI_STATUS_OK));
    ASSERT_EQ(controller_->DestroyMidiClient(clientId_), MIDI_STATUS_OK);

    // Verify resource info for first client is cleaned up
    EXPECT_EQ(controller_->clientResourceInfo_.find(clientId_), controller_->clientResourceInfo_.end());

    // Second client's resource info should still exist
    ASSERT_NE(controller_->clientResourceInfo_.find(clientId2), controller_->clientResourceInfo_.end());

    // Clean up second client
    EXPECT_CALL(*rawMockDriver_, CloseInputPort(driverId, 1)).WillOnce(Return(MIDI_STATUS_OK));
    EXPECT_CALL(*rawMockDriver_, CloseDevice(driverId)).WillOnce(Return(MIDI_STATUS_OK));
    controller_->DestroyMidiClient(clientId2);
}

/**
 * @tc.name: SharedPortCounting001
 * @tc.desc: Verify that shared ports don't increment count multiple times
 * @tc.type: FUNC
 */
HWTEST_F(MidiServiceControllerUnitTest, SharedPortCounting001, TestSize.Level0)
{
    // Create second client
    uint32_t clientId2 = 0;
    sptr<IRemoteObject> clientObj2;
    sptr<MockMidiCallbackStub> cb2 = new MockMidiCallbackStub();
    ASSERT_EQ(controller_->CreateMidiInServer(cb2->AsObject(), clientObj2, clientId2), MIDI_STATUS_OK);

    int64_t driverId = 9000;
    int64_t deviceId = SimulateDeviceConnection(driverId, "Shared Device");

    EXPECT_CALL(*rawMockDriver_, OpenDevice(driverId)).WillOnce(Return(MIDI_STATUS_OK));
    ASSERT_EQ(controller_->OpenDevice(clientId_, deviceId), MIDI_STATUS_OK);
    ASSERT_EQ(controller_->OpenDevice(clientId2, deviceId), MIDI_STATUS_OK);

    // First client opens port (count = 1)
    EXPECT_CALL(*rawMockDriver_, OpenInputPort(driverId, 0, _)).WillOnce(Return(MIDI_STATUS_OK));
    std::shared_ptr<MidiSharedRing> buffer;
    ASSERT_EQ(controller_->OpenInputPort(clientId_, buffer, deviceId, 0), MIDI_STATUS_OK);

    auto &resourceInfo1 = controller_->clientResourceInfo_[clientId_];
    EXPECT_EQ(resourceInfo1.openPortCount, 1);

    // Second client connects to same port (count should still be 1 for each client)
    std::shared_ptr<MidiSharedRing> buffer2;
    ASSERT_EQ(controller_->OpenInputPort(clientId2, buffer2, deviceId, 0), MIDI_STATUS_OK);

    auto &resourceInfo2 = controller_->clientResourceInfo_[clientId2];
    EXPECT_EQ(resourceInfo2.openPortCount, 1);

    // Clean up
    EXPECT_CALL(*rawMockDriver_, CloseInputPort(driverId, 0)).WillOnce(Return(MIDI_STATUS_OK));
    controller_->CloseInputPort(clientId_, deviceId, 0);
    controller_->CloseInputPort(clientId2, deviceId, 0);
    EXPECT_CALL(*rawMockDriver_, CloseDevice(driverId)).WillOnce(Return(MIDI_STATUS_OK));
    controller_->CloseDevice(clientId_, deviceId);
    controller_->CloseDevice(clientId2, deviceId);
    controller_->DestroyMidiClient(clientId2);
}
