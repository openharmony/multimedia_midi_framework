/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "native_midi.h"
#include "gtest/gtest.h"
#include <atomic>
#include <fsteam>
#include <iostream>
#include <queue>
#include <string>
#include <thread>
#include <vector>

using namespace std;
using namespace testing::ext;

class MidiSdkTest : public testing::Test {
protected:
    OH_MIDIClient *client = nullptr;

    virtual void SetUp()
    {
        OH_MIDICallbacks callbacks = {nullptr, nullptr};
        OH_MIDIStatusCode ret = OH_MIDIClientCreate(&client, callbacks, nullptr);
        ASSERT_EQ(ret, MIDI_STATUS_OK);
        ASSERT_NE(client, nullptr);
    }

    virtual void TearDown()
    {
        if (client) {
            OH_MIDIClientDestroy(client);
            client = nullptr;
        }
    }

    int64_t GetFirstDeviceId()
    {
        size_t count = 0;
        if (OH_MIDIGetDevices(client, nullptr, &count) != MIDI_STATUS_OK) {
            return -1;
        }
        if (count == 0) {
            return -1;
        }
        std::vector<OH_MIDIDeviceInformation> infos(count);
        if (OH_MIDIGetDevices(client, infos.data(), &count) != MIDI_STATUS_OK) {
            return -1;
        }
        return infos[0].midiDeviceId;
    }
};

/**
 * @tc.number    : SUB_MULTIMEDIA_MIDI_CLIENT_0100
 * @tc.name      : Test OH_MIDIClientCreate with valid parameters
 * @tc.desc      : Verify that a client can be created successfully.
 */
HWTEST_F(MidiSdkTest, MidiClient_Create_001, Function | MediumTest | Level1) { EXPECT_NE(client, nullptr); }

/**
 * @tc.number    : SUB_MULTIMEDIA_MIDI_CLIENT_0200
 * @tc.name      : Test OH_MIDIClientCreate with nullptr
 * @tc.desc      : Verify error handling when client pointer is null.
 */
HWTEST_F(MidiSdkTest, MidiClient_Create_002, Function | MediumTest | Level1) {
    OH_MIDICallbacks callbacks = {nullptr, nullptr};
    OH_MIDIStatusCode ret = OH_MIDIClientCreate(nullptr, callbacks, nullptr);
    EXPECT_EQ(ret, MIDI_STATUS_GENERIC_INVALID_ARGUMENT);
}

/**
 * @tc.number    : SUB_MULTIMEDIA_MIDI_DISCOVERY_0100
 * @tc.name      : Test OH_MIDIGetDevices double call pattern
 * @tc.desc      : Verify device enumeration logic.
 */
HWTEST_F(MidiSdkTest, MidiGetDevices_001, Function | MediumTest | Level1)
{
    size_t count = 0;
    OH_MIDIStatusCode ret = OH_MIDIGetDevices(client, nullptr, &count);
    EXPECT_EQ(ret, MIDI_STATUS_OK);

    if (count > 0) {
        std::vector<OH_MIDIDeviceInformation> infos(count);
        ret = OH_MIDIGetDevices(client, infos.data(), &count);
        EXPECT_EQ(ret, MIDI_STATUS_OK);
        EXPECT_NE(infos[0].midiDeviceId, 0);
    } else {
        printf("[MidiSdkTest] No devices found, skipping detail check.\n");
    }
}

/**
 * @tc.number    : SUB_MULTIMEDIA_MIDI_DEVICE_0100
 * @tc.name      : Test Device Open/Close Lifecycle
 * @tc.desc      : Verify opening and closing a device works (Hardware Dependent).
 */
HWTEST_F(MidiSdkTest, MidiOpenDevice_001, Function | MediumTest | Level1)
{
    int64_t devId = GetFirstDeviceId();
    if (devId < 0) {
        printf("[MidiSdkTest] Skip: No hardware connected.\n");
        return;
    }

    OH_MIDIDevice *device = nullptr;
    // 1. Open
    OH_MIDIStatusCode ret = OH_MIDIOpenDevice(client, devId, &device);
    EXPECT_EQ(ret, MIDI_STATUS_OK);
    EXPECT_NE(device, nullptr);

    // 2. Duplicate Open (Should fail)
    OH_MIDIDevice *device2 = nullptr;
    ret = OH_MIDIOpenDevice(client, devId, &device2);
    EXPECT_EQ(ret, MIDI_STATUS_DEVICE_ALREADY_OPEN);

    // 3. Close
    ret = OH_MIDICloseDevice(device);
    EXPECT_EQ(ret, MIDI_STATUS_OK);
}

/**
 * @tc.number    : SUB_MULTIMEDIA_MIDI_DEVICE_0200
 * @tc.name      : Test Open Invalid Device ID
 * @tc.desc      : Verify error handling for non-existent device ID.
 */
HWTEST_F(MidiSdkTest, MidiOpenDevice_002, Function | MediumTest | Level1) {
    OH_MIDIDevice *device = nullptr;
    int64_t invalidId = 999999;
    OH_MIDIStatusCode ret = OH_MIDIOpenDevice(client, invalidId, &device);
    EXPECT_NE(ret, MIDI_STATUS_OK);
}

/**
 * @tc.number    : SUB_MULTIMEDIA_MIDI_PORT_0100
 * @tc.name      : Test GetPorts and Open Output Port
 * @tc.desc      : Verify port enumeration and output port opening.
 */
HWTEST_F(MidiSdkTest, MidiOpenOutputPort_001, Function | MediumTest | Level1)
{
    int64_t devId = GetFirstDeviceId();
    if (devId < 0) {
        printf("[MidiSdkTest] Skip: No hardware connected.\n");
        return;
    }

    OH_MIDIDevice *device = nullptr;
    OH_MIDIOpenDevice(client, devId, &device);
    ASSERT_NE(device, nullptr);

    // 1. Get Ports
    size_t portCount = 0;
    OH_MIDIGetDevicePorts(device, nullptr, &portCount);
    
    if (portCount > 0) {
        std::vector<OH_MIDIPortInformation> ports(portCount);
        OH_MIDIGetDevicePorts(device, ports.data(), &portCount);

        // 2. Find an Output Port
        int targetPortIndex = -1;
        for (const auto &port : ports) {
            if (port.direction == MIDI_PORT_DIRECTION_OUTPUT) {
                targetPortIndex = port.portIndex;
                break;
            }
        }

        if (targetPortIndex >= 0) {
            // 3. Open Output Port
            OH_MIDIPortDescriptor desc;
            desc.portIndex = targetPortIndex;
            desc.protocol = MIDI_PROTOCOL_1_0; // Default test

            OH_MIDIStatusCode ret = OH_MIDIOpenOutputPort(device, desc);
            EXPECT_EQ(ret, MIDI_STATUS_OK);

            // 4. Close Port
            ret = OH_MIDIClosePort(device, targetPortIndex);
            EXPECT_EQ(ret, MIDI_STATUS_OK);
        } else {
            printf("[MidiSdkTest] Skip: No OUTPUT port found on device.\n");
        }
    }

    OH_MIDICloseDevice(device);
}

/**
 * @tc.number    : SUB_MULTIMEDIA_MIDI_IO_0100
 * @tc.name      : Test OH_MIDISend (Smoke Test)
 * @tc.desc      : Verify Send does not crash and returns OK.
 */
HWTEST_F(MidiSdkTest, MidiSend_001, Function | MediumTest | Level1)
{
    int64_t devId = GetFirstDeviceId();
    if (devId < 0) {
        printf("[MidiSdkTest] Skip: No hardware connected.\n");
        return;
    }

    OH_MIDIDevice *device = nullptr;
    OH_MIDIOpenDevice(client, devId, &device);
    
    OH_MIDIPortDescriptor desc = {0, MIDI_PROTOCOL_1_0};
    OH_MIDIStatusCode ret = OH_MIDIOpenOutputPort(device, desc);
    
    if (ret == MIDI_STATUS_OK) {
        // Type=2, Group=0, Status=0x90, Note=60, Vel=100
        uint32_t umpData = 0x20903C64;
        
        OH_MIDIEvent event;
        event.timestamp = 0; // Immediate
        event.length = 1;    // 1 word
        event.data = &umpData;

        uint32_t written = 0;
        ret = OH_MIDISend(device, desc, &event, 1, &written);
        
        EXPECT_EQ(ret, MIDI_STATUS_OK);
        EXPECT_EQ(written, 1);

        OH_MIDIClosePort(device, 0);
    }

    OH_MIDICloseDevice(device);
}
/**
 * @tc.number    : SUB_MULTIMEDIA_MIDI_BLE_0100
 * @tc.name      : Test OH_MIDIOpenBleDevice with invalid arguments
 * @tc.desc      : Verify error handling for BLE connection parameters.
 */
HWTEST_F(MidiSdkTest, MidiOpenBleDevice_Negative_001, Function | MediumTest | Level2) {
    OH_MIDIDevice *device = nullptr;
    const char *validMac = "AA:BB:CC:DD:EE:FF";

    // Case 1: Null Client
    OH_MIDIStatusCode ret = OH_MIDIOpenBleDevice(nullptr, validMac, &device);
    EXPECT_EQ(ret, MIDI_STATUS_INVALID_CLIENT);

    // Case 2: Null Address
    ret = OH_MIDIOpenBleDevice(client, nullptr, &device);
    EXPECT_EQ(ret, MIDI_STATUS_GENERIC_INVALID_ARGUMENT);

    // Case 3: Empty Address
    ret = OH_MIDIOpenBleDevice(client, "", &device);
    EXPECT_NE(ret, MIDI_STATUS_OK); // Should fail immediately
}

void TestOnMidiReceived(void *userData, const OH_MIDIEvent *events, size_t eventCount)
{
    // No-op
    (void)userData;
    (void)events;
    (void)eventCount;
}

/**
 * @tc.number    : SUB_MULTIMEDIA_MIDI_INPUT_0100
 * @tc.name      : Test OH_MIDIOpenInputPort parameter validation
 * @tc.desc      : Verify callback registration and direction checks.
 */
HWTEST_F(MidiSdkTest, MidiOpenInputPort_Validation_001, Function | MediumTest | Level1)
{
    int64_t devId = GetFirstDeviceId();
    if (devId < 0) {
        return; // Skip
    }
    
    OH_MIDIDevice *device = nullptr;
    OH_MIDIOpenDevice(client, devId, &device);
    ASSERT_NE(device, nullptr);

    size_t portCount = 0;
    OH_MIDIGetDevicePorts(device, nullptr, &portCount);
    if (portCount == 0) {
        OH_MIDICloseDevice(device);
        return;
    }
    std::vector<OH_MIDIPortInformation> ports(portCount);
    OH_MIDIGetDevicePorts(device, ports.data(), &portCount);

    for (const auto& port : ports) {
        OH_MIDIPortDescriptor desc = {port.portIndex, MIDI_PROTOCOL_1_0};

        if (port.direction == MIDI_PORT_DIRECTION_INPUT) {
            // Case A: Valid Input Open
            OH_MIDIStatusCode ret = OH_MIDIOpenInputPort(device, desc, TestOnMidiReceived, nullptr);
            EXPECT_EQ(ret, MIDI_STATUS_OK);
            OH_MIDIClosePort(device, port.portIndex);

            // Case B: Null Callback (Negative)
            ret = OH_MIDIOpenInputPort(device, desc, nullptr, nullptr);
            EXPECT_EQ(ret, MIDI_STATUS_GENERIC_INVALID_ARGUMENT);

        } else {
            // Case C: Open Output as Input (Negative)
            OH_MIDIStatusCode ret = OH_MIDIOpenInputPort(device, desc, TestOnMidiReceived, nullptr);
            EXPECT_EQ(ret, MIDI_STATUS_INVALID_PORT);
        }
    }

    OH_MIDICloseDevice(device);
}
/**
 * @tc.number    : SUB_MULTIMEDIA_MIDI_SYSEX_0100
 * @tc.name      : Test OH_MIDISendSysEx helper function
 * @tc.desc      : Verify the byte-stream to UMP helper logic.
 */
HWTEST_F(MidiSdkTest, MidiSendSysEx_001, Function | MediumTest | Level1)
{
    int64_t devId = GetFirstDeviceId();
    if (devId < 0) {
        return;
    }

    OH_MIDIDevice *device = nullptr;
    OH_MIDIOpenDevice(client, devId, &device);

    size_t portCount = 0;
    OH_MIDIGetDevicePorts(device, nullptr, &portCount);
    std::vector<OH_MIDIPortInformation> ports(portCount);
    OH_MIDIGetDevicePorts(device, ports.data(), &portCount);
    
    int outPortIndex = -1;
    for (auto &p : ports) {
        if (p.direction == MIDI_PORT_DIRECTION_OUTPUT) {
            outPortIndex = p.portIndex;
            break;
        }
    }

    if (outPortIndex >= 0) {
        OH_MIDIPortDescriptor desc = {(uint32_t)outPortIndex, MIDI_PROTOCOL_1_0};
        OH_MIDIOpenOutputPort(device, desc);

        // Case 1: Valid SysEx (Identity Request: F0 7E 7F 06 01 F7)
        uint8_t sysExMsg[] = {0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7};
        OH_MIDIStatusCode ret = OH_MIDISendSysEx(device, outPortIndex, sysExMsg, sizeof(sysExMsg));
        EXPECT_EQ(ret, MIDI_STATUS_OK);

        // Case 2: Null Data (Negative)
        ret = OH_MIDISendSysEx(device, outPortIndex, nullptr, 5);
        EXPECT_EQ(ret, MIDI_STATUS_GENERIC_INVALID_ARGUMENT);

        // Case 3: Zero Size (Negative)
        ret = OH_MIDISendSysEx(device, outPortIndex, sysExMsg, 0);
        EXPECT_NE(ret, MIDI_STATUS_OK); // Should fail

        OH_MIDIClosePort(device, outPortIndex);
    }

    OH_MIDICloseDevice(device);
}
/**
 * @tc.number    : SUB_MULTIMEDIA_MIDI_FLUSH_0100
 * @tc.name      : Test OH_MIDIFlushOutputPort
 * @tc.desc      : Verify flush operation on output ports.
 */
HWTEST_F(MidiSdkTest, MidiFlush_001, Function | MediumTest | Level1)
{
    int64_t devId = GetFirstDeviceId();
    if (devId < 0) {
        return;
    }

    OH_MIDIDevice *device = nullptr;
    OH_MIDIOpenDevice(client, devId, &device);
    
    size_t count = 0;
    OH_MIDIGetDevicePorts(device, nullptr, &count);
    if (count > 0) {
        OH_MIDIPortDescriptor desc = {0, MIDI_PROTOCOL_1_0}; // 尝试 index 0
        if (OH_MIDIOpenOutputPort(device, desc) == MIDI_STATUS_OK) {
            
            // Case 1: Valid Flush
            OH_MIDIStatusCode ret = OH_MIDIFlushOutputPort(device, 0);
            EXPECT_EQ(ret, MIDI_STATUS_OK);

            OH_MIDIClosePort(device, 0);
        }

        // Case 2: Flush Closed Port (Negative)
        OH_MIDIStatusCode ret = OH_MIDIFlushOutputPort(device, 0);

        EXPECT_NE(ret, MIDI_STATUS_GENERIC_IPC_FAILURE);
    }

    OH_MIDICloseDevice(device);
}

/**
 * @tc.number    : SUB_MULTIMEDIA_MIDI_DESTROY_0100
 * @tc.name      : Test OH_MIDIClientDestroy robustness
 * @tc.desc      : Verify destroying null or invalid clients.
 */
HWTEST_F(MidiSdkTest, MidiClient_Destroy_Negative_001, Function | MediumTest | Level1)
{
    // Case 1: Destroy Nullptr
    OH_MIDIStatusCode ret = OH_MIDIClientDestroy(nullptr);
    EXPECT_EQ(ret, MIDI_STATUS_INVALID_CLIENT);

    // Case 2: Double Destroy is NOT safe to test directly if it crashes,
    // but we can ensure logic flow.
    // In SetUp/TearDown pattern, client is destroyed at TearDown.
    // Here we manually destroy it to test success.
    if (client) {
        ret = OH_MIDIClientDestroy(client);
        EXPECT_EQ(ret, MIDI_STATUS_OK);
        client = nullptr; // Prevent TearDown from double-freeing
    }
}