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
} // namespace MIDI
} // namespace OHOS