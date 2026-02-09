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

class MockMidiCallbackStub : public MidiCallbackStub {
public:
    MOCK_METHOD(int32_t, NotifyDeviceChange, (int32_t change, (const std::map<int32_t, std::string> &deviceInfo)),
                (override));
    MOCK_METHOD(int32_t, NotifyError, (int32_t code), (override));
};

class MockIpcMidiInServer : public IIpcMidiInServer {
public:
    MOCK_METHOD(int32_t, GetDevices, ((std::vector<std::map<int32_t, std::string>> & devices)), (override));
    MOCK_METHOD(int32_t, OpenDevice, (int64_t), (override));
    MOCK_METHOD(int32_t, OpenBleDevice, (const std::string &address, const sptr<IRemoteObject> &object), (override));
    MOCK_METHOD(int32_t, CloseDevice, (int64_t), (override));
    MOCK_METHOD(int32_t, GetDevicePorts, (int64_t, (std::vector<std::map<int32_t, std::string>> &)), (override));
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
    std::vector<std::map<int32_t, std::string>> deviceInfos;
    EXPECT_EQ(client.GetDevices(deviceInfos), MIDI_STATUS_GENERIC_IPC_FAILURE);
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

    std::vector<std::map<int32_t, std::string>> deviceInfos;
    EXPECT_CALL(*mockIpc, GetDevices(_))
        .Times(1)
        .WillOnce(Invoke([](std::vector<std::map<int32_t, std::string>> &devices) {
            devices.clear();
            devices.push_back({{0, "dev0"}, {1, "usb"}});
            return MIDI_STATUS_OK;
        }));

    EXPECT_EQ(client.GetDevices(deviceInfos), MIDI_STATUS_OK);
    ASSERT_EQ(deviceInfos.size(), 1u);
    EXPECT_EQ(deviceInfos[0].at(0), "dev0");
}

/**
 * @tc.name: OpenDevice_001
 * @tc.desc: ipc_ is nullptr -> return IPC_FAILURE.
 * @tc.type: FUNC
 */
HWTEST_F(MidiServiceClientUnitTest, OpenDevice_001, TestSize.Level0)
{
    MidiServiceClient client;
    EXPECT_EQ(client.OpenDevice(1), MIDI_STATUS_GENERIC_IPC_FAILURE);
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
    EXPECT_CALL(*mockIpc, OpenDevice(deviceId)).Times(1).WillOnce(Return(MIDI_STATUS_OK));
    EXPECT_EQ(client.OpenDevice(deviceId), MIDI_STATUS_OK);
}

/**
 * @tc.name: CloseDevice_001
 * @tc.desc: ipc_ is nullptr -> return IPC_FAILURE.
 * @tc.type: FUNC
 */
HWTEST_F(MidiServiceClientUnitTest, CloseDevice_001, TestSize.Level0)
{
    MidiServiceClient client;
    EXPECT_EQ(client.CloseDevice(1), MIDI_STATUS_GENERIC_IPC_FAILURE);
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
    EXPECT_CALL(*mockIpc, CloseDevice(deviceId)).Times(1).WillOnce(Return(MIDI_STATUS_OK));
    EXPECT_EQ(client.CloseDevice(deviceId), MIDI_STATUS_OK);
}

/**
 * @tc.name: GetDevicePorts_001
 * @tc.desc: ipc_ is nullptr -> return IPC_FAILURE.
 * @tc.type: FUNC
 */
HWTEST_F(MidiServiceClientUnitTest, GetDevicePorts_001, TestSize.Level0)
{
    MidiServiceClient client;
    std::vector<std::map<int32_t, std::string>> portInfos;
    EXPECT_EQ(client.GetDevicePorts(1, portInfos), MIDI_STATUS_GENERIC_IPC_FAILURE);
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
    std::vector<std::map<int32_t, std::string>> portInfos;

    EXPECT_CALL(*mockIpc, GetDevicePorts(deviceId, _))
        .Times(1)
        .WillOnce(Invoke([](int64_t, std::vector<std::map<int32_t, std::string>> &ports) {
            ports.clear();
            ports.push_back({{0, "port0"}, {1, "input"}});
            ports.push_back({{0, "port1"}, {1, "output"}});
            return MIDI_STATUS_OK;
        }));

    EXPECT_EQ(client.GetDevicePorts(deviceId, portInfos), MIDI_STATUS_OK);
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
    EXPECT_EQ(client.OpenInputPort(buffer, 1, 0), MIDI_STATUS_GENERIC_IPC_FAILURE);
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
            return (outBuffer != nullptr) ? MIDI_STATUS_OK : MIDI_STATUS_UNKNOWN_ERROR;
        }));

    EXPECT_EQ(client.OpenInputPort(buffer, deviceId, portIndex), MIDI_STATUS_OK);
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
    EXPECT_EQ(client.CloseInputPort(1, 0), MIDI_STATUS_GENERIC_IPC_FAILURE);
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

    EXPECT_CALL(*mockIpc, CloseInputPort(deviceId, portIndex)).Times(1).WillOnce(Return(MIDI_STATUS_OK));
    EXPECT_EQ(client.CloseInputPort(deviceId, portIndex), MIDI_STATUS_OK);
}