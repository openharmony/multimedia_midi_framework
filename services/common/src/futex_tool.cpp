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
#ifndef LOG_TAG
#define LOG_TAG "FutexTool"
#endif

#include "futex_tool.h"

#include "linux/futex.h"
#include <cinttypes>
#include <ctime>
#include <sys/syscall.h>

#include "midi_log.h"
#include "native_midi_base.h"

namespace OHOS {
namespace MIDI {
namespace {
const int32_t WAIT_TRY_COUNT = 50;
const int64_t SEC_TO_NANOSEC = 1000000000;

// Default implementation calling the real syscall
static long g_realSysCall(std::atomic<uint32_t> *futexPtr, int op, int val, const struct timespec *timeout)
{
    // The syscall interface requires raw pointers and specific casts
    return syscall(__NR_futex, futexPtr, op, val, timeout, NULL, 0);
}

// Default implementation calling the real time utility
static int64_t g_realGetTime() { return ClockTime::GetCurNano(); }

// Global wrappers initialized with default real implementations
FutexTool::FutexSysCall g_sysCallFunc = g_realSysCall;
FutexTool::TimeGetter g_timeFunc = g_realGetTime;
} // namespace

// Exposed interface to inject mocks for Unit Testing
void FutexTool::SetStubFunc(const FutexSysCall &sysCall, const TimeGetter &timeCall)
{
    if (sysCall) {
        g_sysCallFunc = sysCall;
    } else {
        g_sysCallFunc = g_realSysCall;
    }

    if (timeCall) {
        g_timeFunc = timeCall;
    } else {
        g_timeFunc = g_realGetTime;
    }
}

// FUTEX_WAIT using relative timeout value.
void TimeoutToRelativeTime(int64_t timeout, struct timespec &realtime)
{
    int64_t timeoutNanoSec = timeout % SEC_TO_NANOSEC;
    int64_t timeoutSec = timeout / SEC_TO_NANOSEC;

    realtime.tv_nsec = timeoutNanoSec;
    realtime.tv_sec = timeoutSec;
}

// Helper: Validate input parameters
FutexCode CheckWaitParams(std::atomic<uint32_t> *futexPtr, const std::function<bool(void)> &pred)
{
    CHECK_AND_RETURN_RET_LOG(futexPtr != nullptr, FUTEX_INVALID_PARAMS, "futexPtr is null");
    CHECK_AND_RETURN_RET_LOG(pred, FUTEX_INVALID_PARAMS, "pred err");
    uint32_t current = futexPtr->load();
    if (current != IS_READY && current != IS_NOT_READY && current != IS_PRE_EXIT) {
        MIDI_ERR_LOG("failed: invalid param:%{public}u", current);
        return FUTEX_INVALID_PARAMS;
    }
    return FUTEX_SUCCESS;
}

// Helper: Calculate remaining wait time. Returns false if timeout occurred.
bool RecalculateWaitTime(int64_t timeout, int64_t timeIn, struct timespec &waitTime)
{
    if (timeout <= 0) {
        return true;
    }
    int64_t cost = g_timeFunc() - timeIn;
    if (cost >= timeout) {
        return false;
    }
    TimeoutToRelativeTime(timeout - cost, waitTime);
    return true;
}

// Helper: Execute the actual syscall
FutexCode ExecFutexWaitSyscall(std::atomic<uint32_t> *futexPtr, int64_t timeout, struct timespec *waitTimePtr)
{
    long res = g_sysCallFunc(futexPtr, FUTEX_WAIT, IS_NOT_READY, waitTimePtr);
    auto sysErr = errno;

    if ((res != 0) && (sysErr == ETIMEDOUT)) {
        MIDI_WARNING_LOG("wait:%{public}" PRId64 "ns timeout, result:%{public}" PRId64 ", sysErr[%{public}d]:%{public}s",
                         timeout, res, sysErr, strerror(sysErr));
        return FUTEX_TIMEOUT;
    }

    if ((res != 0) && (sysErr != EAGAIN)) {
        MIDI_WARNING_LOG("result:%{public}" PRId64 ", sysErr[%{public}d]:%{public}s", res, sysErr, strerror(sysErr));
    }
    // EAGAIN or Success are treated as continuation or success in caller
    return FUTEX_SUCCESS;
}

FutexCode FutexTool::FutexWait(std::atomic<uint32_t> *futexPtr, int64_t timeout, const std::function<bool(void)> &pred)
{
    FutexCode checkRet = CheckWaitParams(futexPtr, pred);
    if (checkRet != FUTEX_SUCCESS) {
        return checkRet;
    }

    int64_t timeIn = g_timeFunc();
    struct timespec waitTime;
    if (timeout > 0) {
        TimeoutToRelativeTime(timeout, waitTime);
    }

    int32_t tryCount = 0;
    while (tryCount < WAIT_TRY_COUNT) {
        uint32_t expect = IS_READY;
        if (!futexPtr->compare_exchange_strong(expect, IS_NOT_READY)) {
            if (expect == IS_PRE_EXIT) {
                MIDI_ERR_LOG("failed with invalid status:%{public}u", expect);
                return FUTEX_OPERATION_FAILED;
            }
        }
        if (pred()) {
            return FUTEX_SUCCESS;
        }
        if (!RecalculateWaitTime(timeout, timeIn, waitTime)) {
            return FUTEX_TIMEOUT;
        }

        FutexCode sysRet = ExecFutexWaitSyscall(futexPtr, timeout, (timeout <= 0 ? NULL : &waitTime));
        if (sysRet == FUTEX_TIMEOUT) {
            return FUTEX_TIMEOUT;
        }
        if (pred()) {
            return FUTEX_SUCCESS;
        }
        tryCount++;
    }

    MIDI_ERR_LOG("too much spurious wake-up");
    return FUTEX_OPERATION_FAILED;
}

FutexCode FutexTool::FutexWake(std::atomic<uint32_t> *futexPtr, uint32_t wakeVal)
{
    CHECK_AND_RETURN_RET_LOG(futexPtr != nullptr, FUTEX_INVALID_PARAMS, "futexPtr is null");
    uint32_t current = futexPtr->load();
    if (current != IS_READY && current != IS_NOT_READY && current != IS_PRE_EXIT) {
        MIDI_ERR_LOG("failed: invalid param:%{public}u", current);
        return FUTEX_INVALID_PARAMS;
    }
    if (wakeVal == IS_PRE_EXIT) {
        futexPtr->store(IS_PRE_EXIT);
        g_sysCallFunc(futexPtr, FUTEX_WAKE, INT_MAX, NULL);
        return FUTEX_SUCCESS;
    }
    uint32_t expect = IS_NOT_READY;
    if (futexPtr->compare_exchange_strong(expect, IS_READY)) {
        long res = g_sysCallFunc(futexPtr, FUTEX_WAKE, INT_MAX, NULL);
        if (res < 0) {
            MIDI_ERR_LOG("failed:%{public}" PRId64 ", errno[%{public}d]:%{public}s", res, errno, strerror(errno));
            return FUTEX_OPERATION_FAILED;
        }
    }
    return FUTEX_SUCCESS;
}
} // namespace MIDI
} // namespace OHOS