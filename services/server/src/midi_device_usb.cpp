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
#include "midi_utils.h"
#include "midi_device_usb.h"

using namespace OHOS::HDI::Midi::V1_0;

namespace OHOS {
namespace MIDI {

UsbMidiTransportDeviceDriver::UsbMidiTransportDeviceDriver() { midiHdi_ = IMidiInterface::Get(true); }
static std::vector<PortInformation> ConvertToDeviceInformation(const MidiDeviceInfo device)
{
    std::vector<PortInformation> portInfos;
    for (const auto &port : device.ports) {
        CHECK_AND_CONTINUE_LOG(port.direction == static_cast<int32_t>(PORT_DIRECTION_INPUT) || port.direction ==
            static_cast<int32_t>(PORT_DIRECTION_OUTPUT), "Invalid port direction: %{public}d", port.direction);
        PortInformation portInfo;
        portInfo.portId = port.portId;
        portInfo.name = port.name;
        portInfo.direction = static_cast<PortDirection>(port.direction);
        portInfo.transportProtocol = static_cast<TransportProtocol>(device.protocol);
        portInfos.push_back(portInfo);
    }
    return portInfos;
}

std::vector<DeviceInformation> UsbMidiTransportDeviceDriver::GetRegisteredDevices()
{
    std::vector<MidiDeviceInfo> deviceList;
    std::vector<DeviceInformation> deviceInfos;
    CHECK_AND_RETURN_RET_LOG(midiHdi_ != nullptr, deviceInfos, "midiHdi_ is nullptr");
    auto ret = midiHdi_->GetDeviceList(deviceList);
    CHECK_AND_RETURN_RET_LOG(ret == OH_MIDI_STATUS_OK, deviceInfos, "GetDeviceList failed: %{public}d", ret);
    for (const auto &device : deviceList) {
        CHECK_AND_CONTINUE_LOG(device.protocol == static_cast<int32_t>(PROTOCOL_1_0) || device.protocol ==
            static_cast<int32_t>(PROTOCOL_2_0), "Invalid MIDI protocol: %{public}d", device.protocol);
        DeviceInformation devInfo;
        devInfo.driverDeviceId = device.deviceId;
        devInfo.deviceType = DEVICE_TYPE_USB;
        devInfo.transportProtocol = static_cast<TransportProtocol>(device.protocol);
        devInfo.deviceName = device.productName;
        devInfo.productId = device.productName;
        devInfo.vendorId = device.vendorName;
        devInfo.portInfos = ConvertToDeviceInformation(device);
        deviceInfos.push_back(devInfo);
    }
    return deviceInfos;
}

int32_t UsbMidiTransportDeviceDriver::OpenDevice(int64_t deviceId)
{
    CHECK_AND_RETURN_RET_LOG(midiHdi_ != nullptr, OH_MIDI_STATUS_SYSTEM_ERROR, "midiHdi_ is nullptr");
    return midiHdi_->OpenDevice(deviceId);
}

int32_t UsbMidiTransportDeviceDriver::OpenDevice(std::string deviceAddr, BleDriverCallback deviceCallback)
{
    return -1;
}

int32_t UsbMidiTransportDeviceDriver::CloseDevice(int64_t deviceId)
{
    CHECK_AND_RETURN_RET_LOG(midiHdi_ != nullptr, OH_MIDI_STATUS_SYSTEM_ERROR, "midiHdi_ is nullptr");
    return midiHdi_->CloseDevice(deviceId);
}

int32_t UsbMidiTransportDeviceDriver::OpenInputPort(int64_t deviceId, uint32_t portIndex, UmpInputCallback cb)
{
    CHECK_AND_RETURN_RET_LOG(midiHdi_ != nullptr, OH_MIDI_STATUS_SYSTEM_ERROR, "midiHdi_ is nullptr");
    auto usbCallback = sptr<UsbDriverCallback>::MakeSptr(cb);
    return midiHdi_->OpenInputPort(deviceId, portIndex, usbCallback);
}

int32_t UsbMidiTransportDeviceDriver::CloseInputPort(int64_t deviceId, uint32_t portIndex)
{
    CHECK_AND_RETURN_RET_LOG(midiHdi_ != nullptr, OH_MIDI_STATUS_SYSTEM_ERROR, "midiHdi_ is nullptr");
    return midiHdi_->CloseInputPort(deviceId, portIndex);
}

int32_t UsbMidiTransportDeviceDriver::OpenOutputPort(int64_t deviceId, uint32_t portIndex)
{
    CHECK_AND_RETURN_RET_LOG(midiHdi_ != nullptr, OH_MIDI_STATUS_SYSTEM_ERROR, "midiHdi_ is nullptr");
    return midiHdi_->OpenOutputPort(deviceId, portIndex);
}

int32_t UsbMidiTransportDeviceDriver::CloseOutputPort(int64_t deviceId, uint32_t portIndex)
{
    CHECK_AND_RETURN_RET_LOG(midiHdi_ != nullptr, OH_MIDI_STATUS_SYSTEM_ERROR, "midiHdi_ is nullptr");
    return midiHdi_->CloseOutputPort(deviceId, portIndex);
}


int32_t UsbMidiTransportDeviceDriver::HandleUmpInput(int64_t deviceId, uint32_t portIndex,
    std::vector<MidiEventInner> &list)
{
    CHECK_AND_RETURN_RET_LOG(midiHdi_ != nullptr, OH_MIDI_STATUS_SYSTEM_ERROR, "midiHdi_ is nullptr");
    MIDI_DEBUG_LOG("%{public}s", DumpMidiEvents(list).c_str());
    std::vector<OHOS::HDI::Midi::V1_0::MidiMessage> messages;
    for (auto &event: list) {
        OHOS::HDI::Midi::V1_0::MidiMessage msg;
        msg.timestamp = static_cast<int64_t>(event.timestamp);
        for (size_t i = 0; i < event.length; ++i) {
            msg.data.push_back(event.data[i]);
        }
        messages.emplace_back(msg);
    }

    int32_t ret = midiHdi_->SendMidiMessages(deviceId, portIndex, messages);
    return ret;
}

int32_t UsbDriverCallback::OnMidiDataReceived(const std::vector<OHOS::HDI::Midi::V1_0::MidiMessage> &messages)
{
    std::vector<MidiEventInner> events;
    events.reserve(messages.size());
    MIDI_DEBUG_LOG("[server]: get midi events from hdi");
    for (auto &message : messages) {
        CHECK_AND_CONTINUE_LOG(!message.data.empty(), "Received MIDI message with empty data, skipping");
        MidiEventInner event = {
            .timestamp = message.timestamp,
            .length = message.data.size(),
            .data = message.data.data(),
        };
        events.emplace_back(event);
    }
    CHECK_AND_RETURN_RET(!events.empty(), 0);
    MIDI_DEBUG_LOG("%{public}s", DumpMidiEvents(events).c_str());
    callback_(events);
    return 0;
}
} // namespace MIDI
} // namespace OHOS