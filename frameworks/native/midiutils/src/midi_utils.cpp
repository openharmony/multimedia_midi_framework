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

#ifndef LOG_TAG
#define LOG_TAG "MidiUtils"
#endif

#include <cinttypes>
#include <ctime>
#include <sstream>
#include <ostream>
#include <climits>
#include <string>
#include <unistd.h>

#include "midi_log.h"
#include "midi_utils.h"

namespace OHOS {
namespace MIDI {

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

int64_t ClockTime::GetRealNano()
{
    int64_t result = -1; // -1 for bad result
    struct timespec time;
    clockid_t clockId = CLOCK_REALTIME;
    int ret = clock_gettime(clockId, &time);
    CHECK_AND_RETURN_RET_LOG(ret >= 0, result, "GetRealNanotime fail, result:%{public}d", ret);
    result = (time.tv_sec * MIDI_NS_PER_SECOND) + time.tv_nsec;
    return result;
}

int64_t ClockTime::GetBootNano()
{
    int64_t result = -1; // -1 for bad result
    struct timespec time;
    clockid_t clockId = CLOCK_BOOTTIME;
    int ret = clock_gettime(clockId, &time);
    CHECK_AND_RETURN_RET_LOG(ret >= 0, result, "GetBootNanotime fail, result:%{public}d", ret);
    result = (time.tv_sec * MIDI_NS_PER_SECOND) + time.tv_nsec;
    return result;
}

int32_t ClockTime::AbsoluteSleep(int64_t nanoTime)
{
    int32_t ret = -1; // -1 for bad result.
    CHECK_AND_RETURN_RET_LOG(nanoTime > 0, ret, "AbsoluteSleep invalid sleep time :%{public}" PRId64 " ns", nanoTime);
    struct timespec time;
    time.tv_sec = nanoTime / MIDI_NS_PER_SECOND;
    time.tv_nsec = nanoTime - (time.tv_sec * MIDI_NS_PER_SECOND); // Avoids % operation.

    clockid_t clockId = CLOCK_MONOTONIC;
    ret = clock_nanosleep(clockId, TIMER_ABSTIME, &time, nullptr);
    if (ret != 0) {
        MIDI_WARNING_LOG("AbsoluteSleep may failed, ret is :%{public}d", ret);
    }

    return ret;
}

std::string ClockTime::NanoTimeToString(int64_t nanoTime)
{
    struct tm *tm_info;
    char buffer[80];
    time_t time_seconds = nanoTime / MIDI_NS_PER_SECOND;

    tm_info = localtime(&time_seconds);
    if (tm_info == NULL) {
        MIDI_ERR_LOG("get localtime failed!");
        return "";
    }

    size_t res = strftime(buffer, sizeof(buffer), "%H:%M:%S", tm_info);
    CHECK_AND_RETURN_RET_LOG(res != 0, "", "strftime failed!");
    return std::string(buffer);
}

int32_t ClockTime::RelativeSleep(int64_t nanoTime)
{
    int32_t ret = -1; // -1 for bad result.
    CHECK_AND_RETURN_RET_LOG(nanoTime > 0, ret, "AbsoluteSleep invalid sleep time :%{public}" PRId64 " ns", nanoTime);
    struct timespec time;
    time.tv_sec = nanoTime / MIDI_NS_PER_SECOND;
    time.tv_nsec = nanoTime - (time.tv_sec * MIDI_NS_PER_SECOND); // Avoids % operation.

    clockid_t clockId = CLOCK_MONOTONIC;
    const int relativeFlag = 0; // flag of relative sleep.
    ret = clock_nanosleep(clockId, relativeFlag, &time, nullptr);
    if (ret != 0) {
        MIDI_WARNING_LOG("RelativeSleep may failed, ret is :%{public}d", ret);
    }

    return ret;
}

void ClockTime::GetAllTimeStamp(std::vector<uint64_t> &timestamp)
{
    timestamp.resize(Timestamp::Timestampbase::BASESIZE);
    int64_t tmpTime = GetCurNano();
    if (tmpTime > 0) {
        timestamp[Timestamp::Timestampbase::MONOTONIC] = static_cast<uint64_t>(tmpTime);
    }
    tmpTime = GetBootNano();
    if (tmpTime > 0) {
        timestamp[Timestamp::Timestampbase::BOOTTIME] = static_cast<uint64_t>(tmpTime);
    }
}

bool ClockTime::CheckTimeInterval(std::atomic<int64_t> &lastRecordTimestamp, const int64_t timeInterval)
{
    int64_t curTimestamp = GetCurNano();
    CHECK_AND_RETURN_RET(lastRecordTimestamp.load() + timeInterval < curTimestamp, false);
    lastRecordTimestamp.store(curTimestamp);
    return true;
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
} // namespace MIDI
} // namespace OHOS