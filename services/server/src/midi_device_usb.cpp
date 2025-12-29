#include "midi_log.h"
#include "midi_device_usb.h"

using namespace OHOS::HDI::Midi::V1_0;

namespace OHOS {
namespace MIDI {

UsbMidiTransportDeviceDriver::UsbMidiTransportDeviceDriver()
{
    midiHdi_ = IMidiInterface::Get(true);
}

std::vector<DeviceInformation> UsbMidiTransportDeviceDriver::GetRegisteredDevices()
{
    // 默认返回空列表
    return std::vector<DeviceInformation>();
}

std::vector<PortInformation> UsbMidiTransportDeviceDriver::GetPortsForDevice(int64_t deviceId)
{
    // 默认返回空列表
    return std::vector<PortInformation>();
}

bool UsbMidiTransportDeviceDriver::OpenDevice(int64_t deviceId)
{
    // 默认返回失败
    return midiHdi_->OpenDevice(deviceId);
}

bool UsbMidiTransportDeviceDriver::CloseDevice(int64_t deviceId)
{
    // 默认返回失败
    return false;
}

bool UsbMidiTransportDeviceDriver::HanleUmpInput(int64_t deviceId, size_t portIndex, MidiEvent list)
{
    // 默认返回失败
    return false;
}

} // namespace MIDI
} // namespace OHOS