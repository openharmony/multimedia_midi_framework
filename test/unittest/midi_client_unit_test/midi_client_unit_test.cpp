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

#include <mutex>
#include <condition_variable>

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "midi_client.h"
#include "midi_client_private.h"
#include "midi_service_interface.h"

using namespace OHOS;
using namespace MIDI;
using namespace testing;
using namespace testing::ext;

namespace {

static MidiEventInner MakeMidiEventInner(uint64_t timestamp, const std::vector<uint32_t> &payloadWords)
{
    MidiEventInner midiEventInner{};
    midiEventInner.timestamp = timestamp;
    midiEventInner.length = payloadWords.size();
    midiEventInner.data = payloadWords.data();
    return midiEventInner;
}

class CallbackCapture {
public:
    void OnReceived(const OH_MIDIEvent *events, uint32_t eventCount)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        receivedCount_ += eventCount;
        lastEventCount_ = eventCount;
        lastEvents_.clear();
        lastEvents_.reserve(eventCount);
        for (uint32_t index = 0; index < eventCount; ++index) {
            lastEvents_.push_back(events[index]);
        }
        condition_.notify_all();
    }

    bool WaitForAtLeast(uint32_t expectedCount, std::chrono::milliseconds timeout)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        return condition_.wait_for(lock, timeout, [this, expectedCount]() { return receivedCount_ >= expectedCount; });
    }

    uint32_t GetReceivedCount() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return receivedCount_;
    }

    uint32_t GetLastEventCount() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return lastEventCount_;
    }

    std::vector<OH_MIDIEvent> GetLastEvents() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return lastEvents_;
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    uint32_t receivedCount_ = 0;
    uint32_t lastEventCount_ = 0;
    std::vector<OH_MIDIEvent> lastEvents_;
};

static void MidiReceivedTrampoline(void *userData, const OH_MIDIEvent *events, size_t eventCount)
{
    auto *capture = reinterpret_cast<CallbackCapture *>(userData);
    if (capture != nullptr) {
        capture->OnReceived(events, eventCount);
    }
}

struct ApiCallbackCapture {
    uint32_t deviceChangeCount = 0;
    uint32_t errorCount = 0;
    uint32_t deviceOpenCount = 0;
    bool lastOpened = false;
    OH_MIDIDevice *lastDevice = nullptr;
    OH_MIDIDeviceInformation lastDeviceInfo {};
    OH_MIDIStatusCode lastError = OH_MIDI_STATUS_OK;
};

static void DeviceChangeTrampoline(
    void *userData, OH_MIDIDeviceChangeAction, OH_MIDIDeviceInformation deviceInfo)
{
    auto *capture = static_cast<ApiCallbackCapture *>(userData);
    if (capture != nullptr) {
        capture->deviceChangeCount++;
        capture->lastDeviceInfo = deviceInfo;
    }
}

static void ErrorTrampoline(void *userData, OH_MIDIStatusCode code)
{
    auto *capture = static_cast<ApiCallbackCapture *>(userData);
    if (capture != nullptr) {
        capture->errorCount++;
        capture->lastError = code;
    }
}

static void DeviceOpenedTrampoline(
    void *userData, bool opened, OH_MIDIDevice *device, OH_MIDIDeviceInformation deviceInfo)
{
    auto *capture = static_cast<ApiCallbackCapture *>(userData);
    if (capture != nullptr) {
        capture->deviceOpenCount++;
        capture->lastOpened = opened;
        capture->lastDevice = device;
        capture->lastDeviceInfo = deviceInfo;
    }
}

}  // namespace

class MidiServiceMock : public MidiServiceInterface {
public:
    MOCK_METHOD(OH_MIDIStatusCode, Init, (sptr<MidiCallbackStub> callback, uint32_t &clientId), (override));
    MOCK_METHOD(OH_MIDIStatusCode, GetDevices, ((std::vector<MidiDeviceInfo>)&deviceInfos), (override));
    MOCK_METHOD(OH_MIDIStatusCode, OpenDevice,
        (int64_t deviceId, (MidiDeviceInfo &deviceInfo)), (override));
    MOCK_METHOD(OH_MIDIStatusCode, OpenBleDevice,
        (std::string address, sptr<MidiDeviceOpenCallbackStub> callback), (override));
    MOCK_METHOD(OH_MIDIStatusCode, CloseDevice, (int64_t deviceId), (override));
    MOCK_METHOD(OH_MIDIStatusCode, GetDevicePorts,
        (int64_t deviceId, std::vector<MidiPortInfo> &portInfos), (override));
    MOCK_METHOD(OH_MIDIStatusCode, OpenInputPort,
        ((std::shared_ptr<MidiSharedRing>)&buffer, int64_t deviceId, uint32_t portIndex), (override));
    MOCK_METHOD(OH_MIDIStatusCode, OpenOutputPort,
        ((std::shared_ptr<MidiSharedRing>)&buffer, int64_t deviceId, uint32_t portIndex), (override));
    MOCK_METHOD(OH_MIDIStatusCode, FlushOutputPort, (int64_t deviceId, uint32_t portIndex), (override));
    MOCK_METHOD(OH_MIDIStatusCode, CloseInputPort, (int64_t deviceId, uint32_t portIndex), (override));
    MOCK_METHOD(OH_MIDIStatusCode, CloseOutputPort, (int64_t deviceId, uint32_t portIndex), (override));
    MOCK_METHOD(OH_MIDIStatusCode, DestroyMidiClient, (), (override));
};

class MidiClientUnitTest : public testing::Test {
public:
    void SetUp() override
    {
        mockService = std::make_shared<MidiServiceMock>();
        client = std::make_unique<MidiClientPrivate>();
        client->ipc_ = mockService;
    }
    void TearDown() override
    {
        client.reset();
        mockService.reset();
    }
    std::shared_ptr<MidiServiceMock> mockService;
    std::unique_ptr<MidiClientPrivate> client;
};

/**
 * @tc.name: CreateMidiClient_AllOutcomes001
 * @tc.desc: Verify the null output guard and ownership for either real-service initialization outcome.
 * @tc.type: FUNC
 */
HWTEST_F(MidiClientUnitTest, CreateMidiClient_AllOutcomes001, TestSize.Level0)
{
    OH_MIDICallbacks callbacks {DeviceChangeTrampoline, ErrorTrampoline};
    ApiCallbackCapture capture;
    EXPECT_EQ(MidiClient::CreateMidiClient(nullptr, callbacks, &capture), OH_MIDI_STATUS_SYSTEM_ERROR);

    MidiClient *createdClient = nullptr;
    OH_MIDIStatusCode ret = MidiClient::CreateMidiClient(&createdClient, callbacks, &capture);
    if (ret == OH_MIDI_STATUS_OK) {
        ASSERT_NE(createdClient, nullptr);
        EXPECT_EQ(createdClient->DestroyMidiClient(), OH_MIDI_STATUS_OK);
        delete createdClient;
    } else {
        EXPECT_EQ(createdClient, nullptr);
    }
}

/**
 * @tc.name: InitAndQueryFailures001
 * @tc.desc: Verify service errors propagate through initialization and collection queries.
 * @tc.type: FUNC
 */
HWTEST_F(MidiClientUnitTest, InitAndQueryFailures001, TestSize.Level0)
{
    OH_MIDICallbacks callbacks {DeviceChangeTrampoline, ErrorTrampoline};
    ApiCallbackCapture capture;
    EXPECT_CALL(*mockService, Init(_, _)).WillOnce(Return(OH_MIDI_STATUS_SYSTEM_ERROR));
    EXPECT_EQ(client->Init(callbacks, &capture), OH_MIDI_STATUS_SYSTEM_ERROR);

    EXPECT_CALL(*mockService, GetDevices(_)).WillOnce(Return(OH_MIDI_STATUS_SYSTEM_ERROR));
    size_t deviceCount = 0;
    EXPECT_EQ(client->GetDevices(nullptr, &deviceCount), OH_MIDI_STATUS_SYSTEM_ERROR);

    EXPECT_CALL(*mockService, GetDevicePorts(1, _)).WillOnce(Return(OH_MIDI_STATUS_SYSTEM_ERROR));
    size_t portCount = 0;
    EXPECT_EQ(client->GetDevicePorts(1, nullptr, &portCount), OH_MIDI_STATUS_SYSTEM_ERROR);
}

/**
 * @tc.name: OpenDevice_001
 * @tc.desc: Test opening device.
 * @tc.type: FUNC
 */
HWTEST_F(MidiClientUnitTest, OpenDevice_001, TestSize.Level0)
{
    int64_t deviceId = 100;
    MidiDevice *device = nullptr;

    // Expect OpenDevice to be called twice on the IPC layer
    EXPECT_CALL(*mockService, OpenDevice(deviceId, _)).WillOnce(Invoke([](int64_t, MidiDeviceInfo &info) {
        info.deviceId = 1001;
        info.deviceType = DeviceType::DEVICE_TYPE_USB;
        info.transportProtocol = TransportProtocol::PROTOCOL_1_0;
        info.address = "";
        info.deviceName = "Mock_Piano";
        info.vendorId = 17169;
        info.productId = 4660;
        return OH_MIDI_STATUS_OK;
    }));

    EXPECT_EQ(client->OpenDevice(deviceId, &device), OH_MIDI_STATUS_OK);
    EXPECT_NE(device, nullptr);
    EXPECT_CALL(*mockService, CloseDevice(1001)).WillOnce(Return(OH_MIDI_STATUS_OK));
    client->CloseAndRemoveDevice(device);
}

/**
 * @tc.name: GetDevice_001
 * @tc.desc: Test getting device list with multiple devices.
 * @tc.type: FUNC
 */
HWTEST_F(MidiClientUnitTest, GetDevices_001, TestSize.Level0)
{
    // 1. Prepare mock data from IPC
    EXPECT_CALL(*mockService, Init(_, _)).Times(1).WillOnce(Return(OH_MIDI_STATUS_OK));
    EXPECT_CALL(*mockService, GetDevices(_)).WillOnce(Invoke([](std::vector<MidiDeviceInfo> &infos) {
        MidiDeviceInfo dev1;
        dev1.deviceId = 1001;
        dev1.deviceType = DeviceType::DEVICE_TYPE_USB;
        dev1.transportProtocol = TransportProtocol::PROTOCOL_1_0;
        dev1.address = "";
        dev1.deviceName = "Mock_Piano";
        dev1.vendorId = 17169;
        dev1.productId = 4660;
        infos.push_back(dev1);

        MidiDeviceInfo dev2;
        dev2.deviceId = 1002;
        dev2.deviceType = DeviceType::DEVICE_TYPE_BLE;
        dev2.transportProtocol = TransportProtocol::PROTOCOL_1_0;
        dev2.address = "aabbcc";
        dev2.deviceName = "Mock_Drum";
        dev2.vendorId = 17185;
        dev2.productId = 22136;
        infos.push_back(dev2);

        return OH_MIDI_STATUS_OK;
    }));
    OH_MIDICallbacks callbacks;
    callbacks.onDeviceChange =
        [](void *userData, OH_MIDIDeviceChangeAction action, OH_MIDIDeviceInformation deviceInfo) {};
    callbacks.onError = [](void *userData, OH_MIDIStatusCode code) {
    };
    void *userData = nullptr;
    client->Init(callbacks, userData);
    OH_MIDIDeviceInformation infoArray[2];
    size_t numDevices = 2;
    OH_MIDIStatusCode status = client->GetDevices(infoArray, &numDevices);

    // 3. Verify
    EXPECT_EQ(status, OH_MIDI_STATUS_OK);
    EXPECT_EQ(numDevices, 2);
    EXPECT_EQ(infoArray[0].midiDeviceId, 1001);
    EXPECT_EQ(infoArray[0].deviceType, OH_MIDI_DEVICE_TYPE_USB);
    EXPECT_EQ(infoArray[0].nativeProtocol, OH_MIDI_PROTOCOL_1_0);
    EXPECT_STREQ(infoArray[0].deviceName, "Mock_Piano");
    EXPECT_EQ(infoArray[0].vendorId, 17169);
    EXPECT_EQ(infoArray[0].productId, 4660);
    EXPECT_STREQ(infoArray[0].deviceAddress, "");
    EXPECT_EQ(infoArray[1].midiDeviceId, 1002);
    EXPECT_EQ(infoArray[1].deviceType, OH_MIDI_DEVICE_TYPE_BLE);
    EXPECT_EQ(infoArray[1].nativeProtocol, OH_MIDI_PROTOCOL_1_0);
    EXPECT_STREQ(infoArray[1].deviceName, "Mock_Drum");
    EXPECT_EQ(infoArray[1].vendorId, 17185);
    EXPECT_EQ(infoArray[1].productId, 22136);
    EXPECT_STREQ(infoArray[1].deviceAddress, "aabbcc");
}

/**
 * @tc.name: GetDevices_002
 * @tc.desc: Test GetDevices with silent fill mode when buffer is smaller than available devices.
 * @tc.type: FUNC
 */
HWTEST_F(MidiClientUnitTest, GetDevices_002, TestSize.Level0)
{
    EXPECT_CALL(*mockService, Init(_, _)).Times(1).WillOnce(Return(OH_MIDI_STATUS_OK));
    EXPECT_CALL(*mockService, GetDevices(_)).WillOnce(Invoke([](std::vector<MidiDeviceInfo> &infos) {
        MidiDeviceInfo dev1;
        dev1.deviceId = 1001;
        dev1.deviceType = DeviceType::DEVICE_TYPE_USB;
        dev1.transportProtocol = TransportProtocol::PROTOCOL_1_0;
        dev1.address = "";
        dev1.deviceName = "Mock_Piano";
        dev1.vendorId = 17169;
        dev1.productId = 4660;
        infos.push_back(dev1);

        MidiDeviceInfo dev2;
        dev2.deviceId = 1002;
        dev2.deviceType = DeviceType::DEVICE_TYPE_BLE;
        dev2.transportProtocol = TransportProtocol::PROTOCOL_1_0;
        dev2.address = "aabbcc";
        dev2.deviceName = "Mock_Drum";
        dev2.vendorId = 17185;
        dev2.productId = 22136;
        infos.push_back(dev2);

        return OH_MIDI_STATUS_OK;
    }));
    OH_MIDICallbacks callbacks;
    callbacks.onDeviceChange =
        [](void *userData, OH_MIDIDeviceChangeAction action, OH_MIDIDeviceInformation deviceInfo) {};
    callbacks.onError = [](void *userData, OH_MIDIStatusCode code) {
    };
    void *userData = nullptr;
    client->Init(callbacks, userData);
    OH_MIDIDeviceInformation infoArrayTest[1];  // Only 1 slot, but 2 devices available
    size_t numDevices = 1;
    OH_MIDIStatusCode status = client->GetDevices(infoArrayTest, &numDevices);

    // Silent fill mode: should return OK and fill only what capacity allows
    EXPECT_EQ(status, OH_MIDI_STATUS_OK);
    EXPECT_EQ(numDevices, 1);  // Actual filled count
    EXPECT_EQ(infoArrayTest[0].midiDeviceId, 1001);
    EXPECT_STREQ(infoArrayTest[0].deviceName, "Mock_Piano");

    // Verify with full buffer
    EXPECT_CALL(*mockService, GetDevices(_)).WillOnce(Invoke([](std::vector<MidiDeviceInfo> &infos) {
        MidiDeviceInfo dev1;
        dev1.deviceId = 1001;
        dev1.deviceType = DeviceType::DEVICE_TYPE_USB;
        dev1.transportProtocol = TransportProtocol::PROTOCOL_1_0;
        dev1.address = "";
        dev1.deviceName = "Mock_Piano";
        dev1.vendorId = 17169;
        dev1.productId = 4660;
        infos.push_back(dev1);

        MidiDeviceInfo dev2;
        dev2.deviceId = 1002;
        dev2.deviceType = DeviceType::DEVICE_TYPE_BLE;
        dev2.transportProtocol = TransportProtocol::PROTOCOL_1_0;
        dev2.address = "aabbcc";
        dev2.deviceName = "Mock_Drum";
        dev2.vendorId = 17185;
        dev2.productId = 22136;
        infos.push_back(dev2);

        return OH_MIDI_STATUS_OK;
    }));
    OH_MIDIDeviceInformation infoArray[2];
    numDevices = 2;
    status = client->GetDevices(infoArray, &numDevices);

    // Verify all devices are filled when buffer is sufficient
    EXPECT_EQ(status, OH_MIDI_STATUS_OK);
    EXPECT_EQ(numDevices, 2);
    EXPECT_EQ(infoArray[0].midiDeviceId, 1001);
    EXPECT_EQ(infoArray[0].deviceType, OH_MIDI_DEVICE_TYPE_USB);
    EXPECT_EQ(infoArray[0].nativeProtocol, OH_MIDI_PROTOCOL_1_0);
    EXPECT_STREQ(infoArray[0].deviceName, "Mock_Piano");
    EXPECT_EQ(infoArray[0].vendorId, 17169);
    EXPECT_EQ(infoArray[0].productId, 4660);
    EXPECT_STREQ(infoArray[0].deviceAddress, "");
    EXPECT_EQ(infoArray[1].midiDeviceId, 1002);
    EXPECT_EQ(infoArray[1].deviceType, OH_MIDI_DEVICE_TYPE_BLE);
    EXPECT_EQ(infoArray[1].nativeProtocol, OH_MIDI_PROTOCOL_1_0);
    EXPECT_STREQ(infoArray[1].deviceName, "Mock_Drum");
    EXPECT_EQ(infoArray[1].vendorId, 17185);
    EXPECT_EQ(infoArray[1].productId, 22136);
    EXPECT_STREQ(infoArray[1].deviceAddress, "aabbcc");
}

/**
 * @tc.name: GetDevicePorts_001
 * @tc.desc: Test getting port information for a specific device.
 * @tc.type: FUNC
 */
HWTEST_F(MidiClientUnitTest, GetDevicePorts_001, TestSize.Level0)
{
    int64_t deviceId = 1001;

    EXPECT_CALL(*mockService, GetDevicePorts(deviceId, _))
        .WillOnce(Invoke([](int64_t id, std::vector<MidiPortInfo> &ports) {
            MidiPortInfo port1;
            port1.portId = 0;
            port1.name = "Midi_In_Port";
            port1.direction = PortDirection::PORT_DIRECTION_INPUT;
            port1.transportProtocol = TransportProtocol::PROTOCOL_1_0;
            ports.push_back(port1);

            MidiPortInfo port2;
            port2.portId = 1;
            port2.name = "Midi_Out_Port";
            port2.direction = PortDirection::PORT_DIRECTION_OUTPUT;
            port2.transportProtocol = TransportProtocol::PROTOCOL_1_0;
            ports.push_back(port2);

            return OH_MIDI_STATUS_OK;
        }));
    OH_MIDIPortInformation portArray[2];
    size_t numPorts = 2;
    OH_MIDIDeviceInformation info;
    info.midiDeviceId = 1001;
    auto device = std::make_shared<MidiDevicePrivate>(mockService, info);
    client->AddDeviceHandler(device);
    OH_MIDIStatusCode status = client->GetDevicePorts(deviceId, portArray, &numPorts);

    EXPECT_EQ(status, OH_MIDI_STATUS_OK);
    EXPECT_EQ(numPorts, 2);
    EXPECT_EQ(portArray[0].portIndex, 0);
    EXPECT_EQ(portArray[0].deviceId, 1001);
    EXPECT_EQ(portArray[0].direction, OH_MIDI_PORT_DIRECTION_INPUT);
    EXPECT_STREQ(portArray[0].name, "Midi_In_Port");
    EXPECT_EQ(portArray[1].portIndex, 1);
    EXPECT_EQ(portArray[1].deviceId, 1001);
    EXPECT_EQ(portArray[1].direction, OH_MIDI_PORT_DIRECTION_OUTPUT);
    EXPECT_STREQ(portArray[1].name, "Midi_Out_Port");
}

/**
 * @tc.name: GetDeviceCount_WithZeroInitialValue
 * @tc.desc: Test GetDevices with nullptr infos and zero initial count returns actual count.
 * @tc.type: FUNC
 */
HWTEST_F(MidiClientUnitTest, GetDeviceCount_WithZeroInitialValue, TestSize.Level0)
{
    EXPECT_CALL(*mockService, GetDevices(_)).WillOnce(Invoke([](std::vector<MidiDeviceInfo> &infos) {
        MidiDeviceInfo dev1;
        dev1.deviceId = 1001;
        dev1.deviceType = DeviceType::DEVICE_TYPE_USB;
        dev1.transportProtocol = TransportProtocol::PROTOCOL_1_0;
        dev1.address = "";
        dev1.deviceName = "Mock_Piano";
        dev1.vendorId = 17169;
        dev1.productId = 4660;
        infos.push_back(dev1);

        MidiDeviceInfo dev2;
        dev2.deviceId = 1002;
        dev2.deviceType = DeviceType::DEVICE_TYPE_BLE;
        dev2.transportProtocol = TransportProtocol::PROTOCOL_1_0;
        dev2.address = "aabbcc";
        dev2.deviceName = "Mock_Drum";
        dev2.vendorId = 17185;
        dev2.productId = 22136;
        infos.push_back(dev2);

        return OH_MIDI_STATUS_OK;
    }));
    size_t numDevices = 0;  // Start with zero
    OH_MIDIStatusCode status = client->GetDevices(nullptr, &numDevices);

    EXPECT_EQ(status, OH_MIDI_STATUS_OK);
    EXPECT_EQ(numDevices, 2);  // Should return actual count
}

/**
 * @tc.name: GetPortCount_WithZeroInitialValue
 * @tc.desc: Test GetDevicePorts with nullptr infos and zero initial count returns actual count.
 * @tc.type: FUNC
 */
HWTEST_F(MidiClientUnitTest, GetPortCount_WithZeroInitialValue, TestSize.Level0)
{
    int64_t deviceId = 1001;

    EXPECT_CALL(*mockService, GetDevicePorts(deviceId, _))
        .WillOnce(Invoke([](int64_t id, std::vector<MidiPortInfo> &ports) {
            ports.clear();
            MidiPortInfo port1;
            port1.portId = 0;
            port1.name = "Midi_In_Port";
            port1.direction = PortDirection::PORT_DIRECTION_INPUT;
            port1.transportProtocol = TransportProtocol::PROTOCOL_1_0;
            ports.push_back(port1);

            MidiPortInfo port2;
            port2.portId = 1;
            port2.name = "Midi_Out_Port";
            port2.direction = PortDirection::PORT_DIRECTION_OUTPUT;
            port2.transportProtocol = TransportProtocol::PROTOCOL_1_0;
            ports.push_back(port2);
            return OH_MIDI_STATUS_OK;
        }));

    OH_MIDIDeviceInformation info;
    info.midiDeviceId = 1001;
    auto device = std::make_shared<MidiDevicePrivate>(mockService, info);
    client->AddDeviceHandler(device);
    size_t numPorts = 0;  // Start with zero
    OH_MIDIStatusCode status = client->GetDevicePorts(deviceId, nullptr, &numPorts);

    EXPECT_EQ(status, OH_MIDI_STATUS_OK);
    EXPECT_EQ(numPorts, 2);  // Should return actual count
}

/**
 * @tc.name: GetDevicePorts_002
 * @tc.desc: Test GetDevicePorts when the device ID is invalid.
 * @tc.type: FUNC
 */
HWTEST_F(MidiClientUnitTest, GetDevicePorts_002, TestSize.Level0)
{
    int64_t invalidId = -1;
    OH_MIDIPortInformation portArray[1];
    size_t numPorts = 1;
    EXPECT_CALL(*mockService, GetDevicePorts(invalidId, _))
        .WillOnce(Invoke([](int64_t id, std::vector<MidiPortInfo> &ports) {
            return OH_MIDI_STATUS_GENERIC_INVALID_ARGUMENT;
        }));
    OH_MIDIStatusCode status = client->GetDevicePorts(invalidId, portArray, &numPorts);
    EXPECT_EQ(status, OH_MIDI_STATUS_GENERIC_INVALID_ARGUMENT);
}

/**
 * @tc.name: CloseInputPort_001
 * @tc.desc: Test closing an input port that was never opened.
 * @tc.type: FUNC
 */
HWTEST_F(MidiClientUnitTest, CloseInputPort_001, TestSize.Level0)
{
    OH_MIDIDeviceInformation info;
    info.midiDeviceId = 102;
    uint32_t portIndex = 5;
    auto device = std::make_unique<MidiDevicePrivate>(mockService, info);

    EXPECT_EQ(device->CloseInputPort(portIndex), OH_MIDI_STATUS_INVALID_PORT);
}

/**
 * @tc.name: CloseOutputPort_001
 * @tc.desc: Test closing an output port that was never opened.
 * @tc.type: FUNC
 */
HWTEST_F(MidiClientUnitTest, CloseOutputPort_001, TestSize.Level0)
{
    uint32_t portIndex = 5;
    OH_MIDIDeviceInformation info;
    info.midiDeviceId = 102;
    auto device = std::make_unique<MidiDevicePrivate>(mockService, info);

    EXPECT_EQ(device->CloseOutputPort(portIndex), OH_MIDI_STATUS_INVALID_PORT);
}

/**
 * @tc.name: MidiDevicePrivate_CloseDevice_001
 * @tc.desc: CloseDevice should forward to IPC CloseDevice.
 * @tc.type: FUNC
 */
HWTEST_F(MidiClientUnitTest, MidiDevicePrivate_CloseDevice_001, TestSize.Level0)
{
    OH_MIDIDeviceInformation info;
    info.midiDeviceId = 1001;
    auto device = std::make_unique<MidiDevicePrivate>(mockService, info);

    EXPECT_CALL(*mockService, CloseDevice(info.midiDeviceId)).Times(1).WillOnce(Return(OH_MIDI_STATUS_OK));

    EXPECT_EQ(device->CloseDevice(), OH_MIDI_STATUS_OK);
}

/**
 * @tc.name: MidiDevicePrivate_OpenInputPort_001
 * @tc.desc: OpenInputPort success: IPC OpenInputPort called once, receiver thread starts, ClosePort stops and closes.
 * @tc.type: FUNC
 */
HWTEST_F(MidiClientUnitTest, MidiDevicePrivate_OpenInputPort_001, TestSize.Level0)
{
    OH_MIDIDeviceInformation info;
    info.midiDeviceId = 2001;
    uint32_t portIndex = 0;
    auto device = std::make_unique<MidiDevicePrivate>(mockService, info);
    OH_MIDIPortDescriptor descriptor;
    descriptor.portIndex = portIndex;
    descriptor.protocol = OH_MIDI_PROTOCOL_1_0;
    CallbackCapture callbackCapture;

    EXPECT_CALL(*mockService, OpenInputPort(_, info.midiDeviceId, portIndex))
        .Times(1)
        .WillOnce(Invoke([](std::shared_ptr<MidiSharedRing> &buffer, int64_t, uint32_t) {
            buffer = MidiSharedRing::CreateFromLocal(256);
            return (buffer != nullptr) ? OH_MIDI_STATUS_OK : OH_MIDI_STATUS_SYSTEM_ERROR;
        }));

    EXPECT_CALL(*mockService, CloseInputPort(info.midiDeviceId, portIndex))
        .Times(1).WillOnce(Return(OH_MIDI_STATUS_OK));

    // Open input port -> should start receiver thread internally
    OH_MIDIStatusCode openStatus = device->OpenInputPort(descriptor, MidiReceivedTrampoline, &callbackCapture);
    EXPECT_EQ(openStatus, OH_MIDI_STATUS_OK);

    // Close port -> should stop thread (via MidiInputPort destructor) and call IPC CloseInputPort
    OH_MIDIStatusCode closeStatus = device->CloseInputPort(portIndex);
    EXPECT_EQ(closeStatus, OH_MIDI_STATUS_OK);

    EXPECT_EQ(device->CloseInputPort(portIndex), OH_MIDI_STATUS_INVALID_PORT);
}

/**
 * @tc.name: MidiDevicePrivate_OpenInputPort_002
 * @tc.desc: OpenInputPort called twice on same portIndex should return OK without calling IPC second time.
 * @tc.type: FUNC
 */
HWTEST_F(MidiClientUnitTest, MidiDevicePrivate_OpenInputPort_002, TestSize.Level0)
{
    OH_MIDIDeviceInformation info;
    info.midiDeviceId = 2002;
    uint32_t portIndex = 1;
    auto device = std::make_unique<MidiDevicePrivate>(mockService, info);
    OH_MIDIPortDescriptor descriptor;
    descriptor.portIndex = portIndex;
    descriptor.protocol = OH_MIDI_PROTOCOL_1_0;
    CallbackCapture callbackCapture;

    EXPECT_CALL(*mockService, OpenInputPort(_, info.midiDeviceId, portIndex))
        .Times(1)
        .WillOnce(Invoke([](std::shared_ptr<MidiSharedRing> &buffer, int64_t, uint32_t) {
            buffer = MidiSharedRing::CreateFromLocal(256);
            return OH_MIDI_STATUS_OK;
        }));

    EXPECT_CALL(*mockService, CloseInputPort(info.midiDeviceId, portIndex))
        .Times(1).WillOnce(Return(OH_MIDI_STATUS_OK));

    EXPECT_EQ(device->OpenInputPort(descriptor, MidiReceivedTrampoline, &callbackCapture), OH_MIDI_STATUS_OK);
    // Second time should hit "already exists" branch and return ALREADY_OPEN without IPC.
    EXPECT_EQ(device->OpenInputPort(descriptor, MidiReceivedTrampoline, &callbackCapture),
        OH_MIDI_STATUS_PORT_ALREADY_OPEN);
    EXPECT_EQ(device->CloseInputPort(portIndex), OH_MIDI_STATUS_OK);
}

/**
 * @tc.name: MidiDevicePrivate_OpenInputPort_003
 * @tc.desc: OpenInputPort should return IPC error when OpenInputPort IPC fails.
 * @tc.type: FUNC
 */
HWTEST_F(MidiClientUnitTest, MidiDevicePrivate_OpenInputPort_003, TestSize.Level0)
{
    OH_MIDIDeviceInformation info;
    info.midiDeviceId = 2003;
    uint32_t portIndex = 2;
    auto device = std::make_unique<MidiDevicePrivate>(mockService, info);
    OH_MIDIPortDescriptor descriptor;
    descriptor.portIndex = portIndex;
    descriptor.protocol = OH_MIDI_PROTOCOL_1_0;
    CallbackCapture callbackCapture;

    EXPECT_CALL(*mockService, OpenInputPort(_, info.midiDeviceId, portIndex))
        .Times(1)
        .WillOnce(Return(OH_MIDI_STATUS_GENERIC_INVALID_ARGUMENT));

    OH_MIDIStatusCode status = device->OpenInputPort(descriptor, MidiReceivedTrampoline, &callbackCapture);
    EXPECT_EQ(status, OH_MIDI_STATUS_GENERIC_INVALID_ARGUMENT);

    EXPECT_EQ(device->CloseInputPort(portIndex), OH_MIDI_STATUS_INVALID_PORT);
}

/**
 * @tc.name: MidiInputPort_StartStop_001
 * @tc.desc: StartReceiverThread should fail if ringBuffer or callback is nullptr; Stop should be idempotent.
 * @tc.type: FUNC
 */
HWTEST_F(MidiClientUnitTest, MidiInputPort_StartStop_001, TestSize.Level0)
{
    CallbackCapture callbackCapture;

    // 1) callback is nullptr -> Start should fail
    {
        MidiInputPort inputPort(nullptr, &callbackCapture, MIDI_NONE);
        EXPECT_FALSE(inputPort.StartReceiverThread());
        EXPECT_TRUE(inputPort.StopReceiverThread());  // should be safe even if never started
    }

    // 2) ringBuffer is nullptr -> Start should fail
    {
        MidiInputPort inputPort(MidiReceivedTrampoline, &callbackCapture, MIDI_NONE);
        // ringBuffer_ is nullptr by default
        EXPECT_FALSE(inputPort.StartReceiverThread());
        EXPECT_TRUE(inputPort.StopReceiverThread());
        EXPECT_TRUE(inputPort.StopReceiverThread());  // idempotent
    }
}

/**
 * @tc.name: MidiInputPort_ReceiverDispatch_001
 * @tc.desc: StartReceiverThread + write to ring should wake and invoke callback (DrainRingAndDispatch path).
 * @tc.type: FUNC
 */
HWTEST_F(MidiClientUnitTest, MidiInputPort_ReceiverDispatch_001, TestSize.Level0)
{
    CallbackCapture callbackCapture;

    MidiInputPort inputPort(MidiReceivedTrampoline, &callbackCapture, MIDI_NONE);
    std::shared_ptr<MidiSharedRing> localRing = MidiSharedRing::CreateFromLocal(512);
    ASSERT_NE(localRing, nullptr);

    // Assign ring buffer (public ref getter)
    inputPort.GetRingBuffer() = localRing;

    ASSERT_TRUE(inputPort.StartReceiverThread());

    // Write one event with notify=true to wake futex waiter
    std::vector<uint32_t> payloadWords{0x20905837, 0x20905937, 0x20905a37};
    MidiEventInner midiEventInner = MakeMidiEventInner(10, payloadWords);

    ASSERT_EQ(MidiStatusCode::OK, localRing->TryWriteEvent(midiEventInner, true));

    // Wait callback
    ASSERT_TRUE(callbackCapture.WaitForAtLeast(1, std::chrono::milliseconds(200)));
    EXPECT_GE(callbackCapture.GetReceivedCount(), 1u);
    EXPECT_EQ(callbackCapture.GetLastEventCount(), 3u);

    auto lastEvents = callbackCapture.GetLastEvents();
    ASSERT_EQ(lastEvents.size(), 3u);
    EXPECT_EQ(lastEvents[0].timestamp, 10u);
    EXPECT_EQ(lastEvents[0].length, 1u);

    // Stop thread
    EXPECT_TRUE(inputPort.StopReceiverThread());
    EXPECT_TRUE(inputPort.StopReceiverThread());
}

/**
 * @tc.name: MidiInputPort_StartReceiverThread_002
 * @tc.desc: StartReceiverThread should fail if called twice (already start branch).
 * @tc.type: FUNC
 */
HWTEST_F(MidiClientUnitTest, MidiInputPort_StartReceiverThread_002, TestSize.Level0)
{
    CallbackCapture callbackCapture;

    MidiInputPort inputPort(MidiReceivedTrampoline, &callbackCapture, MIDI_NONE);
    std::shared_ptr<MidiSharedRing> localRing = MidiSharedRing::CreateFromLocal(256);
    ASSERT_NE(localRing, nullptr);

    inputPort.GetRingBuffer() = localRing;

    ASSERT_TRUE(inputPort.StartReceiverThread());
    EXPECT_FALSE(inputPort.StartReceiverThread());

    EXPECT_TRUE(inputPort.StopReceiverThread());
}

/**
 * @tc.name: MidiOutputPort_SendSysEx_001
 * @tc.desc: test one packet case
 * @tc.type: FUNC
 */
HWTEST_F(MidiClientUnitTest, MidiOutputPort_SendSysEx_001, TestSize.Level0)
{
    MidiOutputPort outputPort(MIDI_NONE);
    std::shared_ptr<MidiSharedRing> localRing = MidiSharedRing::CreateFromLocal(256);
    ASSERT_NE(localRing, nullptr);

    outputPort.GetRingBuffer() = localRing;
    uint32_t portIndex = 0;
    uint8_t data[] = {0xF0, 0x01, 0x02, 0x03, 0xF7};
    uint32_t byteSize = sizeof(data);

    std::vector<MidiEvent> midiEvents;
    std::vector<std::vector<uint32_t>> payloadBuffers;

    EXPECT_EQ(OH_MIDI_STATUS_OK, outputPort.SendSysEx(portIndex, data, byteSize));
    localRing->DrainToBatch(midiEvents, payloadBuffers, 0);
    EXPECT_EQ(midiEvents.size(), 1);
    EXPECT_EQ(midiEvents[0].data[0] & 0xFF, 0xF0);
    EXPECT_EQ(midiEvents[0].data[1] & 0xFF, 0x02);
}

/**
 * @tc.name: MidiOutputPort_SendSysEx_002
 * @tc.desc: test time out case
 * @tc.type: FUNC
 */
HWTEST_F(MidiClientUnitTest, MidiOutputPort_SendSysEx_002, TestSize.Level1)
{
    MidiOutputPort outputPort(MIDI_NONE);
    std::shared_ptr<MidiSharedRing> localRing = MidiSharedRing::CreateFromLocal(256);
    ASSERT_NE(localRing, nullptr);

    outputPort.GetRingBuffer() = localRing;
    uint32_t portIndex = 0;
    const uint32_t largeSize = 1024 * 10;
    uint8_t data[largeSize];
    std::fill_n(data, largeSize, 0x01);

    EXPECT_EQ(OH_MIDI_STATUS_TIMEOUT, outputPort.SendSysEx(portIndex, data, largeSize));
}

/**
 * @tc.name: MidiOutputPort_SendSysEx_003
 * @tc.desc: test muti packets case
 * @tc.type: FUNC
 */
HWTEST_F(MidiClientUnitTest, MidiOutputPort_SendSysEx_003, TestSize.Level0)
{
    MidiOutputPort outputPort(MIDI_NONE);
    std::shared_ptr<MidiSharedRing> localRing = MidiSharedRing::CreateFromLocal(256);
    ASSERT_NE(localRing, nullptr);

    outputPort.GetRingBuffer() = localRing;
    uint32_t portIndex = 0;
    uint32_t byteSize = 8;
    uint8_t data[byteSize];
    for (uint32_t i = 0; i < byteSize; ++i) {
        data[i] = i + 1;
    }

    std::vector<MidiEvent> midiEvents;
    std::vector<std::vector<uint32_t>> payloadBuffers;

    EXPECT_EQ(OH_MIDI_STATUS_OK, outputPort.SendSysEx(portIndex, data, byteSize));
    localRing->DrainToBatch(midiEvents, payloadBuffers, 0);
    EXPECT_EQ(midiEvents.size(), 2);
    EXPECT_EQ(midiEvents[0].data[0] & 0xFF, 0x01);
    EXPECT_EQ(midiEvents[0].data[1] & 0xFF, 0x03);
    EXPECT_EQ((midiEvents[0].data[0] >> 20) & 0xF, 0x01);
    EXPECT_EQ((midiEvents[1].data[0] >> 20) & 0xF, 0x03);
}

/**
 * @tc.name: RemoveDeviceHandler_001
 * @tc.desc: RemoveDeviceHandler should remove the exact device pointer from deviceHandlers_.
 * @tc.type: FUNC
 */
HWTEST_F(MidiClientUnitTest, RemoveDeviceHandler_001, TestSize.Level0)
{
    EXPECT_CALL(*mockService, CloseDevice(_)).WillRepeatedly(Return(OH_MIDI_STATUS_OK));

    // Open two devices
    MidiDevice *device1 = nullptr;
    MidiDevice *device2 = nullptr;
    EXPECT_CALL(*mockService, OpenDevice(1001, _)).WillOnce(Invoke([](int64_t, MidiDeviceInfo &info) {
        info.deviceId = 1001;
        info.deviceType = DeviceType::DEVICE_TYPE_USB;
        info.transportProtocol = TransportProtocol::PROTOCOL_1_0;
        info.deviceName = "Device1";
        return OH_MIDI_STATUS_OK;
    }));
    EXPECT_CALL(*mockService, OpenDevice(1002, _)).WillOnce(Invoke([](int64_t, MidiDeviceInfo &info) {
        info.deviceId = 1002;
        info.deviceType = DeviceType::DEVICE_TYPE_USB;
        info.transportProtocol = TransportProtocol::PROTOCOL_1_0;
        info.deviceName = "Device2";
        return OH_MIDI_STATUS_OK;
    }));
    ASSERT_EQ(client->OpenDevice(1001, &device1), OH_MIDI_STATUS_OK);
    ASSERT_EQ(client->OpenDevice(1002, &device2), OH_MIDI_STATUS_OK);
    ASSERT_NE(device1, nullptr);
    ASSERT_NE(device2, nullptr);

    auto *privDevice1 = static_cast<MidiDevicePrivate *>(device1);
    auto *privDevice2 = static_cast<MidiDevicePrivate *>(device2);

    // Remove device1 from handlers
    client->RemoveDeviceHandler(privDevice1);

    // Now trigger MarkDeviceInValid — it should only iterate over device2, not device1
    // If RemoveDeviceHandler failed, this could access a dangling pointer after delete
    client->MarkDeviceInValid();

    // Verify device2 is still valid (was marked invalid but not removed)
    // The pointer should still be accessible without crash
    EXPECT_NE(privDevice2, nullptr);

    // Clean up: use CloseAndRemoveDevice for the remaining device
    client->CloseAndRemoveDevice(device2);
}

/**
 * @tc.name: RemoveDeviceHandler_002
 * @tc.desc: RemoveDeviceHandler with a non-existent pointer should be a no-op.
 * @tc.type: FUNC
 */
HWTEST_F(MidiClientUnitTest, RemoveDeviceHandler_002, TestSize.Level0)
{
    EXPECT_CALL(*mockService, OpenDevice(1001, _)).WillOnce(Invoke([](int64_t, MidiDeviceInfo &info) {
        info.deviceId = 1001;
        info.deviceType = DeviceType::DEVICE_TYPE_USB;
        info.transportProtocol = TransportProtocol::PROTOCOL_1_0;
        info.deviceName = "Device1";
        return OH_MIDI_STATUS_OK;
    }));

    MidiDevice *device = nullptr;
    ASSERT_EQ(client->OpenDevice(1001, &device), OH_MIDI_STATUS_OK);
    ASSERT_NE(device, nullptr);

    // Create a device NOT in deviceHandlers_
    OH_MIDIDeviceInformation info;
    info.midiDeviceId = 9999;
    auto orphanDevice = std::make_unique<MidiDevicePrivate>(mockService, info);

    // Removing a non-tracked device should not crash and not affect existing devices
    client->RemoveDeviceHandler(orphanDevice.get());

    // MarkDeviceInValid should still work on the tracked device
    client->MarkDeviceInValid();

    client->CloseAndRemoveDevice(device);
}

/**
 * @tc.name: RemoveDeviceHandler_UAFProtection_001
 * @tc.desc: After RemoveDeviceHandler + delete, MarkDeviceInValid should not access
 *           the deleted device (UAF protection).
 * @tc.type: FUNC
 */
HWTEST_F(MidiClientUnitTest, RemoveDeviceHandler_UAFProtection_001, TestSize.Level0)
{
    EXPECT_CALL(*mockService, OpenDevice(1001, _)).WillOnce(Invoke([](int64_t, MidiDeviceInfo &info) {
        info.deviceId = 1001;
        info.deviceType = DeviceType::DEVICE_TYPE_USB;
        info.transportProtocol = TransportProtocol::PROTOCOL_1_0;
        info.deviceName = "Device1";
        return OH_MIDI_STATUS_OK;
    }));
    EXPECT_CALL(*mockService, CloseDevice(1001)).WillOnce(Return(OH_MIDI_STATUS_OK));

    MidiDevice *device = nullptr;
    ASSERT_EQ(client->OpenDevice(1001, &device), OH_MIDI_STATUS_OK);
    ASSERT_NE(device, nullptr);

    // Simulate OH_MIDIClient_CloseDevice flow using CloseAndRemoveDevice:
    // This atomically removes from deviceHandlers_ and closes the device.
    // The shared_ptr inside the handler list is released, but the device
    // stays alive until CloseAndRemoveDevice returns.
    EXPECT_EQ(client->CloseAndRemoveDevice(device), OH_MIDI_STATUS_OK);

    // Now if an OnError callback fires and calls MarkDeviceInValid,
    // it should NOT access the removed device (no UAF).
    // The pointer was removed from deviceHandlers_ so it won't be iterated.
    client->MarkDeviceInValid();  // Should be safe, no crash
}

/**
 * @tc.name: CloseAllPorts_001
 * @tc.desc: CloseAllPorts should clear all input/output port maps and stop receiver threads.
 * @tc.type: FUNC
 */
HWTEST_F(MidiClientUnitTest, CloseAllPorts_001, TestSize.Level0)
{
    OH_MIDIDeviceInformation info;
    info.midiDeviceId = 3001;
    info.nativeProtocol = OH_MIDI_PROTOCOL_1_0;
    auto device = std::make_unique<MidiDevicePrivate>(mockService, info);

    OH_MIDIPortDescriptor desc;
    desc.portIndex = 0;
    desc.protocol = OH_MIDI_PROTOCOL_1_0;
    CallbackCapture capture;

    EXPECT_CALL(*mockService, OpenInputPort(_, 3001, 0))
        .WillOnce(Invoke([](std::shared_ptr<MidiSharedRing> &buffer, int64_t, uint32_t) {
            buffer = MidiSharedRing::CreateFromLocal(256);
            return (buffer != nullptr) ? OH_MIDI_STATUS_OK : OH_MIDI_STATUS_SYSTEM_ERROR;
        }));

    ASSERT_EQ(device->OpenInputPort(desc, MidiReceivedTrampoline, &capture), OH_MIDI_STATUS_OK);

    device->CloseAllPorts();

    EXPECT_EQ(device->CloseInputPort(0), OH_MIDI_STATUS_INVALID_PORT);
}

/**
 * @tc.name: OpenInputPort_RollbackOnThreadStartFail_001
 * @tc.desc: When StartReceiverThread fails, OpenInputPort should rollback by calling IPC CloseInputPort.
 * @tc.type: FUNC
 */
HWTEST_F(MidiClientUnitTest, OpenInputPort_RollbackOnThreadStartFail_001, TestSize.Level0)
{
    OH_MIDIDeviceInformation info;
    info.midiDeviceId = 3002;
    info.nativeProtocol = OH_MIDI_PROTOCOL_1_0;
    auto device = std::make_unique<MidiDevicePrivate>(mockService, info);

    OH_MIDIPortDescriptor desc;
    desc.portIndex = 0;
    desc.protocol = OH_MIDI_PROTOCOL_1_0;
    CallbackCapture capture;

    // IPC OpenInputPort returns OK but doesn't set buffer → ringBuffer_ stays nullptr
    EXPECT_CALL(*mockService, OpenInputPort(_, 3002, 0))
        .WillOnce(Return(OH_MIDI_STATUS_OK));

    // StartReceiverThread fails (buffer nullptr) → rollback should call IPC CloseInputPort
    EXPECT_CALL(*mockService, CloseInputPort(3002, 0))
        .Times(1).WillOnce(Return(OH_MIDI_STATUS_OK));

    EXPECT_EQ(device->OpenInputPort(desc, MidiReceivedTrampoline, &capture), OH_MIDI_STATUS_SYSTEM_ERROR);

    // Port should not be in the map
    EXPECT_EQ(device->CloseInputPort(0), OH_MIDI_STATUS_INVALID_PORT);
}

/**
 * @tc.name: HandleDeviceDisconnect_001
 * @tc.desc: HandleDeviceDisconnect should close ports. First CloseInputPort returns OK,
 *           second returns INVALID_PORT. CloseDevice skips IPC.
 * @tc.type: FUNC
 */
HWTEST_F(MidiClientUnitTest, HandleDeviceDisconnect_001, TestSize.Level0)
{
    EXPECT_CALL(*mockService, OpenDevice(4001, _)).WillOnce(Invoke([](int64_t, MidiDeviceInfo &info) {
        info.deviceId = 4001;
        info.deviceType = DeviceType::DEVICE_TYPE_USB;
        info.transportProtocol = TransportProtocol::PROTOCOL_1_0;
        info.deviceName = "TestDevice";
        return OH_MIDI_STATUS_OK;
    }));

    MidiDevice *rawDevice = nullptr;
    ASSERT_EQ(client->OpenDevice(4001, &rawDevice), OH_MIDI_STATUS_OK);
    auto *device = static_cast<MidiDevicePrivate *>(rawDevice);

    OH_MIDIPortDescriptor desc;
    desc.portIndex = 0;
    desc.protocol = OH_MIDI_PROTOCOL_1_0;
    CallbackCapture capture;

    EXPECT_CALL(*mockService, OpenInputPort(_, 4001, 0))
        .WillOnce(Invoke([](std::shared_ptr<MidiSharedRing> &buffer, int64_t, uint32_t) {
            buffer = MidiSharedRing::CreateFromLocal(256);
            return (buffer != nullptr) ? OH_MIDI_STATUS_OK : OH_MIDI_STATUS_SYSTEM_ERROR;
        }));

    ASSERT_EQ(device->OpenInputPort(desc, MidiReceivedTrampoline, &capture), OH_MIDI_STATUS_OK);

    // Simulate disconnect — tombstones ports instead of clearing
    client->HandleDeviceDisconnect(4001);

    // First CloseInputPort: tombstoned entry erased, returns OK (no IPC)
    EXPECT_CALL(*mockService, CloseInputPort(_, _)).Times(0);
    EXPECT_EQ(device->CloseInputPort(0), OH_MIDI_STATUS_OK);

    // Second CloseInputPort: key absent, returns INVALID_PORT
    EXPECT_EQ(device->CloseInputPort(0), OH_MIDI_STATUS_INVALID_PORT);

    // CloseDevice should skip IPC (device is invalid) and return OK
    EXPECT_CALL(*mockService, CloseDevice(_)).Times(0);
    EXPECT_EQ(device->CloseDevice(), OH_MIDI_STATUS_OK);

    // Cleanup: remove from handlers before TearDown destructor runs
    client->CloseAndRemoveDevice(rawDevice);
}

/**
 * @tc.name: HandleDeviceDisconnect_OutputPort_001
 * @tc.desc: After disconnect, first CloseOutputPort returns OK,
 *           second returns INVALID_PORT. No IPC on close.
 * @tc.type: FUNC
 */
HWTEST_F(MidiClientUnitTest, HandleDeviceDisconnect_OutputPort_001, TestSize.Level0)
{
    EXPECT_CALL(*mockService, OpenDevice(4002, _)).WillOnce(Invoke([](int64_t, MidiDeviceInfo &info) {
        info.deviceId = 4002;
        info.deviceType = DeviceType::DEVICE_TYPE_USB;
        info.transportProtocol = TransportProtocol::PROTOCOL_1_0;
        info.deviceName = "TestDevice";
        return OH_MIDI_STATUS_OK;
    }));

    MidiDevice *rawDevice = nullptr;
    ASSERT_EQ(client->OpenDevice(4002, &rawDevice), OH_MIDI_STATUS_OK);
    auto *device = static_cast<MidiDevicePrivate *>(rawDevice);

    OH_MIDIPortDescriptor desc;
    desc.portIndex = 0;
    desc.protocol = OH_MIDI_PROTOCOL_1_0;

    EXPECT_CALL(*mockService, OpenOutputPort(_, 4002, 0))
        .WillOnce(Invoke([](std::shared_ptr<MidiSharedRing> &buffer, int64_t, uint32_t) {
            buffer = MidiSharedRing::CreateFromLocal(256);
            return (buffer != nullptr) ? OH_MIDI_STATUS_OK : OH_MIDI_STATUS_SYSTEM_ERROR;
        }));

    ASSERT_EQ(device->OpenOutputPort(desc), OH_MIDI_STATUS_OK);

    // Simulate disconnect
    client->HandleDeviceDisconnect(4002);

    // First CloseOutputPort: tombstoned entry erased, returns OK (no IPC)
    EXPECT_CALL(*mockService, CloseOutputPort(_, _)).Times(0);
    EXPECT_EQ(device->CloseOutputPort(0), OH_MIDI_STATUS_OK);

    // Second CloseOutputPort: key absent, returns INVALID_PORT
    EXPECT_EQ(device->CloseOutputPort(0), OH_MIDI_STATUS_INVALID_PORT);

    // CloseDevice should skip IPC (device is invalid)
    EXPECT_CALL(*mockService, CloseDevice(_)).Times(0);
    EXPECT_EQ(device->CloseDevice(), OH_MIDI_STATUS_OK);

    client->CloseAndRemoveDevice(rawDevice);
}

/**
 * @tc.name: HandleDeviceDisconnect_UnopenedPort_001
 * @tc.desc: Closing a port that was never opened after disconnect returns INVALID_PORT.
 * @tc.type: FUNC
 */
HWTEST_F(MidiClientUnitTest, HandleDeviceDisconnect_UnopenedPort_001, TestSize.Level0)
{
    EXPECT_CALL(*mockService, OpenDevice(4003, _)).WillOnce(Invoke([](int64_t, MidiDeviceInfo &info) {
        info.deviceId = 4003;
        info.deviceType = DeviceType::DEVICE_TYPE_USB;
        info.transportProtocol = TransportProtocol::PROTOCOL_1_0;
        info.deviceName = "TestDevice";
        return OH_MIDI_STATUS_OK;
    }));

    MidiDevice *rawDevice = nullptr;
    ASSERT_EQ(client->OpenDevice(4003, &rawDevice), OH_MIDI_STATUS_OK);
    auto *device = static_cast<MidiDevicePrivate *>(rawDevice);

    // Simulate disconnect without opening any ports
    client->HandleDeviceDisconnect(4003);

    // Closing a port that was never opened should return INVALID_PORT
    EXPECT_EQ(device->CloseInputPort(0), OH_MIDI_STATUS_INVALID_PORT);
    EXPECT_EQ(device->CloseOutputPort(0), OH_MIDI_STATUS_INVALID_PORT);

    // CloseDevice should skip IPC
    EXPECT_CALL(*mockService, CloseDevice(_)).Times(0);
    EXPECT_EQ(device->CloseDevice(), OH_MIDI_STATUS_OK);

    client->CloseAndRemoveDevice(rawDevice);
}

/**
 * @tc.name: CloseDevice_DisconnectedDevice_001
 * @tc.desc: CloseDevice on an invalid device should return OK without calling IPC.
 * @tc.type: FUNC
 */
HWTEST_F(MidiClientUnitTest, CloseDevice_DisconnectedDevice_001, TestSize.Level0)
{
    OH_MIDIDeviceInformation info;
    info.midiDeviceId = 6001;
    auto device = std::make_unique<MidiDevicePrivate>(mockService, info);

    device->SetInValid();

    EXPECT_CALL(*mockService, CloseDevice(_)).Times(0);
    EXPECT_EQ(device->CloseDevice(), OH_MIDI_STATUS_OK);
}

/**
 * @tc.name: DestructorCleanup_001
 * @tc.desc: ~MidiClientPrivate should delete all devices and stop their receiver threads.
 * @tc.type: FUNC
 */
HWTEST_F(MidiClientUnitTest, DestructorCleanup_001, TestSize.Level0)
{
    EXPECT_CALL(*mockService, OpenDevice(5001, _)).WillOnce(Invoke([](int64_t, MidiDeviceInfo &info) {
        info.deviceId = 5001;
        info.deviceType = DeviceType::DEVICE_TYPE_USB;
        info.transportProtocol = TransportProtocol::PROTOCOL_1_0;
        info.deviceName = "Dev1";
        return OH_MIDI_STATUS_OK;
    }));
    EXPECT_CALL(*mockService, OpenDevice(5002, _)).WillOnce(Invoke([](int64_t, MidiDeviceInfo &info) {
        info.deviceId = 5002;
        info.deviceType = DeviceType::DEVICE_TYPE_USB;
        info.transportProtocol = TransportProtocol::PROTOCOL_1_0;
        info.deviceName = "Dev2";
        return OH_MIDI_STATUS_OK;
    }));

    MidiDevice *device1 = nullptr;
    MidiDevice *device2 = nullptr;
    ASSERT_EQ(client->OpenDevice(5001, &device1), OH_MIDI_STATUS_OK);
    ASSERT_EQ(client->OpenDevice(5002, &device2), OH_MIDI_STATUS_OK);

    // Open input port on device1 to start a receiver thread
    OH_MIDIPortDescriptor desc;
    desc.portIndex = 0;
    desc.protocol = OH_MIDI_PROTOCOL_1_0;
    CallbackCapture capture;

    EXPECT_CALL(*mockService, OpenInputPort(_, 5001, 0))
        .WillOnce(Invoke([](std::shared_ptr<MidiSharedRing> &buffer, int64_t, uint32_t) {
            buffer = MidiSharedRing::CreateFromLocal(256);
            return (buffer != nullptr) ? OH_MIDI_STATUS_OK : OH_MIDI_STATUS_SYSTEM_ERROR;
        }));

    ASSERT_EQ(static_cast<MidiDevicePrivate *>(device1)->OpenInputPort(
        desc, MidiReceivedTrampoline, &capture), OH_MIDI_STATUS_OK);

    // Reset client -> destructor should set destroyed flag, CloseAllPorts, then
    // release shared_ptrs to both devices. No crash, no hang, no leak.
    client.reset();
}

/**
 * @tc.name: CallbackBranches001
 * @tc.desc: Verify client callbacks and disconnect handling.
 * @tc.type: FUNC
 */
HWTEST_F(MidiClientUnitTest, CallbackBranches001, TestSize.Level0)
{
    ApiCallbackCapture capture;
    OH_MIDICallbacks callbacks {DeviceChangeTrampoline, ErrorTrampoline};
    sptr<MidiCallbackStub> serviceCallback;
    EXPECT_CALL(*mockService, Init(_, _))
        .WillOnce(DoAll(SaveArg<0>(&serviceCallback), SetArgReferee<1>(77), Return(OH_MIDI_STATUS_OK)));
    ASSERT_EQ(client->Init(callbacks, &capture), OH_MIDI_STATUS_OK);
    ASSERT_NE(serviceCallback, nullptr);

    EXPECT_CALL(*mockService, OpenDevice(7001, _)).WillOnce(Invoke([](int64_t, MidiDeviceInfo &info) {
        info.deviceId = 7001;
        info.deviceType = DeviceType::DEVICE_TYPE_USB;
        info.transportProtocol = TransportProtocol::PROTOCOL_1_0;
        info.deviceName = "Callback Device";
        return OH_MIDI_STATUS_OK;
    }));
    MidiDevice *rawDevice = nullptr;
    ASSERT_EQ(client->OpenDevice(7001, &rawDevice), OH_MIDI_STATUS_OK);
    ASSERT_NE(rawDevice, nullptr);

    MidiDeviceInfo changedInfo;
    changedInfo.deviceId = 7001;
    changedInfo.deviceType = DeviceType::DEVICE_TYPE_USB;
    changedInfo.transportProtocol = TransportProtocol::PROTOCOL_1_0;
    changedInfo.address.assign(sizeof(OH_MIDIDeviceInformation::deviceAddress) + 8, 'a');
    changedInfo.deviceName.assign(sizeof(OH_MIDIDeviceInformation::deviceName) + 8, 'n');
    EXPECT_EQ(serviceCallback->NotifyDeviceChange(
        OH_MIDI_DEVICE_CHANGE_ACTION_CONNECTED, changedInfo), OH_MIDI_STATUS_OK);
    EXPECT_EQ(capture.deviceChangeCount, 1);
    EXPECT_STREQ(capture.lastDeviceInfo.deviceAddress, "");
    EXPECT_STREQ(capture.lastDeviceInfo.deviceName, "");

    EXPECT_EQ(serviceCallback->NotifyDeviceChange(
        OH_MIDI_DEVICE_CHANGE_ACTION_DISCONNECTED, changedInfo), OH_MIDI_STATUS_OK);
    EXPECT_EQ(capture.deviceChangeCount, 2);
    EXPECT_EQ(static_cast<MidiDevicePrivate *>(rawDevice)->CloseDevice(), OH_MIDI_STATUS_OK);

    EXPECT_EQ(serviceCallback->NotifyError(OH_MIDI_STATUS_SYSTEM_ERROR), OH_MIDI_STATUS_OK);
    EXPECT_EQ(capture.errorCount, 1);
    EXPECT_EQ(capture.lastError, OH_MIDI_STATUS_SYSTEM_ERROR);
}

/**
 * @tc.name: BleOpenAndDestroyedClientBranches001
 * @tc.desc: Verify BLE failure/success and destroyed-client callback guards.
 * @tc.type: FUNC
 */
HWTEST_F(MidiClientUnitTest, BleOpenAndDestroyedClientBranches001, TestSize.Level0)
{
    ApiCallbackCapture capture;
    OH_MIDICallbacks callbacks {DeviceChangeTrampoline, ErrorTrampoline};
    sptr<MidiCallbackStub> serviceCallback;
    EXPECT_CALL(*mockService, Init(_, _))
        .WillOnce(DoAll(SaveArg<0>(&serviceCallback), SetArgReferee<1>(77), Return(OH_MIDI_STATUS_OK)));
    ASSERT_EQ(client->Init(callbacks, &capture), OH_MIDI_STATUS_OK);
    ASSERT_NE(serviceCallback, nullptr);

    EXPECT_CALL(*mockService, OpenDevice(7001, _)).WillOnce(Invoke([](int64_t, MidiDeviceInfo &info) {
        info.deviceId = 7001;
        info.deviceType = DeviceType::DEVICE_TYPE_USB;
        return OH_MIDI_STATUS_OK;
    }));
    MidiDevice *rawDevice = nullptr;
    ASSERT_EQ(client->OpenDevice(7001, &rawDevice), OH_MIDI_STATUS_OK);

    sptr<MidiDeviceOpenCallbackStub> bleCallback;
    EXPECT_CALL(*mockService, OpenBleDevice(_, _))
        .WillOnce(DoAll(SaveArg<1>(&bleCallback), Return(OH_MIDI_STATUS_OK)));
    ASSERT_EQ(client->OpenBleDevice("11:22:33:44:55:66", DeviceOpenedTrampoline, &capture), OH_MIDI_STATUS_OK);
    ASSERT_NE(bleCallback, nullptr);

    MidiDeviceInfo bleInfo;
    bleInfo.deviceId = 7002;
    bleInfo.deviceType = DeviceType::DEVICE_TYPE_BLE;
    bleInfo.transportProtocol = TransportProtocol::PROTOCOL_2_0;
    bleInfo.deviceName = "BLE Device";
    EXPECT_EQ(bleCallback->NotifyDeviceOpened(false, bleInfo), OH_MIDI_STATUS_OK);
    EXPECT_EQ(capture.deviceOpenCount, 1);
    EXPECT_FALSE(capture.lastOpened);
    EXPECT_EQ(capture.lastDevice, nullptr);

    EXPECT_EQ(bleCallback->NotifyDeviceOpened(true, bleInfo), OH_MIDI_STATUS_OK);
    EXPECT_EQ(capture.deviceOpenCount, 2);
    EXPECT_TRUE(capture.lastOpened);
    EXPECT_NE(capture.lastDevice, nullptr);

    MidiDeviceInfo changedInfo;
    changedInfo.deviceId = 7001;
    EXPECT_CALL(*mockService, DestroyMidiClient()).WillOnce(Return(OH_MIDI_STATUS_OK));
    ASSERT_EQ(client->DestroyMidiClient(), OH_MIDI_STATUS_OK);
    EXPECT_EQ(serviceCallback->NotifyDeviceChange(
        OH_MIDI_DEVICE_CHANGE_ACTION_CONNECTED, changedInfo), OH_MIDI_STATUS_OK);
    EXPECT_EQ(serviceCallback->NotifyError(OH_MIDI_STATUS_SYSTEM_ERROR), OH_MIDI_STATUS_OK);
    EXPECT_EQ(bleCallback->NotifyDeviceOpened(true, bleInfo), OH_MIDI_STATUS_OK);
    // Destroyed-client callback guards: every post-destroy notification is dropped,
    // so the counters must stay at their pre-destroy values (0/0/2).
    EXPECT_EQ(capture.deviceChangeCount, 0);
    EXPECT_EQ(capture.errorCount, 0);
    EXPECT_EQ(capture.deviceOpenCount, 2);

    EXPECT_CALL(*mockService, CloseDevice(7001)).WillOnce(Return(OH_MIDI_STATUS_OK));
    EXPECT_EQ(client->CloseAndRemoveDevice(rawDevice), OH_MIDI_STATUS_OK);
}

/**
 * @tc.name: CallbackValidation001
 * @tc.desc: Verify missing callbacks and expired IPC weak references return safe errors.
 * @tc.type: FUNC
 */
HWTEST_F(MidiClientUnitTest, CallbackValidation001, TestSize.Level0)
{
    OH_MIDICallbacks callbacks {};
    sptr<MidiCallbackStub> serviceCallback;
    EXPECT_CALL(*mockService, Init(_, _))
        .WillOnce(DoAll(SaveArg<0>(&serviceCallback), Return(OH_MIDI_STATUS_OK)));
    ASSERT_EQ(client->Init(callbacks, nullptr), OH_MIDI_STATUS_OK);
    ASSERT_NE(serviceCallback, nullptr);

    MidiDeviceInfo info;
    info.deviceId = 7100;
    EXPECT_EQ(serviceCallback->NotifyDeviceChange(
        OH_MIDI_DEVICE_CHANGE_ACTION_CONNECTED, info), OH_MIDI_STATUS_SYSTEM_ERROR);
    EXPECT_EQ(serviceCallback->NotifyError(OH_MIDI_STATUS_SYSTEM_ERROR), OH_MIDI_STATUS_SYSTEM_ERROR);

    auto localService = std::make_shared<MidiServiceMock>();
    auto localClient = std::make_shared<MidiClientPrivate>();
    localClient->ipc_ = localService;
    localClient->selfRef_ = localClient;
    MidiClientDeviceOpenCallback nullCallback(localService, nullptr, nullptr, localClient);
    EXPECT_EQ(nullCallback.NotifyDeviceOpened(true, info), OH_MIDI_STATUS_SYSTEM_ERROR);

    ApiCallbackCapture capture;
    MidiClientDeviceOpenCallback expiredIpcCallback(
        localService, DeviceOpenedTrampoline, &capture, localClient);
    localClient->ipc_.reset();
    localService.reset();
    EXPECT_EQ(expiredIpcCallback.NotifyDeviceOpened(true, info), OH_MIDI_STATUS_SYSTEM_ERROR);
    localClient->selfRef_.reset();
}

/**
 * @tc.name: OutputPortOperations001
 * @tc.desc: Verify output open/send/flush/close success, duplicate, and failure branches.
 * @tc.type: FUNC
 */
HWTEST_F(MidiClientUnitTest, OutputPortOperations001, TestSize.Level0)
{
    OH_MIDIDeviceInformation info {};
    info.midiDeviceId = 7200;
    info.nativeProtocol = OH_MIDI_PROTOCOL_1_0;
    auto device = std::make_unique<MidiDevicePrivate>(mockService, info);
    OH_MIDIPortDescriptor descriptor {};
    descriptor.portIndex = 3;
    descriptor.protocol = OH_MIDI_PROTOCOL_1_0;

    EXPECT_CALL(*mockService, OpenOutputPort(_, info.midiDeviceId, descriptor.portIndex))
        .WillOnce(Invoke([](std::shared_ptr<MidiSharedRing> &buffer, int64_t, uint32_t) {
            buffer = MidiSharedRing::CreateFromLocal(512);
            return OH_MIDI_STATUS_OK;
        }));
    ASSERT_EQ(device->OpenOutputPort(descriptor), OH_MIDI_STATUS_OK);
    EXPECT_EQ(device->OpenOutputPort(descriptor), OH_MIDI_STATUS_PORT_ALREADY_OPEN);

    uint32_t word = 0x20903C40;
    OH_MIDIEvent event {1, 1, &word};
    uint32_t written = 0;
    EXPECT_EQ(device->Send(descriptor.portIndex, &event, 1, &written), OH_MIDI_STATUS_OK);
    EXPECT_EQ(written, 1);
    EXPECT_EQ(device->Send(descriptor.portIndex + 1, &event, 1, &written), OH_MIDI_STATUS_INVALID_PORT);
    EXPECT_EQ(device->Send(descriptor.portIndex, nullptr, 1, &written),
        OH_MIDI_STATUS_GENERIC_INVALID_ARGUMENT);

    uint8_t sysex[] = {0xF0, 0x01, 0xF7};
    EXPECT_EQ(device->SendSysEx(descriptor.portIndex, sysex, sizeof(sysex)), OH_MIDI_STATUS_OK);
    EXPECT_EQ(device->SendSysEx(descriptor.portIndex + 1, sysex, sizeof(sysex)), OH_MIDI_STATUS_INVALID_PORT);

    EXPECT_CALL(*mockService, FlushOutputPort(info.midiDeviceId, descriptor.portIndex))
        .WillOnce(Return(OH_MIDI_STATUS_OK))
        .WillOnce(Return(OH_MIDI_STATUS_SYSTEM_ERROR));
    EXPECT_EQ(device->FlushOutputPort(descriptor.portIndex), OH_MIDI_STATUS_OK);
    EXPECT_EQ(device->FlushOutputPort(descriptor.portIndex), OH_MIDI_STATUS_SYSTEM_ERROR);
    EXPECT_EQ(device->FlushOutputPort(descriptor.portIndex + 1), OH_MIDI_STATUS_INVALID_PORT);

    EXPECT_CALL(*mockService, CloseOutputPort(info.midiDeviceId, descriptor.portIndex))
        .WillOnce(Return(OH_MIDI_STATUS_SYSTEM_ERROR));
    EXPECT_EQ(device->CloseOutputPort(descriptor.portIndex), OH_MIDI_STATUS_SYSTEM_ERROR);
    EXPECT_EQ(device->CloseOutputPort(descriptor.portIndex), OH_MIDI_STATUS_INVALID_PORT);

    EXPECT_CALL(*mockService, OpenOutputPort(_, info.midiDeviceId, 4))
        .WillOnce(Return(OH_MIDI_STATUS_SYSTEM_ERROR));
    descriptor.portIndex = 4;
    EXPECT_EQ(device->OpenOutputPort(descriptor), OH_MIDI_STATUS_SYSTEM_ERROR);
}

/**
 * @tc.name: ExpiredIpcAndInvalidClientOperations001
 * @tc.desc: Verify null IPC, bad handles, empty fills, and close failure branches.
 * @tc.type: FUNC
 */
HWTEST_F(MidiClientUnitTest, ExpiredIpcAndInvalidClientOperations001, TestSize.Level0)
{
    client->ipc_.reset();
    OH_MIDICallbacks callbacks {};
    size_t count = 0;
    MidiDevice *device = nullptr;
    EXPECT_EQ(client->Init(callbacks, nullptr), OH_MIDI_STATUS_SYSTEM_ERROR);
    EXPECT_EQ(client->GetDevices(nullptr, &count), OH_MIDI_STATUS_SYSTEM_ERROR);
    EXPECT_EQ(client->OpenDevice(1, &device), OH_MIDI_STATUS_SYSTEM_ERROR);
    EXPECT_EQ(client->OpenDevice(1, nullptr), OH_MIDI_STATUS_SYSTEM_ERROR);
    EXPECT_EQ(client->OpenBleDevice("x", DeviceOpenedTrampoline, nullptr), OH_MIDI_STATUS_SYSTEM_ERROR);
    EXPECT_EQ(client->GetDevicePorts(1, nullptr, &count), OH_MIDI_STATUS_SYSTEM_ERROR);
    EXPECT_EQ(client->DestroyMidiClient(), OH_MIDI_STATUS_SYSTEM_ERROR);
    EXPECT_EQ(client->CloseAndRemoveDevice(nullptr), OH_MIDI_STATUS_INVALID_DEVICE_HANDLE);

    auto localService = std::make_shared<MidiServiceMock>();
    auto localClient = std::make_unique<MidiClientPrivate>();
    localClient->ipc_ = localService;
    EXPECT_CALL(*localService, GetDevices(_))
        .WillOnce(Return(OH_MIDI_STATUS_SYSTEM_ERROR))
        .WillOnce(Return(OH_MIDI_STATUS_OK));
    OH_MIDIDeviceInformation deviceInfo {};
    count = 1;
    EXPECT_EQ(localClient->GetDevices(&deviceInfo, &count), OH_MIDI_STATUS_SYSTEM_ERROR);
    count = 1;
    EXPECT_EQ(localClient->GetDevices(&deviceInfo, &count), OH_MIDI_STATUS_OK);
    EXPECT_EQ(count, 0);

    EXPECT_CALL(*localService, GetDevicePorts(_, _))
        .WillOnce(Return(OH_MIDI_STATUS_SYSTEM_ERROR))
        .WillOnce(Return(OH_MIDI_STATUS_OK));
    OH_MIDIPortInformation portInfo {};
    count = 1;
    EXPECT_EQ(localClient->GetDevicePorts(1, &portInfo, &count), OH_MIDI_STATUS_SYSTEM_ERROR);
    count = 1;
    EXPECT_EQ(localClient->GetDevicePorts(1, &portInfo, &count), OH_MIDI_STATUS_OK);
    EXPECT_EQ(count, 0);

    EXPECT_CALL(*localService, OpenDevice(_, _)).WillOnce(Return(OH_MIDI_STATUS_SYSTEM_ERROR));
    EXPECT_EQ(localClient->OpenDevice(1, &device), OH_MIDI_STATUS_SYSTEM_ERROR);

    OH_MIDIDeviceInformation rawInfo {};
    rawInfo.midiDeviceId = 7300;
    auto orphan = std::make_unique<MidiDevicePrivate>(localService, rawInfo);
    EXPECT_EQ(localClient->CloseAndRemoveDevice(orphan.get()), OH_MIDI_STATUS_INVALID_DEVICE_HANDLE);
}

/**
 * @tc.name: DevicePortCloseAndExpiredIpc001
 * @tc.desc: Verify close IPC errors and weak-reference expiry for all device operations.
 * @tc.type: FUNC
 */
HWTEST_F(MidiClientUnitTest, DevicePortCloseAndExpiredIpc001, TestSize.Level0)
{
    OH_MIDIDeviceInformation info {};
    info.midiDeviceId = 7400;
    info.nativeProtocol = OH_MIDI_PROTOCOL_1_0;
    auto device = std::make_unique<MidiDevicePrivate>(mockService, info);
    OH_MIDIPortDescriptor descriptor {};
    descriptor.portIndex = 0;
    descriptor.protocol = OH_MIDI_PROTOCOL_1_0;
    CallbackCapture capture;

    EXPECT_CALL(*mockService, OpenInputPort(_, info.midiDeviceId, descriptor.portIndex))
        .WillOnce(Invoke([](std::shared_ptr<MidiSharedRing> &buffer, int64_t, uint32_t) {
            buffer = MidiSharedRing::CreateFromLocal(256);
            return OH_MIDI_STATUS_OK;
        }));
    ASSERT_EQ(device->OpenInputPort(descriptor, MidiReceivedTrampoline, &capture), OH_MIDI_STATUS_OK);
    EXPECT_CALL(*mockService, CloseInputPort(info.midiDeviceId, descriptor.portIndex))
        .WillOnce(Return(OH_MIDI_STATUS_SYSTEM_ERROR));
    EXPECT_EQ(device->CloseInputPort(descriptor.portIndex), OH_MIDI_STATUS_SYSTEM_ERROR);

    auto expiringService = std::make_shared<MidiServiceMock>();
    auto expiredDevice = std::make_unique<MidiDevicePrivate>(expiringService, info);
    expiredDevice->inputPortsMap_[1] = std::make_shared<MidiInputPort>(
        MidiReceivedTrampoline, &capture, MIDI_NONE);
    expiredDevice->outputPortsMap_[1] = std::make_shared<MidiOutputPort>(MIDI_NONE);
    expiringService.reset();

    std::shared_ptr<MidiSharedRing> unused;
    descriptor.portIndex = 2;
    EXPECT_EQ(expiredDevice->CloseDevice(), OH_MIDI_STATUS_SYSTEM_ERROR);
    EXPECT_EQ(expiredDevice->OpenInputPort(descriptor, MidiReceivedTrampoline, &capture),
        OH_MIDI_STATUS_SYSTEM_ERROR);
    EXPECT_EQ(expiredDevice->OpenOutputPort(descriptor), OH_MIDI_STATUS_SYSTEM_ERROR);
    EXPECT_EQ(expiredDevice->CloseInputPort(1), OH_MIDI_STATUS_SYSTEM_ERROR);
    EXPECT_EQ(expiredDevice->CloseOutputPort(1), OH_MIDI_STATUS_SYSTEM_ERROR);
    EXPECT_EQ(expiredDevice->FlushOutputPort(1), OH_MIDI_STATUS_SYSTEM_ERROR);
}

/**
 * @tc.name: InputAndOutputInternalBranches001
 * @tc.desc: Verify input wake guards, receiver early exits, and output validation/status mapping.
 * @tc.type: FUNC
 */
HWTEST_F(MidiClientUnitTest, InputAndOutputInternalBranches001, TestSize.Level0)
{
    CallbackCapture capture;
    MidiInputPort inputPort(MidiReceivedTrampoline, &capture, MIDI_NONE);
    EXPECT_TRUE(inputPort.ShouldWakeForReadOrExit());
    inputPort.running_.store(true);
    EXPECT_TRUE(inputPort.ShouldWakeForReadOrExit());
    inputPort.ReceiverThreadLoop();
    EXPECT_FALSE(inputPort.running_.load());

    inputPort.ringBuffer_ = std::make_shared<MidiSharedRing>(0);
    inputPort.running_.store(true);
    inputPort.ReceiverThreadLoop();
    EXPECT_FALSE(inputPort.running_.load());
    inputPort.DrainRingAndDispatch();

    MidiOutputPort outputPort(MIDI_NONE);
    uint32_t word = 0x20903C40;
    OH_MIDIEvent event {1, 1, &word};
    uint32_t written = 0;
    EXPECT_EQ(outputPort.Send(nullptr, 1, &written), OH_MIDI_STATUS_GENERIC_INVALID_ARGUMENT);
    EXPECT_EQ(outputPort.Send(&event, 1, nullptr), OH_MIDI_STATUS_GENERIC_INVALID_ARGUMENT);
    outputPort.ringBuffer_ = MidiSharedRing::CreateFromLocal(256);
    ASSERT_NE(outputPort.ringBuffer_, nullptr);
    EXPECT_EQ(outputPort.Send(&event, 0, &written), OH_MIDI_STATUS_GENERIC_INVALID_ARGUMENT);
    EXPECT_EQ(outputPort.Send(&event, 1001, &written), OH_MIDI_STATUS_GENERIC_INVALID_ARGUMENT);

    uint8_t data[] = {0xF0};
    MidiOutputPort noRing(MIDI_NONE);
    EXPECT_EQ(noRing.SendSysEx(0, nullptr, 1), OH_MIDI_STATUS_GENERIC_INVALID_ARGUMENT);
    EXPECT_EQ(noRing.SendSysEx(0, data, 0), OH_MIDI_STATUS_GENERIC_INVALID_ARGUMENT);
    EXPECT_EQ(noRing.SendSysEx(0, data, 1), OH_MIDI_STATUS_GENERIC_INVALID_ARGUMENT);
    noRing.ringBuffer_ = MidiSharedRing::CreateFromLocal(256);
    EXPECT_EQ(noRing.SendSysEx(16, data, 1), OH_MIDI_STATUS_INVALID_PORT);

    MidiOutputPort brokenRing(MIDI_NONE);
    brokenRing.ringBuffer_ = std::make_shared<MidiSharedRing>(0);
    EXPECT_EQ(brokenRing.SendSysEx(0, data, 1), OH_MIDI_STATUS_SYSTEM_ERROR);
}

/**
 * @tc.name: ExpiredClientCallbackBranches001
 * @tc.desc: Verify both callback adapters ignore notifications after their client weak reference expires.
 * @tc.type: FUNC
 */
HWTEST_F(MidiClientUnitTest, ExpiredClientCallbackBranches001, TestSize.Level0)
{
    ApiCallbackCapture capture;
    MidiDeviceInfo info;
    info.deviceId = 7600;

    auto callbackService = std::make_shared<MidiServiceMock>();
    auto callbackClient = std::make_shared<MidiClientPrivate>();
    MidiClientDeviceOpenCallback openCallback(
        callbackService, DeviceOpenedTrampoline, &capture, callbackClient);
    callbackClient.reset();
    EXPECT_EQ(openCallback.NotifyDeviceOpened(true, info), OH_MIDI_STATUS_OK);
    EXPECT_EQ(capture.deviceOpenCount, 0);

    auto listenerService = std::make_shared<MidiServiceMock>();
    auto listenerClient = std::make_unique<MidiClientPrivate>();
    listenerClient->ipc_ = listenerService;
    sptr<MidiCallbackStub> listener;
    EXPECT_CALL(*listenerService, Init(_, _))
        .WillOnce(DoAll(SaveArg<0>(&listener), Return(OH_MIDI_STATUS_OK)));
    OH_MIDICallbacks callbacks {DeviceChangeTrampoline, ErrorTrampoline};
    ASSERT_EQ(listenerClient->Init(callbacks, &capture), OH_MIDI_STATUS_OK);
    ASSERT_NE(listener, nullptr);
    listenerClient.reset();
    EXPECT_EQ(listener->NotifyDeviceChange(OH_MIDI_DEVICE_CHANGE_ACTION_CONNECTED, info), OH_MIDI_STATUS_OK);
    EXPECT_EQ(listener->NotifyError(OH_MIDI_STATUS_SYSTEM_ERROR), OH_MIDI_STATUS_OK);
    EXPECT_EQ(capture.deviceChangeCount, 0);
    EXPECT_EQ(capture.errorCount, 0);
}

/**
 * @tc.name: InputConversionMatrix001
 * @tc.desc: Verify both conversion directions, conversion fallback, and unsupported direction dispatch.
 * @tc.type: FUNC
 */
HWTEST_F(MidiClientUnitTest, InputConversionMatrix001, TestSize.Level0)
{
    CallbackCapture capture;
    const std::vector<std::pair<ProtocolDirection, std::vector<uint32_t>>> cases = {
        {MIDI_1_0_TO_MIDI_2_0, {0x20903C64}},
        {MIDI_2_0_TO_MIDI_1_0, {0x40903C00, 0xC8000000}},
        {MIDI_1_0_TO_MIDI_2_0, {0x50000000, 0, 0, 0}},
        {static_cast<ProtocolDirection>(99), {0x20903C64}},
    };

    uint64_t timestamp = 1;
    for (const auto &[direction, words] : cases) {
        MidiInputPort inputPort(MidiReceivedTrampoline, &capture, direction);
        inputPort.ringBuffer_ = MidiSharedRing::CreateFromLocal(512);
        ASSERT_NE(inputPort.ringBuffer_, nullptr);
        MidiEventInner event = MakeMidiEventInner(timestamp++, words);
        ASSERT_EQ(inputPort.ringBuffer_->TryWriteEvent(event, false), MidiStatusCode::OK);
        inputPort.DrainRingAndDispatch();
    }
    EXPECT_EQ(capture.GetReceivedCount(), cases.size());

    MidiInputPort emptyPackets(MidiReceivedTrampoline, &capture, MIDI_NONE);
    emptyPackets.ringBuffer_ = MidiSharedRing::CreateFromLocal(256);
    std::vector<uint32_t> incomplete{0x40000000};
    MidiEventInner incompleteEvent = MakeMidiEventInner(timestamp, incomplete);
    ASSERT_EQ(emptyPackets.ringBuffer_->TryWriteEvent(incompleteEvent, false), MidiStatusCode::OK);
    emptyPackets.DrainRingAndDispatch();
    EXPECT_EQ(capture.GetReceivedCount(), cases.size());
}

/**
 * @tc.name: OutputEdgeBranches001
 * @tc.desc: Verify output conversion, full-ring status, timeout, and packet preparation.
 * @tc.type: FUNC
 */
HWTEST_F(MidiClientUnitTest, OutputEdgeBranches001, TestSize.Level0)
{
    MidiOutputPort convertingOutput(MIDI_2_0_TO_MIDI_1_0);
    convertingOutput.ringBuffer_ = MidiSharedRing::CreateFromLocal(256);
    ASSERT_NE(convertingOutput.ringBuffer_, nullptr);
    uint32_t midi2Words[] = {0x40903C00, 0xC8000000};
    OH_MIDIEvent midi2Event {1, 2, midi2Words};
    uint32_t written = 0;
    EXPECT_EQ(convertingOutput.Send(&midi2Event, 1, &written), OH_MIDI_STATUS_OK);
    EXPECT_EQ(written, 1);

    uint32_t unsupportedWords[] = {0x50000000, 0, 0, 0};
    OH_MIDIEvent unsupportedEvent {2, 4, unsupportedWords};
    EXPECT_EQ(convertingOutput.Send(&unsupportedEvent, 1, &written), OH_MIDI_STATUS_OK);

    std::vector<uint32_t> largePayload(40, 0x20903C40);
    OH_MIDIEvent largeEvent {3, largePayload.size(), largePayload.data()};
    int32_t sendResult = OH_MIDI_STATUS_OK;
    do {
        sendResult = convertingOutput.Send(&largeEvent, 1, &written);
    } while (sendResult == OH_MIDI_STATUS_OK);
    EXPECT_EQ(sendResult, OH_MIDI_STATUS_WOULD_BLOCK);

    std::vector<MidiEventInner> packets(1);
    EXPECT_EQ(convertingOutput.SendSysExPackets(
        packets, 1, std::chrono::steady_clock::now() - std::chrono::seconds(2)), OH_MIDI_STATUS_TIMEOUT);
    SysExPacketData packetData;
    uint8_t sysex[7] = {0xF0, 1, 2, 3, 4, 5, 0xF7};
    convertingOutput.PrepareSysExPackets(0, sysex, sizeof(sysex), 2, packetData);
    EXPECT_EQ(packetData.innerEvents.size(), 2);
    convertingOutput.PrepareSysExPackets(0, sysex, 0, 0, packetData);
    EXPECT_TRUE(packetData.innerEvents.empty());
}

/**
 * @tc.name: DeviceEdgeBranches001
 * @tc.desc: Verify invalid-device sends, null port maps, and device lookup loops.
 * @tc.type: FUNC
 */
HWTEST_F(MidiClientUnitTest, DeviceEdgeBranches001, TestSize.Level0)
{
    OH_MIDIDeviceInformation info {};
    info.midiDeviceId = 7700;
    auto device = std::make_shared<MidiDevicePrivate>(mockService, info);
    device->outputPortsMap_[1] = std::make_shared<MidiOutputPort>(MIDI_NONE);
    device->outputPortsMap_[1]->ringBuffer_ = MidiSharedRing::CreateFromLocal(256);
    device->inputPortsMap_[1] = nullptr;
    device->inputPortsMap_[2] = std::make_shared<MidiInputPort>(MidiReceivedTrampoline, nullptr, MIDI_NONE);
    device->outputPortsMap_[2] = nullptr;
    device->SetInValid();
    uint32_t word = 0x20903C40;
    OH_MIDIEvent event {1, 1, &word};
    uint32_t written = 0;
    EXPECT_EQ(device->Send(1, &event, 1, &written), OH_MIDI_STATUS_GENERIC_IPC_FAILURE);
    uint8_t data = 0xF0;
    EXPECT_EQ(device->SendSysEx(1, &data, 1), OH_MIDI_STATUS_GENERIC_IPC_FAILURE);
    device->CloseAllPorts();

    device->inputPortsMap_[1] = nullptr;
    device->inputPortsMap_[2] = std::make_shared<MidiInputPort>(MidiReceivedTrampoline, nullptr, MIDI_NONE);
    device->outputPortsMap_[1] = nullptr;
    device->outputPortsMap_[2] = std::make_shared<MidiOutputPort>(MIDI_NONE);
    device->TombstoneAllPorts();

    client->deviceHandlers_.push_back(nullptr);
    client->deviceHandlers_.push_back(device);
    EXPECT_TRUE(client->IsDeviceOpened(info.midiDeviceId));
    EXPECT_FALSE(client->IsDeviceOpened(info.midiDeviceId + 1));
    client->MarkDeviceInValid();
}

/**
 * @tc.name: LongPortNameAndCloseFailure001
 * @tc.desc: Verify port-name copy fallback and CloseDevice IPC error propagation.
 * @tc.type: FUNC
 */
HWTEST_F(MidiClientUnitTest, LongPortNameAndCloseFailure001, TestSize.Level0)
{
    constexpr int64_t deviceId = 7800;
    EXPECT_CALL(*mockService, GetDevicePorts(deviceId, _))
        .WillOnce(Invoke([](int64_t, std::vector<MidiPortInfo> &ports) {
            MidiPortInfo port;
            port.portId = 1;
            port.name.assign(sizeof(OH_MIDIPortInformation::name) + 8, 'x');
            ports.push_back(port);
            return OH_MIDI_STATUS_OK;
        }));
    OH_MIDIPortInformation info {};
    size_t count = 1;
    EXPECT_EQ(client->GetDevicePorts(deviceId, &info, &count), OH_MIDI_STATUS_OK);
    EXPECT_STREQ(info.name, "");

    OH_MIDIDeviceInformation deviceInfo {};
    deviceInfo.midiDeviceId = deviceId;
    MidiDevicePrivate device(mockService, deviceInfo);
    EXPECT_CALL(*mockService, CloseDevice(deviceId)).WillOnce(Return(OH_MIDI_STATUS_SYSTEM_ERROR));
    EXPECT_EQ(device.CloseDevice(), OH_MIDI_STATUS_SYSTEM_ERROR);
}
