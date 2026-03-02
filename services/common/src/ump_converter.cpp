/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "ump_converter.h"
namespace {

// -------------------------
// Bit layout constants
// -------------------------
constexpr uint32_t SHIFT_MT            = 28;
constexpr uint32_t SHIFT_GROUP         = 24;

constexpr uint32_t SHIFT_STATUS_NIBBLE = 20;
constexpr uint32_t SHIFT_CHANNEL       = 16;
constexpr uint32_t SHIFT_DATA1         = 8;
constexpr uint32_t SHIFT_DATA2         = 0;

constexpr uint32_t SHIFT_BYTE3         = 24;
constexpr uint32_t SHIFT_BYTE2         = 16;
constexpr uint32_t SHIFT_BYTE1         = 8;
constexpr uint32_t SHIFT_BYTE0         = 0;
constexpr uint32_t SHIFT_BYTE          = 4;

constexpr uint32_t MASK_NIBBLE         = 0x0Fu;
constexpr uint32_t MASK_BYTE           = 0xFFu;

// MIDI data masks
constexpr uint8_t  MASK_7BIT           = 0x7Fu;
constexpr uint16_t MASK_14BIT          = 0x3FFFu;

// -------------------------
// UMP Message Types (MT)
// -------------------------
constexpr uint8_t MT_UTILITY_OR_SYSTEM = 0x1;
constexpr uint8_t MT_MIDI1_CHANNEL     = 0x2;
constexpr uint8_t MT_DATA_SYSEX7       = 0x3;
constexpr uint8_t MT_MIDI2_CHANNEL     = 0x4;

// -------------------------
// MIDI Status Nibbles
// -------------------------
constexpr uint8_t STATUS_NOTE_OFF      = 0x8;
constexpr uint8_t STATUS_NOTE_ON       = 0x9;
constexpr uint8_t STATUS_POLY_PRESSURE = 0xA;
constexpr uint8_t STATUS_CC            = 0xB;
constexpr uint8_t STATUS_PROGRAM       = 0xC;
constexpr uint8_t STATUS_CH_PRESSURE   = 0xD;
constexpr uint8_t STATUS_PITCH_BEND    = 0xE;

// -------------------------
// Scaling between resolutions
// -------------------------
constexpr uint32_t SHIFT_U7_TO_U32  = 25;
constexpr uint32_t SHIFT_U14_TO_U32 = 18;
constexpr uint32_t SHIFT_U7 = 7;

constexpr uint32_t ROUND_U32_TO_U7  = 1u << (SHIFT_U7_TO_U32 - 1);
constexpr uint32_t ROUND_U32_TO_U14 = 1u << (SHIFT_U14_TO_U32 - 1);

// -------------------------
// Other constants
// -------------------------
constexpr uint8_t MIDI2_NOTE_ATTR_NONE = 0;

// MT=0x3 is 64-bit aligned: 2 words per packet
constexpr size_t WORDS_PER_64BIT_PACKET = 2;
} // namespace

// -------------------------
// Basic helpers
// -------------------------
uint8_t UmpConverter::Byte3(uint32_t w) { return static_cast<uint8_t>((w >> SHIFT_BYTE3) & MASK_BYTE); }
uint8_t UmpConverter::Byte2(uint32_t w) { return static_cast<uint8_t>((w >> SHIFT_BYTE2) & MASK_BYTE); }
uint8_t UmpConverter::Byte1(uint32_t w) { return static_cast<uint8_t>((w >> SHIFT_BYTE1) & MASK_BYTE); }
uint8_t UmpConverter::Byte0(uint32_t w) { return static_cast<uint8_t>((w >> SHIFT_BYTE0) & MASK_BYTE); }

uint8_t UmpConverter::MessageType(uint32_t w0) { return static_cast<uint8_t>((w0 >> SHIFT_MT) & MASK_NIBBLE); }
uint8_t UmpConverter::Group(uint32_t w0)       { return static_cast<uint8_t>((w0 >> SHIFT_GROUP) & MASK_NIBBLE); }

// -------------------------
// Scaling
// -------------------------
uint32_t UmpConverter::U7ToU32(uint8_t v7)
{
    return static_cast<uint32_t>(v7) << SHIFT_U7_TO_U32;
}

uint32_t UmpConverter::U14ToU32(uint16_t v14)
{
    return static_cast<uint32_t>(v14) << SHIFT_U14_TO_U32;
}

uint8_t UmpConverter::U32ToU7(uint32_t v32)
{
    return static_cast<uint8_t>((v32 + ROUND_U32_TO_U7) >> SHIFT_U7_TO_U32);
}

uint16_t UmpConverter::U32ToU14(uint32_t v32)
{
    return static_cast<uint16_t>((v32 + ROUND_U32_TO_U14) >> SHIFT_U14_TO_U32);
}

// -------------------------
// Builders (<=5 args)
// -------------------------
void UmpConverter::PushMidi2ChannelVoice(std::vector<uint32_t>& out,
                                         const Midi2ChannelVoiceMsg& msg)
{
    const uint32_t w0 =
        (static_cast<uint32_t>(MT_MIDI2_CHANNEL) << SHIFT_MT) |
        (static_cast<uint32_t>(msg.group & MASK_NIBBLE) << SHIFT_GROUP) |
        (static_cast<uint32_t>(msg.statusNibble & MASK_NIBBLE) << SHIFT_STATUS_NIBBLE) |
        (static_cast<uint32_t>(msg.channel & MASK_NIBBLE) << SHIFT_CHANNEL) |
        (static_cast<uint32_t>(msg.data1) << SHIFT_DATA1) |
        (static_cast<uint32_t>(msg.data2) << SHIFT_DATA2);

    out.push_back(w0);
    out.push_back(msg.value32);
}

void UmpConverter::PushMidi1ChannelVoice(std::vector<uint32_t>& out,
                                         const Midi1ChannelVoiceMsg& msg)
{
    const uint32_t w0 =
        (static_cast<uint32_t>(MT_MIDI1_CHANNEL) << SHIFT_MT) |
        (static_cast<uint32_t>(msg.group & MASK_NIBBLE) << SHIFT_GROUP) |
        (static_cast<uint32_t>(msg.statusByte) << SHIFT_BYTE2) |
        (static_cast<uint32_t>(msg.data1) << SHIFT_BYTE1) |
        (static_cast<uint32_t>(msg.data2) << SHIFT_BYTE0);

    out.push_back(w0);
}

// -------------------------
// Public entry
// -------------------------
bool UmpConverter::ConvertOne(Direction dir,
                              const uint32_t* inWords,
                              size_t inWordCount,
                              std::vector<uint32_t>& outWords)
{
    if (inWords == nullptr || inWordCount == 0) {
        return false;
    }

    const uint32_t w0 = inWords[0];
    const uint8_t mt = MessageType(w0);
    // MT=0x1: System Common & System Real Time -> pass-through
    if (mt == MT_UTILITY_OR_SYSTEM) {
        if (inWordCount != 1) {
            return false;
        }
        outWords.push_back(inWords[0]);
        return true;
    }

    // MT=0x3: SysEx7/Data -> pass-through
    if (mt == MT_DATA_SYSEX7) {
        if ((inWordCount % WORDS_PER_64BIT_PACKET) != 0) {
            return false;
        }
        for (size_t i = 0; i < inWordCount; ++i) {
            outWords.push_back(inWords[i]);
        }
        return true;
    }

    if (dir == Direction::Midi1ToMidi2) {
        if (mt != MT_MIDI1_CHANNEL) {
            return false; // drop
        }
        return ConvertMidi1ChannelVoiceToMidi2(inWords, inWordCount, outWords);
    }

    // Midi2ToMidi1
    if (mt != MT_MIDI2_CHANNEL) {
        return false; // drop
    }
    return ConvertMidi2ChannelVoiceToMidi1(inWords, inWordCount, outWords);
}

// -------------------------
// MIDI1 Channel Voice -> MIDI2 Channel Voice
// -------------------------
bool UmpConverter::ConvertMidi1ChannelVoiceToMidi2(const uint32_t* inWords,
                                                   size_t inWordCount,
                                                   std::vector<uint32_t>& out)
{
    if (inWordCount != 1) {
        return false;
    }

    const uint32_t w0 = inWords[0];
    const uint8_t group = Group(w0);

    const uint8_t status = Byte2(w0);
    const uint8_t data1  = Byte1(w0);
    const uint8_t data2  = Byte0(w0);

    const uint8_t statusNibble = static_cast<uint8_t>((status >> SHIFT_BYTE) & MASK_NIBBLE);
    const uint8_t channel      = static_cast<uint8_t>(status & MASK_NIBBLE);

    Midi2ChannelVoiceMsg m{};
    m.group = group;
    m.statusNibble = statusNibble;
    m.channel = channel;
    return ConvertMidi1ChannelVoiceToMidi2Inner(statusNibble, data1, data2, m, out);
}

// -------------------------
// MIDI2 Channel Voice -> MIDI1 Channel Voice (lossy)
// -------------------------
bool UmpConverter::ConvertMidi2ChannelVoiceToMidi1(const uint32_t* inWords,
                                                   size_t inWordCount,
                                                   std::vector<uint32_t>& out)
{
    if (inWordCount != WORDS_PER_64BIT_PACKET) {
        return false;
    }

    const uint32_t w0 = inWords[0];
    const uint32_t w1 = inWords[1];

    Midi1ChannelVoiceMsg m{};
    m.group = Group(w0);

    const uint8_t statusNibble = static_cast<uint8_t>((w0 >> SHIFT_STATUS_NIBBLE) & MASK_NIBBLE);
    const uint8_t channel      = static_cast<uint8_t>((w0 >> SHIFT_CHANNEL) & MASK_NIBBLE);
    const uint8_t data1        = static_cast<uint8_t>((w0 >> SHIFT_DATA1) & MASK_BYTE);
    // const uint8_t data2        = static_cast<uint8_t>((w0 >> SHIFT_DATA2) & MASK_BYTE);

    m.statusByte = static_cast<uint8_t>((statusNibble << SHIFT_BYTE) | (channel & MASK_NIBBLE));
    return ConvertMidi2ChannelVoiceToMidi1Inner(statusNibble, data1, w1, m, out);
}

bool UmpConverter::ConvertMidi1ChannelVoiceToMidi2Inner(uint8_t statusNibble,
                                                        uint8_t data1,
                                                        uint8_t data2,
                                                        Midi2ChannelVoiceMsg& m,
                                                        std::vector<uint32_t>& out)
{
    switch (statusNibble) {
        case STATUS_NOTE_OFF:
        case STATUS_NOTE_ON:
            m.data1 = static_cast<uint8_t>(data1 & MASK_7BIT);     // note
            m.data2 = MIDI2_NOTE_ATTR_NONE;                        // attribute type
            m.value32 = U7ToU32(static_cast<uint8_t>(data2 & MASK_7BIT)); // velocity
            PushMidi2ChannelVoice(out, m);
            return true;

        case STATUS_POLY_PRESSURE:
            m.data1 = static_cast<uint8_t>(data1 & MASK_7BIT);     // note
            m.data2 = 0;
            m.value32 = U7ToU32(static_cast<uint8_t>(data2 & MASK_7BIT)); // pressure
            PushMidi2ChannelVoice(out, m);
            return true;

        case STATUS_CC:
            m.data1 = data1;                                       // controller
            m.data2 = 0;
            m.value32 = U7ToU32(static_cast<uint8_t>(data2 & MASK_7BIT)); // value
            PushMidi2ChannelVoice(out, m);
            return true;

        case STATUS_PROGRAM:
            m.data1 = static_cast<uint8_t>(data1 & MASK_7BIT);     // program
            m.data2 = 0;
            m.value32 = U7ToU32(m.data1);
            PushMidi2ChannelVoice(out, m);
            return true;

        case STATUS_CH_PRESSURE:
            m.data1 = 0;
            m.data2 = 0;
            m.value32 = U7ToU32(static_cast<uint8_t>(data1 & MASK_7BIT)); // pressure in data1
            PushMidi2ChannelVoice(out, m);
            return true;

        case STATUS_PITCH_BEND: {
            const uint16_t lsb7 = static_cast<uint16_t>(data1 & MASK_7BIT);
            const uint16_t msb7 = static_cast<uint16_t>(data2 & MASK_7BIT);
            const uint16_t pb14 = static_cast<uint16_t>((msb7 << 7) | lsb7);

            m.data1 = 0;
            m.data2 = 0;
            m.value32 = U14ToU32(static_cast<uint16_t>(pb14 & MASK_14BIT));
            PushMidi2ChannelVoice(out, m);
            return true;
        }

        default:
            return false; // drop
    }
}

bool UmpConverter::ConvertMidi2ChannelVoiceToMidi1Inner(uint8_t statusNibble,
                                                        uint8_t data1,
                                                        uint32_t w1,
                                                        Midi1ChannelVoiceMsg& m,
                                                        std::vector<uint32_t>& out)
{
    switch (statusNibble) {
        case STATUS_NOTE_OFF:
        case STATUS_NOTE_ON:
        case STATUS_POLY_PRESSURE:
            m.data1 = static_cast<uint8_t>(data1 & MASK_7BIT);
            m.data2 = static_cast<uint8_t>(U32ToU7(w1) & MASK_7BIT);
            PushMidi1ChannelVoice(out, m);
            return true;

        case STATUS_CC:
            m.data1 = data1;
            m.data2 = static_cast<uint8_t>(U32ToU7(w1) & MASK_7BIT);
            PushMidi1ChannelVoice(out, m);
            return true;

        case STATUS_PROGRAM:
            m.data1 = static_cast<uint8_t>(data1 & MASK_7BIT);
            m.data2 = 0;
            PushMidi1ChannelVoice(out, m);
            return true;

        case STATUS_CH_PRESSURE:
            m.data1 = static_cast<uint8_t>(U32ToU7(w1) & MASK_7BIT);
            m.data2 = 0;
            PushMidi1ChannelVoice(out, m);
            return true;

        case STATUS_PITCH_BEND: {
            const uint16_t pb14 = static_cast<uint16_t>(U32ToU14(w1) & MASK_14BIT);
            m.data1 = static_cast<uint8_t>(pb14 & MASK_7BIT);                    // LSB
            m.data2 = static_cast<uint8_t>((pb14 >> SHIFT_U7) & MASK_7BIT);       // MSB
            PushMidi1ChannelVoice(out, m);
            return true;
        }

        default:
            // Not convertible => drop
            return false;
    }
}