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
        mockCallback_ = new MockMidiServiceCallback();
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
    sptr<MockMidiServiceCallback> mockCallback_;
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
    sptr<MockMidiServiceCallback> cb = new MockMidiServiceCallback();
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
    sptr<MockMidiServiceCallback> cb2 = new MockMidiServiceCallback();
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
    sptr<MockMidiServiceCallback> cb2 = new MockMidiServiceCallback();
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
    sptr<MockMidiServiceCallback> cb2 = new MockMidiServiceCallback();
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
    sptr<MockMidiServiceCallback> cb2 = new MockMidiServiceCallback();
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
    sptr<MockMidiServiceCallback> cb2 = new MockMidiServiceCallback();
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
