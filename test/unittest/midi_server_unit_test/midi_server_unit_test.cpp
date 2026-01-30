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

#include "iremote_stub.h"
#include "iservice_registry.h"
#include "message_parcel.h"
#include "midi_in_server.h"
#include "midi_info.h"
#include "midi_listener_callback.h"
#include "midi_server.h"
#include "midi_test_common.h"
#include "native_midi_base.h"
#include "parcel.h"
#include "system_ability_definition.h"
#include <map>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace OHOS;
using namespace MIDI;
using namespace testing;
using namespace testing::ext;

class MockIMidiCallback : public IMidiCallback {
public:
    MOCK_METHOD(int32_t, NotifyDeviceChange, (int32_t change, (const std::map<int32_t, std::string> &deviceInfo)),
                (override));
    MOCK_METHOD(int32_t, NotifyError, (int32_t code), (override));
    MOCK_METHOD(sptr<IRemoteObject>, AsObject, (), (override));
};

class TestMidiCallbackStub : public IRemoteStub<IMidiCallback> {
public:
    int32_t NotifyDeviceChange(int32_t, const std::map<int32_t, std::string> &) override { return 0; }
    int32_t NotifyError(int32_t) override { return 0; }
};

class MockMidiServiceController : public MidiServiceController {
    MOCK_METHOD(int32_t, CreateMidiInServer,
                (std::shared_ptr<MidiServiceCallback> callback, sptr<IRemoteObject> &client, uint32_t &clientId));
};

class MidiServerUnitTest : public testing::Test {
public:
};

/**
 * @tc.name: MidiInServer_GetDevices001
 * @tc.desc: call controller's GetDevices(), expect OK
 * @tc.type: FUNC
 */

HWTEST_F(MidiServerUnitTest, MidiInServer_GetDevices001, TestSize.Level0)
{
    auto mockCallback = std::make_shared<MockMidiServiceCallback>();
    uint32_t id = 123;
    std::vector<std::map<int32_t, std::string>> devices;

    MidiInServer client(id, mockCallback);
    EXPECT_EQ(MIDI_STATUS_OK, client.GetDevices(devices));
    EXPECT_TRUE(devices.empty());
}

/**
 * @tc.name: MidiInServer_GetDevicePorts001
 * @tc.desc: call controller's GetDevicePorts(), expect OK
 * @tc.type: FUNC
 */

HWTEST_F(MidiServerUnitTest, MidiInServer_GetDevicePorts001, TestSize.Level0)
{
    auto mockCallback = std::make_shared<MockMidiServiceCallback>();
    uint32_t id = 123;
    int64_t deviceId = 12345;
    std::vector<std::map<int32_t, std::string>> ports;

    MidiInServer client(id, mockCallback);
    EXPECT_EQ(MIDI_STATUS_OK, client.GetDevicePorts(deviceId, ports));
    EXPECT_TRUE(ports.empty());
}

/**
 * @tc.name: MidiInServer_OpenDevice001
 * @tc.desc: call controller's OpenDevice()
 * @tc.type: FUNC
 */

HWTEST_F(MidiServerUnitTest, MidiInServer_OpenDevice001, TestSize.Level0)
{
    auto mockCallback = std::make_shared<MockMidiServiceCallback>();
    uint32_t id = 123;
    int64_t deviceId = 12345;

    MidiInServer client(id, mockCallback);
    EXPECT_NE(MIDI_STATUS_OK, client.OpenDevice(deviceId));
}

/**
 * @tc.name: MidiInServer_OpenInputPort001
 * @tc.desc: call controller's OpenInputPort()
 * @tc.type: FUNC
 */

HWTEST_F(MidiServerUnitTest, MidiInServer_OpenInputPort001, TestSize.Level0)
{
    auto mockCallback = std::make_shared<MockMidiServiceCallback>();
    uint32_t id = 123;
    std::shared_ptr<MidiSharedRing> buffer;
    int64_t deviceId = 12345;
    uint32_t portIndex = 1;

    MidiInServer client(id, mockCallback);
    EXPECT_NE(MIDI_STATUS_OK, client.OpenInputPort(buffer, deviceId, portIndex));
}

/**
 * @tc.name: MidiInServer_CloseInputPort001
 * @tc.desc: call controller's CloseInputPort()
 * @tc.type: FUNC
 */

HWTEST_F(MidiServerUnitTest, MidiInServer_CloseInputPort001, TestSize.Level0)
{
    auto mockCallback = std::make_shared<MockMidiServiceCallback>();
    uint32_t id = 123;
    int64_t deviceId = 12345;
    uint32_t portIndex = 1;
    MidiInServer client(id, mockCallback);
    EXPECT_NE(MIDI_STATUS_OK, client.CloseInputPort(deviceId, portIndex));
}

/**
 * @tc.name: MidiInServer_CloseDevice001
 * @tc.desc: call controller's CloseDevice()
 * @tc.type: FUNC
 */

HWTEST_F(MidiServerUnitTest, MidiInServer_CloseDevice001, TestSize.Level0)
{
    auto mockCallback = std::make_shared<MockMidiServiceCallback>();
    uint32_t id = 123;
    int64_t deviceId = 12345;

    MidiInServer client(id, mockCallback);
    EXPECT_NE(MIDI_STATUS_OK, client.CloseDevice(deviceId));
}

/**
 * @tc.name: MidiInServer_DestroyMidiClient001
 * @tc.desc: call controller's DestroyMidiClient()
 * @tc.type: FUNC
 */

HWTEST_F(MidiServerUnitTest, MidiInServer_DestroyMidiClient001, TestSize.Level0)
{
    auto mockCallback = std::make_shared<MockMidiServiceCallback>();
    uint32_t id = 123;

    MidiInServer client(id, mockCallback);
    EXPECT_NE(MIDI_STATUS_OK, client.DestroyMidiClient());
}

/**
 * @tc.name: MidiInServer_NotifyError001
 * @tc.desc: call callback's NotifyError
 * @tc.type: FUNC
 */

HWTEST_F(MidiServerUnitTest, MidiInServer_NotifyError001, TestSize.Level0)
{
    auto mockCallback = std::make_shared<MockMidiServiceCallback>();
    auto rawCallback = mockCallback.get();
    uint32_t id = 123;
    EXPECT_CALL(*rawCallback, NotifyError(-1)).Times(1);

    MidiInServer client(id, mockCallback);
    client.NotifyError(-1);
    ASSERT_NE(nullptr, client.callback_);
}

/**
 * @tc.name: MidiListenerCallback_NotifyDeviceChange001
 * @tc.desc: call callback's NotifyDeviceChange
 * @tc.type: FUNC
 */

HWTEST_F(MidiServerUnitTest, MidiListenerCallback_NotifyDeviceChange001, TestSize.Level0)
{
    std::map<int32_t, std::string> devicdInfo = {{1, "piano"}};
    sptr<MockIMidiCallback> mockCallback = sptr<MockIMidiCallback>::MakeSptr();
    EXPECT_CALL(*mockCallback, NotifyDeviceChange(ADD, devicdInfo)).Times(1);

    MidiListenerCallback listener(mockCallback);
    listener.NotifyDeviceChange(ADD, devicdInfo);
    EXPECT_NE(nullptr, listener.callback_);
}

/**
 * @tc.name: MidiListenerCallback_NotifyError001
 * @tc.desc: call callback's NotifyError
 * @tc.type: FUNC
 */

HWTEST_F(MidiServerUnitTest, MidiListenerCallback_NotifyError001, TestSize.Level0)
{
    sptr<MockIMidiCallback> mockCallback = sptr<MockIMidiCallback>::MakeSptr();
    EXPECT_CALL(*mockCallback, NotifyError(-1)).Times(1);

    MidiListenerCallback listener(mockCallback);
    listener.NotifyError(-1);
    EXPECT_NE(nullptr, listener.callback_);
}

/**
 * @tc.name: MidiServer_OnStart001
 * @tc.desc: call callback's OnStart
 * @tc.type: FUNC
 */

HWTEST_F(MidiServerUnitTest, MidiServer_OnStart001, TestSize.Level0)
{
    int32_t systemAbilityId = 123;
    sptr<MidiServer> server = sptr<MidiServer>::MakeSptr(systemAbilityId, true);
    ASSERT_NE(nullptr, server);
    server->OnStart();
    server->OnDump();
    EXPECT_NE(nullptr, server->controller_);
}

/**
 * @tc.name: MidiServer_CreateMidiInServer001
 * @tc.desc: call callback's CreateMidiInServer
 * @tc.type: FUNC
 */

HWTEST_F(MidiServerUnitTest, MidiServer_CreateMidiInServer001, TestSize.Level0)
{
    int32_t systemAbilityId = 123;
    sptr<MidiServer> server = sptr<MidiServer>::MakeSptr(systemAbilityId, true);
    auto controler = std::make_shared<MockMidiServiceController>();
    sptr<IRemoteObject> object = (new TestMidiCallbackStub())->AsObject();
    sptr<IRemoteObject> client;
    uint32_t clientId = 0;
    server->controller_ = controler;
    ASSERT_NE(server->controller_, nullptr);
    ASSERT_NE(object, nullptr);

    EXPECT_NE(nullptr, server->controller_);
    EXPECT_EQ(MIDI_STATUS_OK, server->CreateMidiInServer(object, client, clientId));
}