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

#include "midi_info.h"
#include "midi_device_driver.h"
#include "v1_0/imidi_interface.h"

namespace OHOS {
namespace MIDI {
class UsbMidiTransportDeviceDriver : public MidiDeviceDriver {
public:
    UsbMidiTransportDeviceDriver();
    ~UsbMidiTransportDeviceDriver() = default;

    std::vector<DeviceInformation> GetRegisteredDevices() override;

    std::vector<PortInformation> GetPortsForDevice(int64_t deviceId) override;

    bool OpenDevice(int64_t deviceId) override;

    bool CloseDevice(int64_t deviceId) override;

    bool HanleUmpInput(int64_t deviceId, size_t portIndex, MidiEvent list) override;
private:
    sptr<IMidiInterface> midiHdi_ = nullptr;
};
}
}
#endif