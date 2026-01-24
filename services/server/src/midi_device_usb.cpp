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
#ifndef LOG_TAG
#define LOG_TAG "UsbDeviceDriver"
#endif

#include "midi_log.h"
#include "midi_device_usb.h"

using namespace OHOS::HDI::Midi::V1_0;

namespace OHOS {
namespace MIDI {

UsbMidiTransportDeviceDriver::UsbMidiTransportDeviceDriver() { midiHdi_ = IMidiInterface::Get(true); }
static std::vector<PortInformation> ConvertToDeviceInformation(const MidiDeviceInfo device)
{
    std::vector<PortInformation> portInfos;
    for (const auto &port : device.ports) {
        PortInformation portInfo;
        portInfo.portId = port.portId;
        portInfo.name = port.name;
        portInfo.direction = (PortDirection)port.direction;
        portInfo.transportProtocol = (TransportProtocol)device.protocol;
        portInfos.push_back(portInfo);
    }
    return portInfos;
}

std::vector<DeviceInformation> UsbMidiTransportDeviceDriver::GetRegisteredDevices()
{
    std::vector<MidiDeviceInfo> deviceList;
    std::vector<DeviceInformation> deviceInfos;
    CHECK_AND_RETURN_RET_LOG(midiHdi_ != nullptr, deviceInfos, "midiHdi_ is nullptr");
    midiHdi_->GetDeviceList(deviceList);
    for (auto device : deviceList) {
        DeviceInformation devInfo;
        devInfo.driverDeviceId = device.deviceId;
        devInfo.deviceType = DEVICE_TYPE_USB;
        devInfo.transportProtocol = (TransportProtocol)device.protocol;
        devInfo.productName = device.productName;
        devInfo.vendorName = device.vendorName;
        devInfo.portInfos = ConvertToDeviceInformation(device);
        deviceInfos.push_back(devInfo);
    }
    return deviceInfos;
}

int32_t UsbMidiTransportDeviceDriver::OpenDevice(int64_t deviceId)
{
    CHECK_AND_RETURN_RET_LOG(midiHdi_ != nullptr, MIDI_STATUS_UNKNOWN_ERROR, "midiHdi_ is nullptr");
    return midiHdi_->OpenDevice(deviceId);
}

int32_t UsbMidiTransportDeviceDriver::OpenDevice(std::string deviceAddr, BleDriverCallback deviceCallback)
{
    return -1;
}

int32_t UsbMidiTransportDeviceDriver::CloseDevice(int64_t deviceId)
{
    CHECK_AND_RETURN_RET_LOG(midiHdi_ != nullptr, MIDI_STATUS_UNKNOWN_ERROR, "midiHdi_ is nullptr");
    return midiHdi_->CloseDevice(deviceId);
}

int32_t UsbMidiTransportDeviceDriver::OpenInputPort(int64_t deviceId, size_t portIndex, UmpInputCallback cb)
{
    CHECK_AND_RETURN_RET_LOG(midiHdi_ != nullptr, MIDI_STATUS_UNKNOWN_ERROR, "midiHdi_ is nullptr");
    auto usbCallback = sptr<UsbDriverCallback>::MakeSptr(cb);
    return midiHdi_->OpenInputPort(deviceId, portIndex, usbCallback);
}

int32_t UsbMidiTransportDeviceDriver::CloseInputPort(int64_t deviceId, size_t portIndex)
{
    CHECK_AND_RETURN_RET_LOG(midiHdi_ != nullptr, MIDI_STATUS_UNKNOWN_ERROR, "midiHdi_ is nullptr");
    return midiHdi_->CloseInputPort(deviceId, portIndex);
}

int32_t UsbMidiTransportDeviceDriver::OpenOutputPort(int64_t deviceId, size_t portIndex)
{
    CHECK_AND_RETURN_RET_LOG(midiHdi_ != nullptr, MIDI_STATUS_UNKNOWN_ERROR, "midiHdi_ is nullptr");
    return midiHdi_->OpenOutputPort(deviceId, portIndex);
}

int32_t UsbMidiTransportDeviceDriver::CloseOutputPort(int64_t deviceId, size_t portIndex)
{
    CHECK_AND_RETURN_RET_LOG(midiHdi_ != nullptr, MIDI_STATUS_UNKNOWN_ERROR, "midiHdi_ is nullptr");
    return midiHdi_->CloseOutputPort(deviceId, portIndex);
}


int32_t UsbMidiTransportDeviceDriver::HanleUmpInput(int64_t deviceId, size_t portIndex, MidiEventInner list)
{
    return 0;
}

int32_t UsbDriverCallback::OnMidiDataReceived(const std::vector<OHOS::HDI::Midi::V1_0::MidiMessage> &messages)
{
    std::vector<MidiEventInner> events;
    events.reserve(messages.size());
    for (auto &message : messages) {
        MidiEventInner event = {
            .timestamp = message.timestamp,
            .length = message.data.size(),
            .data = message.data.data(),
        };
        events.emplace_back(event);
    }
    callback_(events);
    return 0;
}
} // namespace MIDI
} // namespace OHOS