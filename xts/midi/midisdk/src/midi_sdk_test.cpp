/*
 * Copyright (c) 2025 Huawei Device Co., Ltd.
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
#include <iostream>
#include <vector>
#include <atomic>
#include <fsteam>
#include <queue>
#include <string>
#include <thread>
#include "gtest/gtest.h"
#include "native_midi.h"

using namespace std;
using namespace testing::ext;

class MidiSdkTest : public testing::Test {
protected:
    OH_MidiClient *client = nullptr;

    virtual void SetUp()
    {
        OH_MidiCallbacks callbacks = {nullptr, nullptr};
        OH_MidiStatusCode ret = OH_MidiClientCreate(&client, callbacks, nullptr);
        ASSERT_EQ(ret, MIDI_STATUS_OK);
        ASSERT_NE(client, nullptr);
    }

    virtual void TearDown()
    {
        if (client) {
            OH_MidiClientDestroy(client);
            client = nullptr;
        }
    }

    int64_t GetFirstDeviceId() {
        size_t count = 0;
        if (OH_MidiGetDevices(client, nullptr, &count) != MIDI_STATUS_OK) {
            return -1;
        }
        if (count == 0) {
            return -1;
        }
        std::vector<OH_MidiDeviceInformation> infos(count);
        if (OH_MidiGetDevices(client, infos.data(), &count) != MIDI_STATUS_OK) {
            return -1;
        }
        return infos[0].midiDeviceId;
    }
};

/**
 * @tc.number    : SUB_MULTIMEDIA_MIDI_CLIENT_0100
 * @tc.name      : Test OH_MidiClientCreate with valid parameters
 * @tc.desc      : Verify that a client can be created successfully.
 */
HWTEST_F(MidiSdkTest, MidiClient_Create_001, Function | MediumTest | Level1) {
    EXPECT_NE(client, nullptr);
}

/**
 * @tc.number    : SUB_MULTIMEDIA_MIDI_CLIENT_0200
 * @tc.name      : Test OH_MidiClientCreate with nullptr
 * @tc.desc      : Verify error handling when client pointer is null.
 */
HWTEST_F(MidiSdkTest, MidiClient_Create_002, Function | MediumTest | Level1) {
    OH_MidiCallbacks callbacks = {nullptr, nullptr};
    OH_MidiStatusCode ret = OH_MidiClientCreate(nullptr, callbacks, nullptr);
    EXPECT_EQ(ret, MIDI_STATUS_GENERIC_INVALID_ARGUMENT);
}

/**
 * @tc.number    : SUB_MULTIMEDIA_MIDI_DISCOVERY_0100
 * @tc.name      : Test OH_MidiGetDevices double call pattern
 * @tc.desc      : Verify device enumeration logic.
 */
HWTEST_F(MidiSdkTest, MidiGetDevices_001, Function | MediumTest | Level1) {
    size_t count = 0;
    OH_MidiStatusCode ret = OH_MidiGetDevices(client, nullptr, &count);
    EXPECT_EQ(ret, MIDI_STATUS_OK);

    if (count > 0) {
        std::vector<OH_MidiDeviceInformation> infos(count);
        ret = OH_MidiGetDevices(client, infos.data(), &count);
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
HWTEST_F(MidiSdkTest, MidiOpenDevice_001, Function | MediumTest | Level1) {
    int64_t devId = GetFirstDeviceId();
    if (devId < 0) {
        printf("[MidiSdkTest] Skip: No hardware connected.\n");
        return;
    }

    OH_MidiDevice *device = nullptr;
    // 1. Open
    OH_MidiStatusCode ret = OH_MidiOpenDevice(client, devId, &device);
    EXPECT_EQ(ret, MIDI_STATUS_OK);
    EXPECT_NE(device, nullptr);

    // 2. Duplicate Open (Should fail)
    OH_MidiDevice *device2 = nullptr;
    ret = OH_MidiOpenDevice(client, devId, &device2);
    EXPECT_EQ(ret, MIDI_STATUS_DEVICE_ALREADY_OPEN);

    // 3. Close
    ret = OH_MidiCloseDevice(device);
    EXPECT_EQ(ret, MIDI_STATUS_OK);
}

/**
 * @tc.number    : SUB_MULTIMEDIA_MIDI_DEVICE_0200
 * @tc.name      : Test Open Invalid Device ID
 * @tc.desc      : Verify error handling for non-existent device ID.
 */
HWTEST_F(MidiSdkTest, MidiOpenDevice_002, Function | MediumTest | Level1) {
    OH_MidiDevice *device = nullptr;
    int64_t invalidId = 999999;
    OH_MidiStatusCode ret = OH_MidiOpenDevice(client, invalidId, &device);
    EXPECT_NE(ret, MIDI_STATUS_OK);
}

/**
 * @tc.number    : SUB_MULTIMEDIA_MIDI_PORT_0100
 * @tc.name      : Test GetPorts and Open Output Port
 * @tc.desc      : Verify port enumeration and output port opening.
 */
HWTEST_F(MidiSdkTest, MidiOpenOutputPort_001, Function | MediumTest | Level1) {
    int64_t devId = GetFirstDeviceId();
    if (devId < 0) {
        printf("[MidiSdkTest] Skip: No hardware connected.\n");
        return;
    }

    OH_MidiDevice *device = nullptr;
    OH_MidiOpenDevice(client, devId, &device);
    ASSERT_NE(device, nullptr);

    // 1. Get Ports
    size_t portCount = 0;
    OH_MidiGetDevicePorts(device, nullptr, &portCount);
    
    if (portCount > 0) {
        std::vector<OH_MidiPortInformation> ports(portCount);
        OH_MidiGetDevicePorts(device, ports.data(), &portCount);

        // 2. Find an Output Port
        int targetPortIndex = -1;
        for (const auto& port : ports) {
            if (port.direction == MIDI_PORT_DIRECTION_OUTPUT) {
                targetPortIndex = port.portIndex;
                break;
            }
        }

        if (targetPortIndex >= 0) {
            // 3. Open Output Port
            OH_MidiPortDescriptor desc;
            desc.portIndex = targetPortIndex;
            desc.protocol = MIDI_PROTOCOL_1_0; // Default test

            OH_MidiStatusCode ret = OH_MidiOpenOutputPort(device, desc);
            EXPECT_EQ(ret, MIDI_STATUS_OK);
            
            // 4. Close Port
            ret = OH_MidiClosePort(device, targetPortIndex);
            EXPECT_EQ(ret, MIDI_STATUS_OK);
        } else {
            printf("[MidiSdkTest] Skip: No OUTPUT port found on device.\n");
        }
    }

    OH_MidiCloseDevice(device);
}

/**
 * @tc.number    : SUB_MULTIMEDIA_MIDI_IO_0100
 * @tc.name      : Test OH_MidiSend (Smoke Test)
 * @tc.desc      : Verify Send does not crash and returns OK.
 */
HWTEST_F(MidiSdkTest, MidiSend_001, Function | MediumTest | Level1) {
    int64_t devId = GetFirstDeviceId();
    if (devId < 0) {
        printf("[MidiSdkTest] Skip: No hardware connected.\n");
        return;
    }

    OH_MidiDevice *device = nullptr;
    OH_MidiOpenDevice(client, devId, &device);
    
    OH_MidiPortDescriptor desc = {0, MIDI_PROTOCOL_1_0};
    OH_MidiStatusCode ret = OH_MidiOpenOutputPort(device, desc);
    
    if (ret == MIDI_STATUS_OK) {
        // Type=2, Group=0, Status=0x90, Note=60, Vel=100
        uint32_t umpData = 0x20903C64;
        
        OH_MidiEvent event;
        event.timestamp = 0; // Immediate
        event.length = 1;    // 1 word
        event.data = &umpData;

        uint32_t written = 0;
        ret = OH_MidiSend(device, desc, &event, 1, &written);
        
        EXPECT_EQ(ret, MIDI_STATUS_OK);
        EXPECT_EQ(written, 1);

        OH_MidiClosePort(device, 0);
    }

    OH_MidiCloseDevice(device);
}
/**
 * @tc.number    : SUB_MULTIMEDIA_MIDI_BLE_0100
 * @tc.name      : Test OH_MidiOpenBleDevice with invalid arguments
 * @tc.desc      : Verify error handling for BLE connection parameters.
 */
HWTEST_F(MidiSdkTest, MidiOpenBleDevice_Negative_001, Function | MediumTest | Level2) {
    OH_MidiDevice *device = nullptr;
    const char *validMac = "AA:BB:CC:DD:EE:FF";

    // Case 1: Null Client
    OH_MidiStatusCode ret = OH_MidiOpenBleDevice(nullptr, validMac, &device);
    EXPECT_EQ(ret, MIDI_STATUS_INVALID_CLIENT);

    // Case 2: Null Address
    ret = OH_MidiOpenBleDevice(client, nullptr, &device);
    EXPECT_EQ(ret, MIDI_STATUS_GENERIC_INVALID_ARGUMENT);

    // Case 3: Empty Address
    ret = OH_MidiOpenBleDevice(client, "", &device);
    EXPECT_NE(ret, MIDI_STATUS_OK); // Should fail immediately
}

void TestOnMidiReceived(void *userData, const OH_MidiEvent *events, size_t eventCount) {
    // No-op
}

/**
 * @tc.number    : SUB_MULTIMEDIA_MIDI_INPUT_0100
 * @tc.name      : Test OH_MidiOpenInputPort parameter validation
 * @tc.desc      : Verify callback registration and direction checks.
 */
HWTEST_F(MidiSdkTest, MidiOpenInputPort_Validation_001, Function | MediumTest | Level1) {
    int64_t devId = GetFirstDeviceId();
    if (devId < 0) {
        return; // Skip
    }
    
    OH_MidiDevice *device = nullptr;
    OH_MidiOpenDevice(client, devId, &device);
    ASSERT_NE(device, nullptr);

    size_t portCount = 0;
    OH_MidiGetDevicePorts(device, nullptr, &portCount);
    if (portCount == 0) {
        OH_MidiCloseDevice(device);
        return;
    }
    std::vector<OH_MidiPortInformation> ports(portCount);
    OH_MidiGetDevicePorts(device, ports.data(), &portCount);

    for (const auto& port : ports) {
        OH_MidiPortDescriptor desc = {port.portIndex, MIDI_PROTOCOL_1_0};

        if (port.direction == MIDI_PORT_DIRECTION_INPUT) {
            // Case A: Valid Input Open
            OH_MidiStatusCode ret = OH_MidiOpenInputPort(device, desc, TestOnMidiReceived, nullptr);
            EXPECT_EQ(ret, MIDI_STATUS_OK);
            OH_MidiClosePort(device, port.portIndex);

            // Case B: Null Callback (Negative)
            ret = OH_MidiOpenInputPort(device, desc, nullptr, nullptr);
            EXPECT_EQ(ret, MIDI_STATUS_GENERIC_INVALID_ARGUMENT);

        } else {
            // Case C: Open Output as Input (Negative)
            OH_MidiStatusCode ret = OH_MidiOpenInputPort(device, desc, TestOnMidiReceived, nullptr);
            EXPECT_EQ(ret, MIDI_STATUS_INVALID_PORT);
        }
    }

    OH_MidiCloseDevice(device);
}
/**
 * @tc.number    : SUB_MULTIMEDIA_MIDI_SYSEX_0100
 * @tc.name      : Test OH_MidiSendSysEx helper function
 * @tc.desc      : Verify the byte-stream to UMP helper logic.
 */
HWTEST_F(MidiSdkTest, MidiSendSysEx_001, Function | MediumTest | Level1) {
    int64_t devId = GetFirstDeviceId();
    if (devId < 0) {
        return;
    }

    OH_MidiDevice *device = nullptr;
    OH_MidiOpenDevice(client, devId, &device);

    size_t portCount = 0;
    OH_MidiGetDevicePorts(device, nullptr, &portCount);
    std::vector<OH_MidiPortInformation> ports(portCount);
    OH_MidiGetDevicePorts(device, ports.data(), &portCount);
    
    int outPortIndex = -1;
    for (auto &p : ports) {
        if (p.direction == MIDI_PORT_DIRECTION_OUTPUT) {
            outPortIndex = p.portIndex;
            break;
        }
    }

    if (outPortIndex >= 0) {
        OH_MidiPortDescriptor desc = {(uint32_t)outPortIndex, MIDI_PROTOCOL_1_0};
        OH_MidiOpenOutputPort(device, desc);

        // Case 1: Valid SysEx (Identity Request: F0 7E 7F 06 01 F7)
        uint8_t sysExMsg[] = {0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7};
        OH_MidiStatusCode ret = OH_MidiSendSysEx(device, outPortIndex, sysExMsg, sizeof(sysExMsg));
        EXPECT_EQ(ret, MIDI_STATUS_OK);

        // Case 2: Null Data (Negative)
        ret = OH_MidiSendSysEx(device, outPortIndex, nullptr, 5);
        EXPECT_EQ(ret, MIDI_STATUS_GENERIC_INVALID_ARGUMENT);

        // Case 3: Zero Size (Negative)
        ret = OH_MidiSendSysEx(device, outPortIndex, sysExMsg, 0);
        EXPECT_NE(ret, MIDI_STATUS_OK); // Should fail

        OH_MidiClosePort(device, outPortIndex);
    }

    OH_MidiCloseDevice(device);
}
/**
 * @tc.number    : SUB_MULTIMEDIA_MIDI_FLUSH_0100
 * @tc.name      : Test OH_MidiFlushOutputPort
 * @tc.desc      : Verify flush operation on output ports.
 */
HWTEST_F(MidiSdkTest, MidiFlush_001, Function | MediumTest | Level1) {
    int64_t devId = GetFirstDeviceId();
    if (devId < 0) {
        return;
    }

    OH_MidiDevice *device = nullptr;
    OH_MidiOpenDevice(client, devId, &device);
    
    size_t count = 0;
    OH_MidiGetDevicePorts(device, nullptr, &count);
    if (count > 0) {
        OH_MidiPortDescriptor desc = {0, MIDI_PROTOCOL_1_0}; // 尝试 index 0
        if (OH_MidiOpenOutputPort(device, desc) == MIDI_STATUS_OK) {
            
            // Case 1: Valid Flush
            OH_MidiStatusCode ret = OH_MidiFlushOutputPort(device, 0);
            EXPECT_EQ(ret, MIDI_STATUS_OK);

            OH_MidiClosePort(device, 0);
        }
        
        // Case 2: Flush Closed Port (Negative)
        OH_MidiStatusCode ret = OH_MidiFlushOutputPort(device, 0);

        EXPECT_NE(ret, MIDI_STATUS_GENERIC_IPC_FAILURE);
    }

    OH_MidiCloseDevice(device);
}

/**
 * @tc.number    : SUB_MULTIMEDIA_MIDI_DESTROY_0100
 * @tc.name      : Test OH_MidiClientDestroy robustness
 * @tc.desc      : Verify destroying null or invalid clients.
 */
HWTEST_F(MidiSdkTest, MidiClient_Destroy_Negative_001, Function | MediumTest | Level1) {
    // Case 1: Destroy Nullptr
    OH_MidiStatusCode ret = OH_MidiClientDestroy(nullptr);
    EXPECT_EQ(ret, MIDI_STATUS_INVALID_CLIENT);

    // Case 2: Double Destroy is NOT safe to test directly if it crashes,
    // but we can ensure logic flow.
    // In SetUp/TearDown pattern, client is destroyed at TearDown.
    // Here we manually destroy it to test success.
    if (client) {
        ret = OH_MidiClientDestroy(client);
        EXPECT_EQ(ret, MIDI_STATUS_OK);
        client = nullptr; // Prevent TearDown from double-freeing
    }
}