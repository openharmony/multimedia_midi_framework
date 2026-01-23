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

#ifndef MIDI_DEVICE_USB_H
#define MIDI_DEVICE_USB_H

#include <vector>
#include "midi_info.h"
#include "midi_device_driver.h"
#include "v1_0/imidi_interface.h"

namespace OHOS {
namespace MIDI {

class UsbDriverCallback : public HDI::Midi::V1_0::IMidiCallback {
public:
    UsbDriverCallback(UmpInputCallback cb) : callback_(cb) {}
    int32_t OnMidiDataReceived(const std::vector<HDI::Midi::V1_0::MidiMessage> &messages) override;

private:
    UmpInputCallback callback_;
};

class UsbMidiTransportDeviceDriver : public MidiDeviceDriver {
public:
    UsbMidiTransportDeviceDriver();
    ~UsbMidiTransportDeviceDriver() = default;

    std::vector<DeviceInformation> GetRegisteredDevices() override;

    int32_t OpenDevice(int64_t deviceId) override;

    int32_t CloseDevice(int64_t deviceId) override;

    // register input callback while opening inputport
    int32_t OpenInputPort(int64_t deviceId, size_t portIndex, UmpInputCallback cb) override;

    int32_t OpenOutputPort(int64_t deviceId, uint32_t portIndex) override;

    int32_t CloseInputPort(int64_t deviceId, size_t portIndex) override;

    int32_t CloseOutputPort(int64_t deviceId, uint32_t portIndex) override;

    int32_t HanleUmpInput(int64_t deviceId, size_t portIndex, std::vector<MidiEventInner> &list) override;

private:
    sptr<HDI::Midi::V1_0::IMidiInterface> midiHdi_ = nullptr;
    std::vector<OHOS::HDI::Midi::V1_0::MidiMessage> messages_;
};
} // namespace MIDI
} // namespace OHOS
#endif