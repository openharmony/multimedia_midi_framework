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

}  // namespace

class MidiServiceMock : public MidiServiceInterface {
public:
    MOCK_METHOD(OH_MIDIStatusCode, Init, (sptr<MidiCallbackStub> callback, uint32_t &clientId), (override));
    MOCK_METHOD(OH_MIDIStatusCode, GetDevices, ((std::vector<std::map<int32_t, std::string>>)&deviceInfos), (override));
    MOCK_METHOD(OH_MIDIStatusCode, OpenDevice, (int64_t deviceId), (override));
    MOCK_METHOD(OH_MIDIStatusCode, OpenBleDevice,
        (std::string address, sptr<MidiDeviceOpenCallbackStub> callback), (override));
    MOCK_METHOD(OH_MIDIStatusCode, CloseDevice, (int64_t deviceId), (override));
    MOCK_METHOD(OH_MIDIStatusCode, GetDevicePorts,
        (int64_t deviceId, (std::vector<std::map<int32_t, std::string>>)&portInfos), (override));
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
 * @tc.name: OpenDevice_001
 * @tc.desc: Test opening device.
 * @tc.type: FUNC
 */
HWTEST_F(MidiClientUnitTest, OpenDevice_001, TestSize.Level0)
{
    int64_t deviceId = 100;
    MidiDevice *device = nullptr;

    // Expect OpenDevice to be called twice on the IPC layer
    EXPECT_CALL(*mockService, OpenDevice(deviceId)).Times(1).WillRepeatedly(Return(MIDI_STATUS_OK));

    EXPECT_EQ(client->OpenDevice(deviceId, &device), MIDI_STATUS_OK);
    EXPECT_NE(device, nullptr);
    delete device;
}

/**
 * @tc.name: GetDevice_001
 * @tc.desc: Test getting device list with multiple devices.
 * @tc.type: FUNC
 */
HWTEST_F(MidiClientUnitTest, GetDevices_001, TestSize.Level0)
{
    // 1. Prepare mock data from IPC
    EXPECT_CALL(*mockService, Init(_, _)).Times(1).WillOnce(Return(MIDI_STATUS_OK));
    EXPECT_CALL(*mockService, GetDevices(_)).WillOnce(Invoke([](std::vector<std::map<int32_t, std::string>> &infos) {
        infos.push_back({{DEVICE_ID, "1001"},
            {DEVICE_TYPE, "0"},
            {MIDI_PROTOCOL, "1"},
            {DEVICE_NAME, "Mock_Piano"},
            {PRODUCT_ID, "1234"},
            {VENDOR_ID, "MockVendor_1"},
            {ADDRESS, ""}});
        infos.push_back({{DEVICE_ID, "1002"},
            {DEVICE_TYPE, "1"},
            {MIDI_PROTOCOL, "1"},
            {DEVICE_NAME, "Mock_Drum"},
            {PRODUCT_ID, "5678"},
            {VENDOR_ID, "MockVendor_2"},
            {ADDRESS, "aabbcc"}});
        return MIDI_STATUS_OK;
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
    EXPECT_EQ(status, MIDI_STATUS_OK);
    EXPECT_EQ(numDevices, 2);
    EXPECT_EQ(infoArray[0].midiDeviceId, 1001);
    EXPECT_EQ(infoArray[0].deviceType, MIDI_DEVICE_TYPE_USB);
    EXPECT_EQ(infoArray[0].nativeProtocol, MIDI_PROTOCOL_1_0);
    EXPECT_STREQ(infoArray[0].deviceName, "Mock_Piano");
    EXPECT_EQ(infoArray[0].vendorId, 0);
    EXPECT_EQ(infoArray[0].productId, 0);
    EXPECT_STREQ(infoArray[0].deviceAddress, "");
    EXPECT_EQ(infoArray[1].midiDeviceId, 1002);
    EXPECT_EQ(infoArray[1].deviceType, MIDI_DEVICE_TYPE_BLE);
    EXPECT_EQ(infoArray[1].nativeProtocol, MIDI_PROTOCOL_1_0);
    EXPECT_STREQ(infoArray[1].deviceName, "Mock_Drum");
    EXPECT_EQ(infoArray[1].vendorId, 0);
    EXPECT_EQ(infoArray[1].productId, 0);
    EXPECT_STREQ(infoArray[1].deviceAddress, "aabbcc");
}

/**
 * @tc.name: GetDevices_002
 * @tc.desc: Test GetDevices when the provided buffer is too small.
 * @tc.type: FUNC
 */
HWTEST_F(MidiClientUnitTest, GetDevices_002, TestSize.Level0)
{
    EXPECT_CALL(*mockService, Init(_, _)).Times(1).WillOnce(Return(MIDI_STATUS_OK));
    EXPECT_CALL(*mockService, GetDevices(_)).WillOnce(Invoke([](std::vector<std::map<int32_t, std::string>> &infos) {
        infos.push_back({{DEVICE_ID, "1001"},
            {DEVICE_TYPE, "0"},
            {MIDI_PROTOCOL, "1"},
            {DEVICE_NAME, "Mock_Piano"},
            {PRODUCT_ID, "1234"},
            {VENDOR_ID, "MockVendor_1"},
            {ADDRESS, ""}});
        infos.push_back({{DEVICE_ID, "1002"},
            {DEVICE_TYPE, "1"},
            {MIDI_PROTOCOL, "1"},
            {DEVICE_NAME, "Mock_Drum"},
            {PRODUCT_ID, "5678"},
            {VENDOR_ID, "MockVendor_2"},
            {ADDRESS, "aabbcc"}});
        return MIDI_STATUS_OK;
    }));
    OH_MIDICallbacks callbacks;
    callbacks.onDeviceChange =
        [](void *userData, OH_MIDIDeviceChangeAction action, OH_MIDIDeviceInformation deviceInfo) {};
    callbacks.onError = [](void *userData, OH_MIDIStatusCode code) {
    };
    void *userData = nullptr;
    client->Init(callbacks, userData);
    OH_MIDIDeviceInformation infoArrayTest[1];  // Only 1 slot
    size_t numDevices = 1;
    OH_MIDIStatusCode status = client->GetDevices(infoArrayTest, &numDevices);

    // Should return insufficient space and update numDevices to required size
    EXPECT_EQ(status, MIDI_STATUS_INSUFFICIENT_RESULT_SPACE);
    EXPECT_EQ(numDevices, 2);
    EXPECT_CALL(*mockService, GetDevices(_)).WillOnce(Invoke([](std::vector<std::map<int32_t, std::string>> &infos) {
        infos.push_back({{DEVICE_ID, "1001"},
            {DEVICE_TYPE, "0"},
            {MIDI_PROTOCOL, "1"},
            {DEVICE_NAME, "Mock_Piano"},
            {PRODUCT_ID, "1234"},
            {VENDOR_ID, "MockVendor_1"},
            {ADDRESS, ""}});
        infos.push_back({{DEVICE_ID, "1002"},
            {DEVICE_TYPE, "1"},
            {MIDI_PROTOCOL, "1"},
            {DEVICE_NAME, "Mock_Drum"},
            {PRODUCT_ID, "5678"},
            {VENDOR_ID, "MockVendor_2"},
            {ADDRESS, "aabbcc"}});
        return MIDI_STATUS_OK;
    }));
    OH_MIDIDeviceInformation infoArray[2];
    status = client->GetDevices(infoArray, &numDevices);

    // 3. Verify
    EXPECT_EQ(status, MIDI_STATUS_OK);
    EXPECT_EQ(numDevices, 2);
    EXPECT_EQ(infoArray[0].midiDeviceId, 1001);
    EXPECT_EQ(infoArray[0].deviceType, MIDI_DEVICE_TYPE_USB);
    EXPECT_EQ(infoArray[0].nativeProtocol, MIDI_PROTOCOL_1_0);
    EXPECT_STREQ(infoArray[0].deviceName, "Mock_Piano");
    EXPECT_EQ(infoArray[0].vendorId, 0);
    EXPECT_EQ(infoArray[0].productId, 0);
    EXPECT_STREQ(infoArray[0].deviceAddress, "");
    EXPECT_EQ(infoArray[1].midiDeviceId, 1002);
    EXPECT_EQ(infoArray[1].deviceType, MIDI_DEVICE_TYPE_BLE);
    EXPECT_EQ(infoArray[1].nativeProtocol, MIDI_PROTOCOL_1_0);
    EXPECT_STREQ(infoArray[1].deviceName, "Mock_Drum");
    EXPECT_EQ(infoArray[1].vendorId, 0);
    EXPECT_EQ(infoArray[1].productId, 0);
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
        .WillOnce(Invoke([](int64_t id, std::vector<std::map<int32_t, std::string>> &ports) {
            ports.push_back({{PORT_INDEX, "0"},
                {DIRECTION, "0"},  // Input
                {PORT_NAME, "Midi_In_Port"}});
            ports.push_back({{PORT_INDEX, "1"},
                {DIRECTION, "1"},  // Output
                {PORT_NAME, "Midi_Out_Port"}});
            return MIDI_STATUS_OK;
        }));
    OH_MIDIPortInformation portArray[2];
    size_t numPorts = 2;
    OH_MIDIStatusCode status = client->GetDevicePorts(deviceId, portArray, &numPorts);

    EXPECT_EQ(status, MIDI_STATUS_OK);
    EXPECT_EQ(numPorts, 2);
    EXPECT_EQ(portArray[0].portIndex, 0);
    EXPECT_EQ(portArray[0].deviceId, 1001);
    EXPECT_EQ(portArray[0].direction, MIDI_PORT_DIRECTION_INPUT);
    EXPECT_STREQ(portArray[0].name, "Midi_In_Port");
    EXPECT_EQ(portArray[1].portIndex, 1);
    EXPECT_EQ(portArray[1].deviceId, 1001);
    EXPECT_EQ(portArray[1].direction, MIDI_PORT_DIRECTION_OUTPUT);
    EXPECT_STREQ(portArray[1].name, "Midi_Out_Port");
}

/**
 * @tc.name: GetDevicePorts_001
 * @tc.desc: Test GetDevicePorts when the device ID is invalid.
 * @tc.type: FUNC
 */
HWTEST_F(MidiClientUnitTest, GetDevicePorts_002, TestSize.Level0)
{
    int64_t invalidId = -1;

    // Simulate IPC returning error for invalid device
    EXPECT_CALL(*mockService, GetDevicePorts(invalidId, _)).WillOnce(Return(MIDI_STATUS_GENERIC_INVALID_ARGUMENT));

    OH_MIDIPortInformation portArray[1];
    size_t numPorts = 1;
    OH_MIDIStatusCode status = client->GetDevicePorts(invalidId, portArray, &numPorts);

    EXPECT_EQ(status, MIDI_STATUS_GENERIC_INVALID_ARGUMENT);
}

/**
 * @tc.name: ClosePort_001
 * @tc.desc: Test closing a port that was never opened.
 * @tc.type: FUNC
 */
HWTEST_F(MidiClientUnitTest, ClosePort_001, TestSize.Level0)
{
    int64_t deviceId = 102;
    uint32_t portIndex = 5;
    auto device = std::make_unique<MidiDevicePrivate>(mockService, deviceId);

    EXPECT_EQ(device->ClosePort(portIndex), MIDI_STATUS_INVALID_PORT);
}

/**
 * @tc.name: MidiDevicePrivate_CloseDevice_001
 * @tc.desc: CloseDevice should forward to IPC CloseDevice.
 * @tc.type: FUNC
 */
HWTEST_F(MidiClientUnitTest, MidiDevicePrivate_CloseDevice_001, TestSize.Level0)
{
    int64_t deviceId = 1001;
    auto device = std::make_unique<MidiDevicePrivate>(mockService, deviceId);

    EXPECT_CALL(*mockService, CloseDevice(deviceId)).Times(1).WillOnce(Return(MIDI_STATUS_OK));

    EXPECT_EQ(device->CloseDevice(), MIDI_STATUS_OK);
}

/**
 * @tc.name: MidiDevicePrivate_OpenInputPort_001
 * @tc.desc: OpenInputPort success: IPC OpenInputPort called once, receiver thread starts, ClosePort stops and closes.
 * @tc.type: FUNC
 */
HWTEST_F(MidiClientUnitTest, MidiDevicePrivate_OpenInputPort_001, TestSize.Level0)
{
    int64_t deviceId = 2001;
    uint32_t portIndex = 0;
    auto device = std::make_unique<MidiDevicePrivate>(mockService, deviceId);
    OH_MIDIPortDescriptor descriptor;
    descriptor.portIndex = portIndex;
    descriptor.protocol = MIDI_PROTOCOL_1_0;
    CallbackCapture callbackCapture;

    EXPECT_CALL(*mockService, OpenInputPort(_, deviceId, portIndex))
        .Times(1)
        .WillOnce(Invoke([](std::shared_ptr<MidiSharedRing> &buffer, int64_t, uint32_t) {
            buffer = MidiSharedRing::CreateFromLocal(256);
            return (buffer != nullptr) ? MIDI_STATUS_OK : MIDI_STATUS_SYSTEM_ERROR;
        }));

    EXPECT_CALL(*mockService, CloseInputPort(deviceId, portIndex)).Times(1).WillOnce(Return(MIDI_STATUS_OK));

    // Open input port -> should start receiver thread internally
    OH_MIDIStatusCode openStatus = device->OpenInputPort(descriptor, MidiReceivedTrampoline, &callbackCapture);
    EXPECT_EQ(openStatus, MIDI_STATUS_OK);

    // Close port -> should stop thread (via MidiInputPort destructor) and call IPC CloseInputPort
    OH_MIDIStatusCode closeStatus = device->ClosePort(portIndex);
    EXPECT_EQ(closeStatus, MIDI_STATUS_OK);

    EXPECT_EQ(device->ClosePort(portIndex), MIDI_STATUS_INVALID_PORT);
}

/**
 * @tc.name: MidiDevicePrivate_OpenInputPort_002
 * @tc.desc: OpenInputPort called twice on same portIndex should return OK without calling IPC second time.
 * @tc.type: FUNC
 */
HWTEST_F(MidiClientUnitTest, MidiDevicePrivate_OpenInputPort_002, TestSize.Level0)
{
    int64_t deviceId = 2002;
    uint32_t portIndex = 1;
    auto device = std::make_unique<MidiDevicePrivate>(mockService, deviceId);
    OH_MIDIPortDescriptor descriptor;
    descriptor.portIndex = portIndex;
    descriptor.protocol = MIDI_PROTOCOL_1_0;
    CallbackCapture callbackCapture;

    EXPECT_CALL(*mockService, OpenInputPort(_, deviceId, portIndex))
        .Times(1)
        .WillOnce(Invoke([](std::shared_ptr<MidiSharedRing> &buffer, int64_t, uint32_t) {
            buffer = MidiSharedRing::CreateFromLocal(256);
            return MIDI_STATUS_OK;
        }));

    EXPECT_CALL(*mockService, CloseInputPort(deviceId, portIndex)).Times(1).WillOnce(Return(MIDI_STATUS_OK));

    EXPECT_EQ(device->OpenInputPort(descriptor, MidiReceivedTrampoline, &callbackCapture), MIDI_STATUS_OK);
    // Second time should hit "already exists" branch and return ALREADY_OPEN without IPC.
    EXPECT_EQ(device->OpenInputPort(descriptor, MidiReceivedTrampoline, &callbackCapture),
        MIDI_STATUS_PORT_ALREADY_OPEN);
    EXPECT_EQ(device->ClosePort(portIndex), MIDI_STATUS_OK);
}

/**
 * @tc.name: MidiDevicePrivate_OpenInputPort_003
 * @tc.desc: OpenInputPort should return IPC error when OpenInputPort IPC fails.
 * @tc.type: FUNC
 */
HWTEST_F(MidiClientUnitTest, MidiDevicePrivate_OpenInputPort_003, TestSize.Level0)
{
    int64_t deviceId = 2003;
    uint32_t portIndex = 2;
    auto device = std::make_unique<MidiDevicePrivate>(mockService, deviceId);
    OH_MIDIPortDescriptor descriptor;
    descriptor.portIndex = portIndex;
    descriptor.protocol = MIDI_PROTOCOL_1_0;
    CallbackCapture callbackCapture;

    EXPECT_CALL(*mockService, OpenInputPort(_, deviceId, portIndex))
        .Times(1)
        .WillOnce(Return(MIDI_STATUS_GENERIC_INVALID_ARGUMENT));

    OH_MIDIStatusCode status = device->OpenInputPort(descriptor, MidiReceivedTrampoline, &callbackCapture);
    EXPECT_EQ(status, MIDI_STATUS_GENERIC_INVALID_ARGUMENT);

    EXPECT_EQ(device->ClosePort(portIndex), MIDI_STATUS_INVALID_PORT);
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
        MidiInputPort inputPort(nullptr, &callbackCapture, MIDI_PROTOCOL_1_0);
        EXPECT_FALSE(inputPort.StartReceiverThread());
        EXPECT_TRUE(inputPort.StopReceiverThread());  // should be safe even if never started
    }

    // 2) ringBuffer is nullptr -> Start should fail
    {
        MidiInputPort inputPort(MidiReceivedTrampoline, &callbackCapture, MIDI_PROTOCOL_1_0);
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

    MidiInputPort inputPort(MidiReceivedTrampoline, &callbackCapture, MIDI_PROTOCOL_1_0);
    std::shared_ptr<MidiSharedRing> localRing = MidiSharedRing::CreateFromLocal(512);
    ASSERT_NE(localRing, nullptr);

    // Assign ring buffer (public ref getter)
    inputPort.GetRingBuffer() = localRing;

    ASSERT_TRUE(inputPort.StartReceiverThread());

    // Write one event with notify=true to wake futex waiter
    std::vector<uint32_t> payloadWords{0x11111111, 0x22222222, 0x33333333};
    MidiEventInner midiEventInner = MakeMidiEventInner(10, payloadWords);

    ASSERT_EQ(MidiStatusCode::OK, localRing->TryWriteEvent(midiEventInner, true));

    // Wait callback
    ASSERT_TRUE(callbackCapture.WaitForAtLeast(1, std::chrono::milliseconds(200)));
    EXPECT_GE(callbackCapture.GetReceivedCount(), 1u);
    EXPECT_EQ(callbackCapture.GetLastEventCount(), 1u);

    auto lastEvents = callbackCapture.GetLastEvents();
    ASSERT_EQ(lastEvents.size(), 1u);
    EXPECT_EQ(lastEvents[0].timestamp, 10u);
    EXPECT_EQ(lastEvents[0].length, payloadWords.size());

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

    MidiInputPort inputPort(MidiReceivedTrampoline, &callbackCapture, MIDI_PROTOCOL_1_0);
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
    MidiOutputPort outputPort(MIDI_PROTOCOL_2_0);
    std::shared_ptr<MidiSharedRing> localRing = MidiSharedRing::CreateFromLocal(256);
    ASSERT_NE(localRing, nullptr);

    outputPort.GetRingBuffer() = localRing;
    uint32_t portIndex = 0;
    uint8_t data[] = {0xF0, 0x01, 0x02, 0x03, 0xF7};
    uint32_t byteSize = sizeof(data);

    std::vector<MidiEvent> midiEvents;
    std::vector<std::vector<uint32_t>> payloadBuffers;

    EXPECT_EQ(MIDI_STATUS_OK, outputPort.SendSysEx(portIndex, data, byteSize));
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
    MidiOutputPort outputPort(MIDI_PROTOCOL_2_0);
    std::shared_ptr<MidiSharedRing> localRing = MidiSharedRing::CreateFromLocal(256);
    ASSERT_NE(localRing, nullptr);

    outputPort.GetRingBuffer() = localRing;
    uint32_t portIndex = 0;
    const uint32_t largeSize = 1024 * 10;
    uint8_t data[largeSize];
    std::fill_n(data, largeSize, 0x01);

    EXPECT_EQ(MIDI_STATUS_TIMEOUT, outputPort.SendSysEx(portIndex, data, largeSize));
}

/**
 * @tc.name: MidiOutputPort_SendSysEx_003
 * @tc.desc: test muti packets case
 * @tc.type: FUNC
 */
HWTEST_F(MidiClientUnitTest, MidiOutputPort_SendSysEx_003, TestSize.Level0)
{
    MidiOutputPort outputPort(MIDI_PROTOCOL_2_0);
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

    EXPECT_EQ(MIDI_STATUS_OK, outputPort.SendSysEx(portIndex, data, byteSize));
    localRing->DrainToBatch(midiEvents, payloadBuffers, 0);
    EXPECT_EQ(midiEvents.size(), 2);
    EXPECT_EQ(midiEvents[0].data[0] & 0xFF, 0x01);
    EXPECT_EQ(midiEvents[0].data[1] & 0xFF, 0x03);
    EXPECT_EQ((midiEvents[0].data[0] >> 20) & 0xF, 0x01);
    EXPECT_EQ((midiEvents[1].data[0] >> 20) & 0xF, 0x03);
}