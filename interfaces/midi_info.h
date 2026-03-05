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
#include <stddef.h>
#include <stdint.h>
#include <string>
#include <map>
#include <memory>
#include <vector>
#include "native_midi_base.h"
#include "midi_types.h"

namespace OHOS {
namespace MIDI {
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