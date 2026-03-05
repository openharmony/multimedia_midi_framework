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

#include "midi_info.h"
#include "midi_service_client.h"
#include "native_midi_base.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace OHOS;
using namespace MIDI;
using namespace testing;
using namespace testing::ext;

namespace {
bool operator==(const MidiDeviceInfo &lhs, const MidiDeviceInfo &rhs)
{
    return lhs.deviceId == rhs.deviceId &&
           lhs.driverDeviceId == rhs.driverDeviceId &&
           lhs.deviceType == rhs.deviceType &&
           lhs.transportProtocol == rhs.transportProtocol &&
           lhs.address == rhs.address &&
           lhs.deviceName == rhs.deviceName &&
           lhs.productId == rhs.productId &&
           lhs.vendorId == rhs.vendorId;
}
}

class MockMidiCallbackStub : public MidiCallbackStub {
public:
    MOCK_METHOD(int32_t, NotifyDeviceChange, (int32_t change, (const MidiDeviceInfo &deviceInfo)),
                (override));
    MOCK_METHOD(int32_t, NotifyError, (int32_t code), (override));
};

class MockIpcMidiInServer : public IIpcMidiInServer {
public:
    MOCK_METHOD(int32_t, GetDevices, (std::vector<MidiDeviceInfo> & devices), (override));
    MOCK_METHOD(int32_t, OpenDevice, (int64_t), (override));
    MOCK_METHOD(int32_t, OpenBleDevice, (const std::string &address, const sptr<IRemoteObject> &object), (override));
    MOCK_METHOD(int32_t, CloseDevice, (int64_t), (override));
    MOCK_METHOD(int32_t, GetDevicePorts, (int64_t, std::vector<MidiPortInfo> &), (override));
    MOCK_METHOD(int32_t, OpenInputPort, (std::shared_ptr<MidiSharedRing> &, int64_t, uint32_t), (override));
    MOCK_METHOD(int32_t, OpenOutputPort, (std::shared_ptr<MidiSharedRing> &, int64_t, uint32_t), (override));
    MOCK_METHOD(int32_t, FlushOutputPort, (int64_t, uint32_t), (override));
    MOCK_METHOD(int32_t, CloseInputPort, (int64_t, uint32_t), (override));
    MOCK_METHOD(int32_t, CloseOutputPort, (int64_t, uint32_t), (override));
    MOCK_METHOD(int32_t, DestroyMidiClient, (), (override));
    MOCK_METHOD(sptr<IRemoteObject>, AsObject, (), (override));
};

class MockIRemoteObject : public IRemoteObject {
public:
    MockIRemoteObject() : IRemoteObject(u"IRemoteObject") {}
    MOCK_METHOD(int32_t, GetObjectRefCount, (), (override));
    MOCK_METHOD(int, SendRequest, (uint32_t code, MessageParcel &data, MessageParcel &reply, MessageOption &option),
                (override));
    MOCK_METHOD(bool, AddDeathRecipient, (const sptr<DeathRecipient> &recipient), (override));
    MOCK_METHOD(bool, RemoveDeathRecipient, (const sptr<DeathRecipient> &recipient), (override));
    MOCK_METHOD(int, Dump, (int fd, const std::vector<std::u16string> &args), (override));
};

class MidiServiceClientUnitTest : public testing::Test {
public:
};

static void InjectIpcForTest(MidiServiceClient &client, const sptr<IIpcMidiInServer> &ipc) { client.ipc_ = ipc; }

/**
 * @tc.name: GetDevices_001
 * @tc.desc: ipc_ is nullptr -> return IPC_FAILURE.
 * @tc.type: FUNC
 */
HWTEST_F(MidiServiceClientUnitTest, GetDevices_001, TestSize.Level0)
{
    MidiServiceClient client;
    std::vector<MidiDeviceInfo> deviceInfos;
    EXPECT_EQ(client.GetDevices(deviceInfos), OH_MIDI_STATUS_GENERIC_IPC_FAILURE);
}

/**
 * @tc.name: GetDevices_002
 * @tc.desc: ipc_ not null -> should forward to ipc_->GetDevices.
 * @tc.type: FUNC
 */
HWTEST_F(MidiServiceClientUnitTest, GetDevices_002, TestSize.Level0)
{
    MidiServiceClient client;
    sptr<MockIpcMidiInServer> mockIpc = sptr<MockIpcMidiInServer>::MakeSptr();
    ASSERT_NE(mockIpc, nullptr);
    InjectIpcForTest(client, mockIpc);

    std::vector<MidiDeviceInfo> deviceInfos;
    EXPECT_CALL(*mockIpc, GetDevices(_))
        .Times(1)
        .WillOnce(Invoke([](std::vector<MidiDeviceInfo> &devices) {
            devices.clear();
            MidiDeviceInfo dev;
            dev.deviceId = 1001;
            dev.deviceType = DeviceType::DEVICE_TYPE_USB;
            dev.transportProtocol = TransportProtocol::PROTOCOL_1_0;
            dev.address = "";
            dev.deviceName = "dev0";
            dev.productId = 0x1234;
            dev.vendorId = 0x5678;
            devices.push_back(dev);
            return OH_MIDI_STATUS_OK;
        }));

    EXPECT_EQ(client.GetDevices(deviceInfos), OH_MIDI_STATUS_OK);
    ASSERT_EQ(deviceInfos.size(), 1u);
    EXPECT_EQ(deviceInfos[0].deviceId, 1001);
}

/**
 * @tc.name: OpenDevice_001
 * @tc.desc: ipc_ is nullptr -> return IPC_FAILURE.
 * @tc.type: FUNC
 */
HWTEST_F(MidiServiceClientUnitTest, OpenDevice_001, TestSize.Level0)
{
    MidiServiceClient client;
    EXPECT_EQ(client.OpenDevice(1), OH_MIDI_STATUS_GENERIC_IPC_FAILURE);
}

/**
 * @tc.name: OpenDevice_002
 * @tc.desc: ipc_ not null -> should forward to ipc_->OpenDevice.
 * @tc.type: FUNC
 */
HWTEST_F(MidiServiceClientUnitTest, OpenDevice_002, TestSize.Level0)
{
    MidiServiceClient client;
    sptr<MockIpcMidiInServer> mockIpc = sptr<MockIpcMidiInServer>::MakeSptr();
    ASSERT_NE(mockIpc, nullptr);
    InjectIpcForTest(client, mockIpc);

    int64_t deviceId = 1001;
    EXPECT_CALL(*mockIpc, OpenDevice(deviceId)).Times(1).WillOnce(Return(OH_MIDI_STATUS_OK));
    EXPECT_EQ(client.OpenDevice(deviceId), OH_MIDI_STATUS_OK);
}

/**
 * @tc.name: CloseDevice_001
 * @tc.desc: ipc_ is nullptr -> return IPC_FAILURE.
 * @tc.type: FUNC
 */
HWTEST_F(MidiServiceClientUnitTest, CloseDevice_001, TestSize.Level0)
{
    MidiServiceClient client;
    EXPECT_EQ(client.CloseDevice(1), OH_MIDI_STATUS_GENERIC_IPC_FAILURE);
}

/**
 * @tc.name: CloseDevice_002
 * @tc.desc: ipc_ not null -> should forward to ipc_->CloseDevice.
 * @tc.type: FUNC
 */
HWTEST_F(MidiServiceClientUnitTest, CloseDevice_002, TestSize.Level0)
{
    MidiServiceClient client;
    sptr<MockIpcMidiInServer> mockIpc = sptr<MockIpcMidiInServer>::MakeSptr();
    ASSERT_NE(mockIpc, nullptr);
    InjectIpcForTest(client, mockIpc);

    int64_t deviceId = 1001;
    EXPECT_CALL(*mockIpc, CloseDevice(deviceId)).Times(1).WillOnce(Return(OH_MIDI_STATUS_OK));
    EXPECT_EQ(client.CloseDevice(deviceId), OH_MIDI_STATUS_OK);
}

/**
 * @tc.name: GetDevicePorts_001
 * @tc.desc: ipc_ is nullptr -> return IPC_FAILURE.
 * @tc.type: FUNC
 */
HWTEST_F(MidiServiceClientUnitTest, GetDevicePorts_001, TestSize.Level0)
{
    MidiServiceClient client;
    std::vector<MidiPortInfo> portInfos;
    EXPECT_EQ(client.GetDevicePorts(1, portInfos), OH_MIDI_STATUS_GENERIC_IPC_FAILURE);
}

/**
 * @tc.name: GetDevicePorts_002
 * @tc.desc: ipc_ not null -> should forward to ipc_->GetDevicePorts.
 * @tc.type: FUNC
 */
HWTEST_F(MidiServiceClientUnitTest, GetDevicePorts_002, TestSize.Level0)
{
    MidiServiceClient client;
    sptr<MockIpcMidiInServer> mockIpc = sptr<MockIpcMidiInServer>::MakeSptr();
    ASSERT_NE(mockIpc, nullptr);
    InjectIpcForTest(client, mockIpc);

    int64_t deviceId = 1002;
    std::vector<MidiPortInfo> portInfos;

    EXPECT_CALL(*mockIpc, GetDevicePorts(deviceId, _))
        .Times(1)
        .WillOnce(Invoke([](int64_t, std::vector<MidiPortInfo> &ports) {
            ports.clear();
            MidiPortInfo port1;
            port1.portId = 0;
            port1.direction = PortDirection::PORT_DIRECTION_INPUT;
            port1.name = "Input Port";
            port1.transportProtocol = TransportProtocol::PROTOCOL_1_0;
            ports.push_back(port1);

            MidiPortInfo port2;
            port2.portId = 1;
            port2.direction = PortDirection::PORT_DIRECTION_OUTPUT;
            port2.name = "Output Port";
            port2.transportProtocol = TransportProtocol::PROTOCOL_1_0;
            ports.push_back(port2);
            return OH_MIDI_STATUS_OK;
        }));

    EXPECT_EQ(client.GetDevicePorts(deviceId, portInfos), OH_MIDI_STATUS_OK);
    ASSERT_EQ(portInfos.size(), 2u);
}

/**
 * @tc.name: OpenInputPort_001
 * @tc.desc: ipc_ is nullptr -> return IPC_FAILURE.
 * @tc.type: FUNC
 */
HWTEST_F(MidiServiceClientUnitTest, OpenInputPort_001, TestSize.Level0)
{
    MidiServiceClient client;
    std::shared_ptr<MidiSharedRing> buffer;
    EXPECT_EQ(client.OpenInputPort(buffer, 1, 0), OH_MIDI_STATUS_GENERIC_IPC_FAILURE);
}

/**
 * @tc.name: OpenInputPort_002
 * @tc.desc: ipc_ not null -> should forward to ipc_->OpenInputPort and set buffer.
 * @tc.type: FUNC
 */
HWTEST_F(MidiServiceClientUnitTest, OpenInputPort_002, TestSize.Level0)
{
    MidiServiceClient client;
    sptr<MockIpcMidiInServer> mockIpc = sptr<MockIpcMidiInServer>::MakeSptr();
    ASSERT_NE(mockIpc, nullptr);
    InjectIpcForTest(client, mockIpc);

    std::shared_ptr<MidiSharedRing> buffer;
    int64_t deviceId = 1003;
    uint32_t portIndex = 3;

    EXPECT_CALL(*mockIpc, OpenInputPort(_, deviceId, portIndex))
        .Times(1)
        .WillOnce(Invoke([](std::shared_ptr<MidiSharedRing> &outBuffer, int64_t, uint32_t) {
            outBuffer = MidiSharedRing::CreateFromLocal(256);
            return (outBuffer != nullptr) ? OH_MIDI_STATUS_OK : OH_MIDI_STATUS_SYSTEM_ERROR;
        }));

    EXPECT_EQ(client.OpenInputPort(buffer, deviceId, portIndex), OH_MIDI_STATUS_OK);
    EXPECT_NE(buffer, nullptr);
}

/**
 * @tc.name: CloseInputPort_001
 * @tc.desc: ipc_ is nullptr -> return IPC_FAILURE.
 * @tc.type: FUNC
 */
HWTEST_F(MidiServiceClientUnitTest, CloseInputPort_001, TestSize.Level0)
{
    MidiServiceClient client;
    EXPECT_EQ(client.CloseInputPort(1, 0), OH_MIDI_STATUS_GENERIC_IPC_FAILURE);
}

/**
 * @tc.name: CloseInputPort_002
 * @tc.desc: ipc_ not null -> should forward to ipc_->CloseInputPort.
 * @tc.type: FUNC
 */
HWTEST_F(MidiServiceClientUnitTest, CloseInputPort_002, TestSize.Level0)
{
    MidiServiceClient client;
    sptr<MockIpcMidiInServer> mockIpc = sptr<MockIpcMidiInServer>::MakeSptr();
    ASSERT_NE(mockIpc, nullptr);
    InjectIpcForTest(client, mockIpc);

    int64_t deviceId = 1004;
    uint32_t portIndex = 0;

    EXPECT_CALL(*mockIpc, CloseInputPort(deviceId, portIndex)).Times(1).WillOnce(Return(OH_MIDI_STATUS_OK));
    EXPECT_EQ(client.CloseInputPort(deviceId, portIndex), OH_MIDI_STATUS_OK);
}