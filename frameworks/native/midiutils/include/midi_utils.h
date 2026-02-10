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

#ifndef MIDI_UTILS_H
#define MIDI_UTILS_H

#include <array>
#include "midi_info.h"

namespace OHOS {
namespace MIDI {

const uint64_t MIDI_NS_PER_SECOND = 1000000000;

// ============ UMP SysEx7 (Type 3, 64-bit) packing ============
constexpr uint32_t MAX_PACKET_BYTES = 6;
constexpr uint32_t UMP_TYPE_3 = 0x3;
constexpr uint32_t UMP_MASK = 0xF;

// 0: complete, 1: start, 2: continue, 3: end
constexpr uint8_t SYSEX7_COMPLETE  = 0;
constexpr uint8_t SYSEX7_START     = 1;
constexpr uint8_t SYSEX7_CONTINUE  = 2;
constexpr uint8_t SYSEX7_END       = 3;

constexpr uint32_t SYSEX7_WORD0_TYPE_SHIFT      = 28;
constexpr uint32_t SYSEX7_WORD0_GROUP_SHIFT     = 24;
constexpr uint32_t SYSEX7_WORD0_STATUS_SHIFT    = 20;
constexpr uint32_t SYSEX7_WORD0_BYTES_NUM_SHIFT = 16;

constexpr uint32_t SYSEX7_WORD_COUNT = 2;
constexpr uint32_t BITS_PER_BYTE = 8;

constexpr uint32_t PACKETS_BATCH_NUM = 256;

constexpr int64_t WAIT_SLICE_NS = 2 * 1000 * 1000; // 2ms

constexpr auto MAX_TIMEOUT_MS = std::chrono::milliseconds(2000);

inline uint8_t GetSysexStatus(uint32_t pktIndex, uint32_t totalPkts)
{
    if (totalPkts == 1) {
        return SYSEX7_COMPLETE;
    }
    if (pktIndex == 0) {
        return SYSEX7_START;
    }
    if (pktIndex + 1 == totalPkts) {
        return SYSEX7_END;
    }
    return SYSEX7_CONTINUE;
}

void CloseFd(int fd);
std::string GetEncryptStr(const std::string &str);
std::string BytesToString(uint32_t value);
std::string DumpOneEvent(uint64_t ts, size_t len, const uint32_t *data);
std::string DumpMidiEvents(const std::vector<MidiEvent>& events);
std::string DumpMidiEvents(const std::vector<MidiEventInner>& events);
long StringToNum(const std::string &str);

std::array<uint32_t, SYSEX7_WORD_COUNT> PackSysEx7Ump64(uint8_t group, uint8_t status,
    const uint8_t* bytes, uint8_t nbytes);

class ClockTime {
public:
    static int64_t GetCurNano();
};

/**
 * @brief Represents Timestamp information, including the frame position information and high-resolution time source.
 */
class Timestamp {
public:
    Timestamp() : framePosition(0)
    {
        time.tv_sec = 0;
        time.tv_nsec = 0;
    }
    virtual ~Timestamp() = default;
    uint32_t framePosition;
    struct timespec time;

    /**
     * @brief Enumerates the time base of this <b>Timestamp</b>. Different timing methods are supported.
     *
     */
    enum Timestampbase {
        /** Monotonically increasing time, excluding the system sleep time */
        MONOTONIC = 0,
        /** Boot time, including the system sleep time */
        BOOTTIME = 1,
        /** Timebase enum size */
        BASESIZE = 2
    };
};

class UniqueFd {
public:
    UniqueFd() = default;
    explicit UniqueFd(int fd) : fd_(fd) {}
    ~UniqueFd();

    UniqueFd(const UniqueFd &) = delete;
    UniqueFd &operator=(const UniqueFd &) = delete;

    UniqueFd(UniqueFd &&other) noexcept;
    UniqueFd &operator=(UniqueFd &&other) noexcept;

    int Get() const { return fd_; }
    bool Valid() const { return fd_ >= 0; }
    void Reset(int fd = -1);

private:
    int fd_ = -1;
};
} // namespace MIDI
} // namespace OHOS
#endif