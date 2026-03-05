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
#ifndef UMP_CONVERTER_H
#define UMP_CONVERTER_H
#include <cstdint>
#include <vector>

class UmpConverter final {
public:
    enum class Direction {
        Midi1ToMidi2,
        Midi2ToMidi1
    };

    struct Midi2ChannelVoiceMsg {
        uint8_t group = 0;        // 0..15
        uint8_t statusNibble = 0; // 0x8..0xE
        uint8_t channel = 0;      // 0..15
        uint8_t data1 = 0;        // meaning depends on status
        uint8_t data2 = 0;        // meaning depends on status (note attr type / reserved)
        uint32_t value32 = 0;     // 32-bit value
    };

    struct Midi1ChannelVoiceMsg {
        uint8_t group = 0;      // 0..15
        uint8_t statusByte = 0; // 0x8n..0xEn
        uint8_t data1 = 0;      // 0..127 typically
        uint8_t data2 = 0;      // 0..127 typically
    };

    static bool ConvertOne(Direction dir,
                           const uint32_t* inWords,
                           size_t inWordCount,
                           std::vector<uint32_t>& outWords);

    static bool ConvertMidi1ToMidi2(const uint32_t* inWords,
                                    size_t inWordCount,
                                    std::vector<uint32_t>& outWords)
    {
        return ConvertOne(Direction::Midi1ToMidi2, inWords, inWordCount, outWords);
    }

    static bool ConvertMidi2ToMidi1(const uint32_t* inWords,
                                    size_t inWordCount,
                                    std::vector<uint32_t>& outWords)
    {
        return ConvertOne(Direction::Midi2ToMidi1, inWords, inWordCount, outWords);
    }

private:
    // Helpers: parsing
    static uint8_t MessageType(uint32_t w0);
    static uint8_t Group(uint32_t w0);

    // MIDI 1.0 Channel Voice UMP bytes: [B3]=MT|GR, [B2]=Status, [B1]=Data1, [B0]=Data2
    static uint8_t Byte3(uint32_t w);
    static uint8_t Byte2(uint32_t w);
    static uint8_t Byte1(uint32_t w);
    static uint8_t Byte0(uint32_t w);

    // Scale helpers
    static uint32_t U7ToU32(uint8_t v7);
    static uint32_t U14ToU32(uint16_t v14);

    static uint8_t U32ToU7(uint32_t v32);
    static uint16_t U32ToU14(uint32_t v32);

    // Builders
    static void PushMidi2ChannelVoice(std::vector<uint32_t>& out, const Midi2ChannelVoiceMsg& msg);

    static void PushMidi1ChannelVoice(std::vector<uint32_t>& out, const Midi1ChannelVoiceMsg& msg);

    // Convert core
    static bool ConvertMidi1ChannelVoiceToMidi2(const uint32_t* inWords, size_t inWordCount,
        std::vector<uint32_t>& out);

    static bool ConvertMidi2ChannelVoiceToMidi1(const uint32_t* inWords, size_t inWordCount,
        std::vector<uint32_t>& out);
    static bool ConvertMidi2ChannelVoiceToMidi1Inner(uint8_t statusNibble,
                                                    uint8_t data1,
                                                    uint32_t w1,
                                                    Midi1ChannelVoiceMsg& m,
                                                    std::vector<uint32_t>& out);
    static bool ConvertMidi1ChannelVoiceToMidi2Inner(uint8_t statusNibble,
                                                    uint8_t data1,
                                                    uint8_t data2,
                                                    Midi2ChannelVoiceMsg& m,
                                                    std::vector<uint32_t>& out);
};
#endif