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
#define LOG_TAG "MidiServerDump"
#endif

#include "midi_server_dump.h"

#include <sstream>
#include <codecvt>
#include <locale>
#include "midi_service_controller.h"
#include "midi_device_mananger.h"
#include "midi_info.h"
#include "midi_log.h"

namespace OHOS {
namespace MIDI {

MidiServerDump::MidiServerDump()
{
    InitDumpFuncMap();
}

void MidiServerDump::InitDumpFuncMap()
{
    dumpFuncMap_[u"-h"] = &MidiServerDump::HelpInfoDump;
    dumpFuncMap_[u"-d"] = &MidiServerDump::DeviceListDump;
    dumpFuncMap_[u"-c"] = &MidiServerDump::ClientInfoDump;
    dumpFuncMap_[u"-p"] = &MidiServerDump::PortMappingDump;
    dumpFuncMap_[u"-s"] = &MidiServerDump::StatisticsDump;
}

void MidiServerDump::HandleDump(std::string &dumpString, std::queue<std::u16string> &argQue)
{
    if (argQue.empty()) {
        AllInfoDump(dumpString);
        return;
    }

    while (!argQue.empty()) {
        std::u16string para = argQue.front();
        argQue.pop();

        if (dumpFuncMap_.find(para) != dumpFuncMap_.end()) {
            (this->*dumpFuncMap_[para])(dumpString);
        } else {
            dumpString += "Unknown parameter: ";
            dumpString += std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t>{}.to_bytes(para);
            dumpString += "\n\n";
            HelpInfoDump(dumpString);
            return;
        }
    }
}

void MidiServerDump::HelpInfoDump(std::string &dumpString)
{
    dumpString += "MIDI Service Dump Commands:\n";
    dumpString += "  -h  | Help information\n";
    dumpString += "  -d  | Dump device list and open status\n";
    dumpString += "  -c  | Dump client information\n";
    dumpString += "  -p  | Dump port mapping and open status\n";
    dumpString += "  -s  | Dump traffic statistics\n";
    dumpString += "\nUsage: hidumper -s <MIDI_SERVICE_ID> -a \"<command>\"\n";
    dumpString += "Example: hidumper -s <MIDI_SERVICE_ID> -a \"-d -c\"\n";
}

void MidiServerDump::AllInfoDump(std::string &dumpString)
{
    dumpString += "================ MIDI Service Dump ================\n\n";
    DumpServiceStatus(dumpString);
    dumpString += "\n";
    DeviceListDump(dumpString);
    dumpString += "\n";
    ClientInfoDump(dumpString);
    dumpString += "\n";
    PortMappingDump(dumpString);
    dumpString += "\n";
    StatisticsDump(dumpString);
    dumpString += "\n==================================================\n";
}

void MidiServerDump::DumpServiceStatus(std::string &dumpString)
{
    auto controller = MidiServiceController::GetInstance();
    if (controller == nullptr) {
        dumpString += "[Service Status]\n  Service State: Not Running\n";
        return;
    }

    dumpString += "[Service Status]\n";
    dumpString += "  Service State: Running\n";

    auto devices = controller->GetDevices();
    dumpString += "  Total Devices: " + std::to_string(devices.size()) + "\n";
}

std::string MidiServerDump::DeviceTypeToString(int type) const
{
    switch (type) {
        case static_cast<int>(DEVICE_TYPE_USB): return "USB";
        case static_cast<int>(DEVICE_TYPE_BLE): return "BLE";
        default: return "Unknown";
    }
}

std::string MidiServerDump::ProtocolToString(int protocol) const
{
    switch (protocol) {
        case static_cast<int>(PROTOCOL_1_0): return "MIDI 1.0";
        case static_cast<int>(PROTOCOL_2_0): return "MIDI 2.0";
        default: return "Unknown";
    }
}

std::string MidiServerDump::PortDirectionToString(int direction) const
{
    switch (direction) {
        case static_cast<int>(PORT_DIRECTION_INPUT): return "INPUT";
        case static_cast<int>(PORT_DIRECTION_OUTPUT): return "OUTPUT";
        default: return "Unknown";
    }
}

void MidiServerDump::DeviceListDump(std::string &dumpString)
{
    auto controller = MidiServiceController::GetInstance();
    if (controller == nullptr) {
        dumpString += "[Device List]\n  Controller not available\n";
        return;
    }

    auto devices = controller->GetDevices();
    dumpString += "[Device List]\n";
    dumpString += "- " + std::to_string(devices.size()) + " Device(s) available:\n\n";

    for (size_t i = 0; i < devices.size(); ++i) {
        const auto &device = devices[i];
        dumpString += "  Device " + std::to_string(i + 1) + ":\n";
        dumpString += "  - Device ID: " + std::to_string(device.deviceId) + "\n";
        dumpString += "  - Name: " + device.deviceName + "\n";
        dumpString += "  - Type: " + DeviceTypeToString(static_cast<int>(device.deviceType)) + "\n";
        dumpString += "  - Address: " + device.address + "\n";
        dumpString += "  - Protocol: " + ProtocolToString(static_cast<int>(device.transportProtocol)) + "\n";
        dumpString += "  - Product ID: 0x" + std::to_string(device.productId) + "\n";
        dumpString += "  - Vendor ID: 0x" + std::to_string(device.vendorId) + "\n";

        // Get port info
        std::vector<MidiPortInfo> portInfos;
        controller->GetDevicePorts(device.deviceId, portInfos);
        dumpString += "  - Ports (" + std::to_string(portInfos.size()) + "):\n";
        for (const auto &port : portInfos) {
            dumpString += "    - Port " + std::to_string(port.portId) + ": "
                        + PortDirectionToString(static_cast<int>(port.direction))
                        + " (" + ProtocolToString(static_cast<int>(port.transportProtocol)) + ")\n";
        }
        dumpString += "\n";
    }
}

void MidiServerDump::ClientInfoDump(std::string &dumpString)
{
    auto controller = MidiServiceController::GetInstance();
    if (controller == nullptr) {
        dumpString += "[Client Information]\n  Controller not available\n";
        return;
    }
    controller->DumpClientInfo(dumpString);
}

void MidiServerDump::PortMappingDump(std::string &dumpString)
{
    auto controller = MidiServiceController::GetInstance();
    if (controller == nullptr) {
        dumpString += "[Port Mapping]\n  Controller not available\n";
        return;
    }
    controller->DumpPortMapping(dumpString);
}

void MidiServerDump::StatisticsDump(std::string &dumpString)
{
    auto controller = MidiServiceController::GetInstance();
    if (controller == nullptr) {
        dumpString += "[Traffic Statistics]\n  Controller not available\n";
        return;
    }
    controller->DumpStatistics(dumpString);
}

} // namespace MIDI
} // namespace OHOS
