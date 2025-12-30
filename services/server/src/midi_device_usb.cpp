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
#ifndef LOG_TAG
#define LOG_TAG "UsbDeviceDriver"
#endif

#include "midi_log.h"
#include "midi_device_usb.h"

using namespace OHOS::HDI::Midi::V1_0;

namespace OHOS {
namespace MIDI {

UsbMidiTransportDeviceDriver::UsbMidiTransportDeviceDriver()
{
    midiHdi_ = IMidiInterface::Get(true);
}
static std::vector<PortInformation> ConvertToDeviceInformation(const MidiDeviceInfo device)
{
    std::vector<PortInformation> portInfos;
    for (const auto &port : device.ports)
    {
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
    midiHdi_->GetDeviceList(deviceList);
    std::vector<DeviceInformation> deviceInfos;
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
    return midiHdi_->OpenDevice(deviceId);
}

int32_t UsbMidiTransportDeviceDriver::CloseDevice(int64_t deviceId)
{
    return midiHdi_->CloseDevice(deviceId);
}


int32_t UsbMidiTransportDeviceDriver::OpenInputPort(int64_t deviceId, size_t portIndex, UmpInputCallback cb)
{   // to be done
    return 0;
}

int32_t UsbMidiTransportDeviceDriver::CloseInputPort(int64_t deviceId, size_t portIndex)
{
    return 0;
}

int32_t UsbMidiTransportDeviceDriver::HanleUmpInput(int64_t deviceId, size_t portIndex, MidiEvent list)
{
    return 0;
}

} // namespace MIDI
} // namespace OHOS