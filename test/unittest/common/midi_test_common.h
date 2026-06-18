
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
#include "iremote_stub.h"
#include "midi_device_driver.h"
#include "midi_info.h"
#include <algorithm>
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "imidi_callback.h"
#include <vector>

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
    MOCK_METHOD(int32_t, HandleUmpInput, (int64_t deviceId, uint32_t portIndex, std::vector<MidiEventInner> &list),
        (override));
};

class MockMidiServiceCallback : public MidiServiceCallback {
public:
    MOCK_METHOD(void, NotifyDeviceChange, (DeviceChangeType change, const MidiDeviceInfo &deviceInfo),
                (override));
    MOCK_METHOD(void, NotifyError, (int32_t code), (override));
};

class MockMidiCallbackStub : public IRemoteStub<IMidiCallback> {
public:
    ErrCode NotifyDeviceChange(int32_t, const MidiDeviceInfo &) override
    {
        return 0;
    }

    ErrCode NotifyError(int32_t) override
    {
        return 0;
    }

    bool AddDeathRecipient(const sptr<DeathRecipient> &recipient) override
    {
        if (recipient == nullptr) {
            return false;
        }
        deathRecipients_.push_back(recipient);
        return true;
    }

    bool RemoveDeathRecipient(const sptr<DeathRecipient> &recipient) override
    {
        auto it = std::find(deathRecipients_.begin(), deathRecipients_.end(), recipient);
        if (it == deathRecipients_.end()) {
            return false;
        }
        deathRecipients_.erase(it);
        return true;
    }

private:
    std::vector<sptr<DeathRecipient>> deathRecipients_;
};
} // namespace MIDI
} // namespace OHOS
#endif
