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
#ifndef FUTEX_TOOL_H
#define FUTEX_TOOL_H

#include <atomic>
#include <unistd.h>
#include <functional>
#include <ctime>

#include "midi_utils.h"

namespace OHOS {
namespace MIDI {
namespace {
const uint32_t IS_READY = 0;
const uint32_t IS_NOT_READY = 1;
const uint32_t IS_PRE_EXIT = 2;
}  // namespace
enum FutexCode : int32_t {
    FUTEX_SUCCESS = 0,
    FUTEX_TIMEOUT,
    FUTEX_INVALID_PARAMS,
    FUTEX_OPERATION_FAILED,
    FUTEX_PRE_EXIT,
};

class FutexTool {
public:
    /**
     * FutexWait will first try change futexPtr from IS_READY to IS_NOT_READY, then acomicly wait on IS_NOT_READY.
     * After Waked up, will check futexPtr == IS_NOT_READY
     */
    static FutexCode FutexWait(std::atomic<uint32_t> *futexPtr, int64_t timeout, const std::function<bool(void)> &pred);
    static FutexCode FutexWake(std::atomic<uint32_t> *futexPtr, uint32_t wakeVal = IS_READY);

    // ===================================================================================
    // Unit Test Injection Interface
    // These definitions allow UT to mock the syscall and time function.
    // ===================================================================================
    using FutexSysCall = std::function<long(std::atomic<uint32_t> *, int, int, const struct timespec *)>;
    using TimeGetter = std::function<int64_t(void)>;

    /**
     * SetStubFunc
     * Used ONLY for Unit Testing to inject mock behaviors.
     */
    static void SetStubFunc(const FutexSysCall &sysCall, const TimeGetter &timeCall);
};
}  // namespace MIDI
}  // namespace OHOS
#endif  // FUTEX_TOOL_H