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

#ifndef BLE_MIDI_ENCODER_H
#define BLE_MIDI_ENCODER_H

#include <vector>
#include <cstdint>

namespace BleMidiConstants {
    // BLE MIDI timestamp is 13 bits, max value 8191 ms (~8 seconds before wrap)
    constexpr uint16_t TIMESTAMP_MASK = 0x1FFF;
    constexpr uint16_t TIMESTAMP_MAX = 8191;

    // Time conversion constants
    constexpr uint64_t NANOSECONDS_PER_MILLISECOND = 1000000;
} // namespace BleMidiConstants

/**
 * @brief BLE MIDI packet encoder following Bluetooth MIDI specification.
 *
 * This class encodes MIDI 1.0 data into BLE MIDI packet format with timestamps.
 * Simplified encoding format:
 *   [Header Byte][Timestamp Byte][MIDI Data...]
 *
 * According to the BLE MIDI spec:
 * - Timestamp is 13 bits (0-8191 ms)
 * - Header Byte: 0x80 | ((timestamp >> 7) & 0x3F)  // bit7=1, bit6=0
 * - Timestamp Byte: 0x80 | (timestamp & 0x7F)       // bit7=1, bits6-0=low 7 bits
 */
class BleMidiPacketEncoder {
public:
    /**
     * @brief Encode a single MIDI event with BLE MIDI timestamp header.
     * @param midiData Pointer to MIDI 1.0 data bytes.
     * @param midiLen Length of MIDI data.
     * @param timestampMs 13-bit timestamp in milliseconds (0-8191). Will be masked to 13 bits.
     * @return Encoded BLE MIDI packet (empty if input is invalid).
     */
    static std::vector<uint8_t> EncodeEvent(
        const uint8_t* midiData,
        size_t midiLen,
        uint16_t timestampMs
    );

    /**
     * @brief Encode multiple MIDI events into a single BLE packet.
     * @param events Vector of (midi_data, timestamp) pairs.
     * @return Encoded BLE MIDI packet containing all events.
     */
    static std::vector<uint8_t> EncodeEvents(
        const std::vector<std::pair<std::vector<uint8_t>, uint16_t>>& events
    );
};

#endif // BLE_MIDI_ENCODER_H
