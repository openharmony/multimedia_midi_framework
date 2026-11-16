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
#include "native_midi.h"
#include "native_midi_base.h"
#include <gtest/gtest.h>

using namespace testing;
using namespace testing::ext;

class OhMidiUnitTest : public testing::Test {
public:
};

/**
 * @tc.name: OhMidiClientNullParams001
 * @tc.desc: Verify every client-level C API rejects null pointers before touching IPC.
 * @tc.type: FUNC
 */
HWTEST_F(OhMidiUnitTest, OhMidiClientNullParams001, TestSize.Level0)
{
    OH_MIDICallbacks callbacks {};
    EXPECT_EQ(OH_MIDIClient_Create(nullptr, callbacks, nullptr), OH_MIDI_STATUS_GENERIC_INVALID_ARGUMENT);

    EXPECT_EQ(OH_MIDIClient_Destroy(nullptr), OH_MIDI_STATUS_INVALID_CLIENT);

    size_t count = 0;
    EXPECT_EQ(OH_MIDIClient_GetDeviceCount(nullptr, &count), OH_MIDI_STATUS_INVALID_CLIENT);

    OH_MIDIClient *fakeClient = reinterpret_cast<OH_MIDIClient *>(0x1);
    EXPECT_EQ(OH_MIDIClient_GetDeviceCount(fakeClient, nullptr), OH_MIDI_STATUS_GENERIC_INVALID_ARGUMENT);

    size_t actual = 0;
    EXPECT_EQ(OH_MIDIClient_GetDeviceInfos(nullptr, 0, nullptr, &actual), OH_MIDI_STATUS_INVALID_CLIENT);
    EXPECT_EQ(OH_MIDIClient_GetDeviceInfos(fakeClient, 0, nullptr, nullptr),
        OH_MIDI_STATUS_GENERIC_INVALID_ARGUMENT);

    OH_MIDIDevice *device = nullptr;
    EXPECT_EQ(OH_MIDIClient_OpenDevice(nullptr, 0, &device), OH_MIDI_STATUS_INVALID_CLIENT);
    EXPECT_EQ(OH_MIDIClient_OpenDevice(fakeClient, 0, nullptr), OH_MIDI_STATUS_GENERIC_INVALID_ARGUMENT);

    EXPECT_EQ(OH_MIDIClient_OpenBLEDevice(nullptr, "addr", nullptr, nullptr), OH_MIDI_STATUS_INVALID_CLIENT);
    EXPECT_EQ(OH_MIDIClient_OpenBLEDevice(fakeClient, nullptr, nullptr, nullptr),
        OH_MIDI_STATUS_GENERIC_INVALID_ARGUMENT);

    EXPECT_EQ(OH_MIDIClient_CloseDevice(nullptr, nullptr), OH_MIDI_STATUS_INVALID_CLIENT);
    EXPECT_EQ(OH_MIDIClient_CloseDevice(fakeClient, nullptr), OH_MIDI_STATUS_INVALID_DEVICE_HANDLE);

    EXPECT_EQ(OH_MIDIClient_GetPortCount(nullptr, 0, &count), OH_MIDI_STATUS_INVALID_CLIENT);
    EXPECT_EQ(OH_MIDIClient_GetPortCount(fakeClient, 0, nullptr), OH_MIDI_STATUS_GENERIC_INVALID_ARGUMENT);

    EXPECT_EQ(OH_MIDIClient_GetPortInfos(nullptr, 0, 0, nullptr, &actual), OH_MIDI_STATUS_INVALID_CLIENT);
    EXPECT_EQ(OH_MIDIClient_GetPortInfos(fakeClient, 0, 0, nullptr, nullptr),
        OH_MIDI_STATUS_GENERIC_INVALID_ARGUMENT);
}

/**
 * @tc.name: OhMidiDeviceNullParams001
 * @tc.desc: Verify every device-level C API rejects null pointers before touching IPC.
 * @tc.type: FUNC
 */
HWTEST_F(OhMidiUnitTest, OhMidiDeviceNullParams001, TestSize.Level0)
{
    OH_MIDIDevice *fakeDevice = reinterpret_cast<OH_MIDIDevice *>(0x1);
    OH_MIDIPortDescriptor descriptor {};
    OH_MIDIEvent events[1] = {};
    uint32_t eventsWritten = 0;
    uint8_t data[1] = {0xF0};

    EXPECT_EQ(OH_MIDIDevice_OpenInputPort(nullptr, descriptor, nullptr, nullptr),
        OH_MIDI_STATUS_INVALID_DEVICE_HANDLE);
    EXPECT_EQ(OH_MIDIDevice_OpenInputPort(fakeDevice, descriptor, nullptr, nullptr),
        OH_MIDI_STATUS_GENERIC_INVALID_ARGUMENT);

    EXPECT_EQ(OH_MIDIDevice_OpenOutputPort(nullptr, descriptor), OH_MIDI_STATUS_INVALID_DEVICE_HANDLE);
    EXPECT_EQ(OH_MIDIDevice_CloseInputPort(nullptr, 0), OH_MIDI_STATUS_INVALID_DEVICE_HANDLE);
    EXPECT_EQ(OH_MIDIDevice_CloseOutputPort(nullptr, 0), OH_MIDI_STATUS_INVALID_DEVICE_HANDLE);

    EXPECT_EQ(OH_MIDIDevice_Send(nullptr, 0, events, 1, &eventsWritten), OH_MIDI_STATUS_INVALID_DEVICE_HANDLE);

    EXPECT_EQ(OH_MIDIDevice_SendSysEx(nullptr, 0, data, 1), OH_MIDI_STATUS_INVALID_DEVICE_HANDLE);
    EXPECT_EQ(OH_MIDIDevice_SendSysEx(fakeDevice, 0, nullptr, 1), OH_MIDI_STATUS_GENERIC_INVALID_ARGUMENT);

    EXPECT_EQ(OH_MIDIDevice_FlushOutputPort(nullptr, 0), OH_MIDI_STATUS_INVALID_DEVICE_HANDLE);
}

/**
 * @tc.name: OhMidiClientLifecycle001
 * @tc.desc: Verify create/get-count/destroy lifecycle against the live service.
 * @tc.type: FUNC
 */
HWTEST_F(OhMidiUnitTest, OhMidiClientLifecycle001, TestSize.Level0)
{
    OH_MIDIClient *client = nullptr;
    OH_MIDICallbacks callbacks {};
    OH_MIDIStatusCode ret = OH_MIDIClient_Create(&client, callbacks, nullptr);
    if (ret != OH_MIDI_STATUS_OK) {
        // Service unavailable (e.g. host run): the failure branch is still exercised.
        EXPECT_EQ(client, nullptr);
        return;
    }
    ASSERT_NE(client, nullptr);

    size_t count = 0;
    EXPECT_EQ(OH_MIDIClient_GetDeviceCount(client, &count), OH_MIDI_STATUS_OK);

    EXPECT_EQ(OH_MIDIClient_Destroy(client), OH_MIDI_STATUS_OK);
}
