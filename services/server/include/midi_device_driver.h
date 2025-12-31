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
#ifndef MIDI_DEVICE_DRIVER_H
#define MIDI_DEVICE_DRIVER_H

#include <functional>
#include <vector>

#include "midi_info.h"

namespace OHOS {
namespace MIDI {

using UmpInputCallback = std::function<void(std::vector<MidiEventInner> &events)>;

class MidiDeviceDriver {
public:
    virtual ~MidiDeviceDriver() = default;

    virtual std::vector<DeviceInformation> GetRegisteredDevices() = 0;

    virtual int32_t OpenDevice(int64_t deviceId) = 0;

    virtual int32_t CloseDevice(int64_t deviceId) = 0;

    virtual int32_t OpenInputPort(int64_t deviceId, size_t portIndex, UmpInputCallback cb) = 0;

    virtual int32_t CloseInputPort(int64_t deviceId, size_t portIndex) = 0;

    virtual int32_t HanleUmpInput(int64_t deviceId, size_t portIndex, MidiEventInner list) = 0;
};

}
}
#endif