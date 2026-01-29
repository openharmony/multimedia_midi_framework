
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
#ifndef MIDI_TEST_COMMON_H
#define MIDI_TEST_COMMON_H
#include "midi_device_driver.h"
#include "midi_info.h"
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "imidi_callback.h"

namespace OHOS {
namespace MIDI {

class MockMidiDeviceDriver : public MidiDeviceDriver {
public:
    MOCK_METHOD(std::vector<DeviceInformation>, GetRegisteredDevices, (), (override));
    MOCK_METHOD(int32_t, OpenDevice, (int64_t deviceId), (override));
    MOCK_METHOD(int32_t, OpenDevice, (std::string deviceAddr, BleDriverCallback deviceCallback), (override));
    MOCK_METHOD(int32_t, CloseDevice, (int64_t deviceId), (override));
    MOCK_METHOD(int32_t, OpenInputPort, (int64_t deviceId, uint32_t portIndex, UmpInputCallback cb), (override));
    MOCK_METHOD(int32_t, CloseInputPort, (int64_t deviceId, uint32_t portIndex), (override));
    MOCK_METHOD(int32_t, OpenOutputPort, (int64_t deviceId, uint32_t portIndex), (override));
    MOCK_METHOD(int32_t, CloseOutputPort, (int64_t deviceId, uint32_t portIndex), (override));
    MOCK_METHOD(int32_t, HanleUmpInput, (int64_t deviceId, uint32_t portIndex, std::vector<MidiEventInner> &list),
        (override));
};

class MockMidiServiceCallback : public IRemoteStub<IMidiCallback> {
public:
    MOCK_METHOD(ErrCode, NotifyDeviceChange, (int32_t change, (const std::map<int32_t, std::string> &deviceInfo)),
        (override));
    MOCK_METHOD(ErrCode, NotifyError, (int32_t code), (override));
};
} // namespace MIDI
} // namespace OHOS
#endif