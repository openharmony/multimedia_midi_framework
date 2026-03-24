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

#include "ble_midi_encoder.h"
#include <chrono>
#include <cstring>

std::vector<uint8_t> BleMidiPacketEncoder::EncodeEvent(
    const uint8_t* midiData, size_t midiLen, uint16_t timestampMs)
{
    std::vector<uint8_t> result;

    // Validate input
    if (midiData == nullptr || midiLen == 0) {
        return result;
    }

    // Mask timestamp to 13-bit range (0-8191 ms)
    uint16_t ts = timestampMs & BleMidiConstants::TIMESTAMP_MASK;

    // Header Byte: bit7=1, bit6=0 (must be 0), bits5-0 = timestamp high 6 bits
    uint8_t headerByte = 0x80 | ((ts >> 7) & 0x3F);

    // Timestamp Byte: bit7=1, bits6-0 = timestamp low 7 bits
    uint8_t timestampByte = 0x80 | (ts & 0x7F);

    // Reserve space: 2 bytes for header + worst case (TS before each status)
    result.reserve(2 + midiLen + (midiLen / 2));

    // Add header byte at the beginning
    result.push_back(headerByte);

    // Add timestamp byte before first status byte
    result.push_back(timestampByte);

    // Add MIDI data bytes, inserting timestamp before each status byte
    for (size_t i = 0; i < midiLen; ++i) {
        uint8_t byte = midiData[i];

        // Check if this is a MIDI status byte (MSB = 1, 0x80-0xFF)
        // But skip the first byte since we already added timestamp for it
        if (i > 0 && (byte & 0x80) != 0) {
            // This is a status byte, add timestamp before it
            result.push_back(timestampByte);
        }

        result.push_back(byte);
    }

    return result;
}

std::vector<uint8_t> BleMidiPacketEncoder::EncodeEvents(
    const std::vector<std::pair<std::vector<uint8_t>, uint16_t>>& events)
{
    std::vector<uint8_t> result;

    if (events.empty()) {
        return result;
    }

    // Pre-calculate total size for efficiency (worst case)
    size_t totalSize = 0;
    for (const auto& event : events) {
        // 2 bytes for header + timestamp + MIDI data + extra timestamps for status bytes
        totalSize += 2 + event.first.size() + (event.first.size() / 2);
    }
    result.reserve(totalSize);

    // Track if this is the first event (for header byte)
    bool firstEvent = true;

    // Encode each event
    for (const auto& event : events) {
        const auto& midiData = event.first;
        uint16_t timestampMs = event.second;

        // Skip empty events
        if (midiData.empty()) {
            continue;
        }

        // Mask timestamp to 13-bit range
        uint16_t ts = timestampMs & BleMidiConstants::TIMESTAMP_MASK;

        // Header Byte: bit7=1, bit6=0, bits5-0 = timestamp high 6 bits
        uint8_t headerByte = 0x80 | ((ts >> 7) & 0x3F);

        // Timestamp Byte: bit7=1, bits6-0 = timestamp low 7 bits
        uint8_t timestampByte = 0x80 | (ts & 0x7F);

        // Add header byte only for first event
        if (firstEvent) {
            result.push_back(headerByte);
            firstEvent = false;
        }

        // Add timestamp byte before first status byte
        result.push_back(timestampByte);

        // Add MIDI data bytes, inserting timestamp before each subsequent status byte
        for (size_t i = 0; i < midiData.size(); ++i) {
            uint8_t byte = midiData[i];

            // Check if this is a MIDI status byte (MSB = 1, 0x80-0xFF)
            // Skip the first byte since we already added timestamp for it
            if (i > 0 && (byte & 0x80) != 0) {
                // This is a status byte, add timestamp before it
                result.push_back(timestampByte);
            }

            result.push_back(byte);
        }
    }

    return result;
}
