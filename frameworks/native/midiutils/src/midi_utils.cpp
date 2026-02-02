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
#define LOG_TAG "MidiUtils"
#endif

#include <cinttypes>
#include <climits>
#include <ctime>
#include <ostream>
#include <sstream>
#include <string>
#include <unistd.h>


#include "midi_log.h"
#include "midi_utils.h"

namespace OHOS {
namespace MIDI {
namespace {
    constexpr size_t FIRST_CHAR = 1;
    constexpr size_t MIN_LEN = 8;
    constexpr size_t HEAD_STR_LEN = 2;
    constexpr size_t TAIL_STR_LEN = 5;
} // namespace

int64_t ClockTime::GetCurNano()
{
    int64_t result = -1; // -1 for bad result.
    struct timespec time;
    clockid_t clockId = CLOCK_MONOTONIC;
    int ret = clock_gettime(clockId, &time);
    CHECK_AND_RETURN_RET_LOG(ret >= 0, result, "GetCurNanoTime fail, result:%{public}d", ret);
    result = (time.tv_sec * MIDI_NS_PER_SECOND) + time.tv_nsec;
    return result;
}

// utils //
void CloseFd(int fd)
{
    // log stdin, stdout, stderr.
    if (fd == STDIN_FILENO || fd == STDOUT_FILENO || fd == STDERR_FILENO) {
        MIDI_WARNING_LOG("special fd: %{public}d will be closed", fd);
    }
    int tmpFd = fd;
    close(fd);
    MIDI_DEBUG_LOG("fd: %{public}d closed successfuly!", tmpFd);
}

std::string GetEncryptStr(const std::string &src)
{
    if (src.empty()) {
        return std::string("");
    }

    size_t strLen = src.length();
    std::string dst;

    if (strLen < MIN_LEN) {
        // src: abcdef
        // dst: *bcdef
        dst = '*' + src.substr(FIRST_CHAR, strLen - FIRST_CHAR);
    } else {
        // src: 00:00:00:00:00:00
        // dst: 00**********00:00
        dst = src.substr(0, HEAD_STR_LEN);
        std::string tempStr(strLen - HEAD_STR_LEN - TAIL_STR_LEN, '*');
        dst += tempStr;
        dst += src.substr(strLen - TAIL_STR_LEN, TAIL_STR_LEN);
    }

    return dst;
}

// ====== UniqueFd ======
UniqueFd::~UniqueFd()
{
    Reset();
}

UniqueFd::UniqueFd(UniqueFd &&other) noexcept
{
    fd_ = other.fd_;
    other.fd_ = -1;
}

UniqueFd &UniqueFd::operator=(UniqueFd &&other) noexcept
{
    if (this != &other) {
        Reset();
        fd_ = other.fd_;
        other.fd_ = -1;
    }
    return *this;
}

void UniqueFd::Reset(int fd)
{
    if (fd_ >= 0) {
        ::close(fd_);
    }
    fd_ = fd;
}
} // namespace MIDI
} // namespace OHOS