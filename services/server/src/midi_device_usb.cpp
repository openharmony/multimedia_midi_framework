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
    return midiHdi_->OpenDevice(deviceId);
}

int32_t UsbMidiTransportDeviceDriver::HanleUmpInput(int64_t deviceId, size_t portIndex, MidiEvent list)
{
    return 0;
}

} // namespace MIDI
} // namespace OHOS