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
#ifndef MIDI_INFO_H
#define MIDI_INFO_H
#include <stddef.h>
#include <stdint.h>
#include <string>
#include <map>
#include <memory>
#include <vector>
#include "native_midi_base.h"

namespace OHOS {
namespace MIDI {
enum DeviceInformationProperty { DEVICE_ID, DEVICE_TYPE, MIDI_PROTOCOL, PRODUCT_NAME, VENDOR_NAME };

enum ProtInformationProperty { PORT_INDEX, DIRECTION, PORT_NAME };

enum PortDirection { PORT_DIRECTION_INPUT = 0, PORT_DIRECTION_OUTPUT = 1 };

enum DeviceType { DEVICE_TYPE_USB = 0, DEVICE_TYPE_BLE = 1 };

enum DeviceChangeType {
    ADD = 0,
    REMOVED = 1,
};

enum TransportProtocol {
    PROTOCOL_1_0,
    PROTOCOL_2_0,
};

struct PortInformation {
    int64_t portId;
    std::string name;
    PortDirection direction;
    TransportProtocol transportProtocol;
};

struct DeviceInformation {
    int64_t deviceId;
    int64_t driverDeviceId;
    DeviceType deviceType;
    TransportProtocol transportProtocol;
    std::string productName;
    std::string vendorName;
    std::vector<PortInformation> portInfos;
    DeviceInformation() : deviceId(0), deviceType(DeviceType::DEVICE_TYPE_USB) {}
};

struct MidiEvent {
    /**
     * @brief Timestamp in nanoseconds.
     * Base time obtained via clock_gettime(CLOCK_MONOTONIC, &time)
     * 0 indicates "send immediately".
     */
    uint64_t timestamp;

    /**
     * @brief Number of 32-bit words in the packet.
     * e.g., 1 for Type 2/4 (64-bit messages use 2 words)
     */
    size_t length;

    /**
     * @brief Pointer to UMP data (Must be 4-byte aligned).
     * This contains the raw UMP words (uint32_t).
     */
    uint32_t *data;
};

// read only
struct MidiEventInner {
    uint64_t timestamp;
    size_t length;
    const uint32_t *data;
};

class MidiServiceCallback {
public:
    virtual ~MidiServiceCallback() = default;
    virtual void NotifyDeviceChange(DeviceChangeType change, std::map<int32_t, std::string> deviceInfo) = 0;
    virtual void NotifyError(int32_t code) = 0;
};
} // namespace MIDI
} // namespace OHOS
#endif