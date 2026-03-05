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
#ifndef MIDI_INFO_H
#define MIDI_INFO_H
#include <new>
#include <parcel.h>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <cstdint>
#include "native_midi_base.h"

namespace OHOS {
namespace MIDI {

enum PortDirection {
    PORT_DIRECTION_INPUT = 0,
    PORT_DIRECTION_OUTPUT = 1,
};

enum DeviceType {
    DEVICE_TYPE_USB = 0,
    DEVICE_TYPE_BLE = 1,
};

enum TransportProtocol {
    PROTOCOL_1_0 = 1,
    PROTOCOL_2_0 = 2,
};

struct MidiPortInfo : public Parcelable {
    int64_t portId;
    std::string name;
    PortDirection direction;
    TransportProtocol transportProtocol;

    bool Marshalling(Parcel &parcel) const override
    {
        parcel.WriteInt64(portId);
        parcel.WriteString(name);
        parcel.WriteInt32(static_cast<int32_t>(direction));
        parcel.WriteInt32(static_cast<int32_t>(transportProtocol));
        return true;
    }

    static MidiPortInfo *Unmarshalling(Parcel &parcel)
    {
        auto portInfo = new(std::nothrow) MidiPortInfo();
        if (portInfo == nullptr) {
            return nullptr;
        }

        portInfo->portId = parcel.ReadInt64();
        portInfo->name = parcel.ReadString();
        portInfo->direction = static_cast<PortDirection>(parcel.ReadInt32());
        portInfo->transportProtocol = static_cast<TransportProtocol>(parcel.ReadInt32());
        return portInfo;
    }
};

struct MidiDeviceInfo : public Parcelable {
    int64_t deviceId;
    int64_t driverDeviceId;
    DeviceType deviceType;
    TransportProtocol transportProtocol;
    std::string address;
    std::string deviceName;
    uint64_t productId;
    uint64_t vendorId;

    bool Marshalling(Parcel &parcel) const override
    {
        parcel.WriteInt64(deviceId);
        parcel.WriteInt64(driverDeviceId);
        parcel.WriteInt32(static_cast<int32_t>(deviceType));
        parcel.WriteInt32(static_cast<int32_t>(transportProtocol));
        parcel.WriteString(address);
        parcel.WriteString(deviceName);
        parcel.WriteUint64(productId);
        parcel.WriteUint64(vendorId);
        return true;
    }

    static MidiDeviceInfo *Unmarshalling(Parcel &parcel)
    {
        auto deviceInfo = new(std::nothrow) MidiDeviceInfo();
        if (deviceInfo == nullptr) {
            return nullptr;
        }

        deviceInfo->deviceId = parcel.ReadInt64();
        deviceInfo->driverDeviceId = parcel.ReadInt64();
        deviceInfo->deviceType = static_cast<DeviceType>(parcel.ReadInt32());
        deviceInfo->transportProtocol = static_cast<TransportProtocol>(parcel.ReadInt32());
        deviceInfo->address = parcel.ReadString();
        deviceInfo->deviceName = parcel.ReadString();
        deviceInfo->productId = parcel.ReadUint64();
        deviceInfo->vendorId = parcel.ReadUint64();
        return deviceInfo;
    }
};

enum DeviceChangeType {
    ADD = 0,
    REMOVED = 1,
};
 
struct DeviceInformation {
    MidiDeviceInfo midiDeviceInfo;
    std::vector<MidiPortInfo> portInfos;
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
    virtual void NotifyDeviceChange(DeviceChangeType change, const MidiDeviceInfo &deviceInfo) = 0;
    virtual void NotifyError(int32_t code) = 0;
};
} // namespace MIDI
} // namespace OHOS
#endif