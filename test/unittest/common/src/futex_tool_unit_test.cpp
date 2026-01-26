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

#include <atomic>
#include <functional>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <vector>

#include "futex_tool.h"

using namespace OHOS;
using namespace MIDI;
using namespace testing;
using namespace testing::ext;

class FutexToolUnitTest : public testing::Test {
public:
    static void SetUpTestCase() {}
    static void TearDownTestCase() {}

    void SetUp() override
    {
        // Reset stubs to default before each test
        FutexTool::SetStubFunc(nullptr, nullptr);
        testFutex_ = IS_READY;
    }

    void TearDown() override
    {
        // Clean up stubs
        FutexTool::SetStubFunc(nullptr, nullptr);
    }

protected:
    std::atomic<uint32_t> testFutex_;
};

/**
 * @tc.name: FutexWait_InvalidParams_001
 * @tc.desc: Test FutexWait with null pointer
 * @tc.type: FUNC
 */
HWTEST_F(FutexToolUnitTest, FutexWait_InvalidParams_001, TestSize.Level0)
{
    auto pred = []() { return true; };
    // futexPtr is null
    FutexCode ret = FutexTool::FutexWait(nullptr, 1000, pred);
    EXPECT_EQ(ret, FUTEX_INVALID_PARAMS);
}

/**
 * @tc.name: FutexWait_InvalidParams_002
 * @tc.desc: Test FutexWait with invalid predicate (empty function)
 * @tc.type: FUNC
 */
HWTEST_F(FutexToolUnitTest, FutexWait_InvalidParams_002, TestSize.Level0)
{
    // pred is empty
    FutexCode ret = FutexTool::FutexWait(&testFutex_, 1000, nullptr);
    EXPECT_EQ(ret, FUTEX_INVALID_PARAMS);
}

/**
 * @tc.name: FutexWait_InvalidParams_003
 * @tc.desc: Test FutexWait when atomic value is corrupted (not READY/NOT_READY/PRE_EXIT)
 * @tc.type: FUNC
 */
HWTEST_F(FutexToolUnitTest, FutexWait_InvalidParams_003, TestSize.Level0)
{
    testFutex_ = 999; // Invalid value
    auto pred = []() { return false; };
    FutexCode ret = FutexTool::FutexWait(&testFutex_, 1000, pred);
    EXPECT_EQ(ret, FUTEX_INVALID_PARAMS);
}

/**
 * @tc.name: FutexWait_ImmediateSuccess_001
 * @tc.desc: Test FutexWait when predicate is already true
 * @tc.type: FUNC
 */
HWTEST_F(FutexToolUnitTest, FutexWait_ImmediateSuccess_001, TestSize.Level0)
{
    // Predicate returns true immediately, no syscall should happen
    auto pred = []() { return true; };

    // Inject mock to ensure syscall is NOT called
    bool syscallCalled = false;
    auto mockSysCall = [&syscallCalled](std::atomic<uint32_t> *, int, int, const struct timespec *) -> long {
        syscallCalled = true;
        return 0;
    };
    FutexTool::SetStubFunc(mockSysCall, nullptr);

    FutexCode ret = FutexTool::FutexWait(&testFutex_, 1000, pred);
    EXPECT_EQ(ret, FUTEX_SUCCESS);
    EXPECT_FALSE(syscallCalled);

    // State should change to IS_NOT_READY before checking pred (inside loop logic)
    // or remain IS_READY depending on specific implementation flow.
    // Based on code: it tries CAS to IS_NOT_READY then checks pred.
    EXPECT_EQ(testFutex_.load(), IS_NOT_READY);
}

/**
 * @tc.name: FutexWait_PreExit_001
 * @tc.desc: Test FutexWait when atomic state is IS_PRE_EXIT
 * @tc.type: FUNC
 */
HWTEST_F(FutexToolUnitTest, FutexWait_PreExit_001, TestSize.Level0)
{
    testFutex_ = IS_PRE_EXIT;
    auto pred = []() { return false; };

    FutexCode ret = FutexTool::FutexWait(&testFutex_, 1000, pred);
    EXPECT_EQ(ret, FUTEX_OPERATION_FAILED);
}

/**
 * @tc.name: FutexWait_SyscallTimeout_001
 * @tc.desc: Test FutexWait when syscall returns ETIMEDOUT
 * @tc.type: FUNC
 */
HWTEST_F(FutexToolUnitTest, FutexWait_SyscallTimeout_001, TestSize.Level0)
{
    auto pred = []() { return false; };

    // Mock syscall to return timeout
    auto mockSysCall = [](std::atomic<uint32_t> *, int, int, const struct timespec *) -> long {
        errno = ETIMEDOUT;
        return -1;
    };
    FutexTool::SetStubFunc(mockSysCall, nullptr);

    FutexCode ret = FutexTool::FutexWait(&testFutex_, 1000, pred);
    EXPECT_EQ(ret, FUTEX_TIMEOUT);
}

/**
 * @tc.name: FutexWait_CalculationTimeout_001
 * @tc.desc: Test FutexWait when RecalculateWaitTime detects timeout
 * @tc.type: FUNC
 */
HWTEST_F(FutexToolUnitTest, FutexWait_CalculationTimeout_001, TestSize.Level0)
{
    auto pred = []() { return false; };

    // Mock Time:
    // 1st call (start): 0
    // 2nd call (recalculate): 2000 (timeout is 1000, so 2000 > 1000 -> expired)
    int callCount = 0;
    auto mockTime = [&callCount]() -> int64_t {
        if (callCount++ == 0) {
            return 0;
        }
        return 2000;
    };

    // Mock Syscall: Return EAGAIN to force loop to continue and call RecalculateWaitTime
    auto mockSysCall = [](std::atomic<uint32_t> *, int, int, const struct timespec *) -> long {
        errno = EAGAIN;
        return -1;
    };

    FutexTool::SetStubFunc(mockSysCall, mockTime);

    // Timeout is 1000ns
    FutexCode ret = FutexTool::FutexWait(&testFutex_, 1000, pred);
    EXPECT_EQ(ret, FUTEX_TIMEOUT);
}

/**
 * @tc.name: FutexWait_SpuriousWakeup_Success_001
 * @tc.desc: Test FutexWait handling spurious wakeup (syscall returns 0 but pred is false initially)
 * @tc.type: FUNC
 */
HWTEST_F(FutexToolUnitTest, FutexWait_SpuriousWakeup_Success_001, TestSize.Level0)
{
    // Predicate: False first time, True second time
    int predCalls = 0;
    auto pred = [&predCalls]() {
        predCalls++;
        return predCalls > 1;
    };

    // Mock Syscall: Always success (0)
    auto mockSysCall = [](std::atomic<uint32_t> *, int, int, const struct timespec *) -> long { return 0; };
    FutexTool::SetStubFunc(mockSysCall, nullptr);

    FutexCode ret = FutexTool::FutexWait(&testFutex_, 5000000, pred);
    EXPECT_EQ(ret, FUTEX_SUCCESS);
    // Should have called pred at least twice (initial check + check after wakeup)
    EXPECT_GE(predCalls, 2);
}

/**
 * @tc.name: FutexWait_MaxTryCount_001
 * @tc.desc: Test FutexWait fails after hitting WAIT_TRY_COUNT limit
 * @tc.type: FUNC
 */
HWTEST_F(FutexToolUnitTest, FutexWait_MaxTryCount_001, TestSize.Level0)
{
    // Predicate always returns false
    auto pred = []() { return false; };

    // Mock Syscall: Always success (Spurious wakeup simulation)
    // This causes the loop to run until WAIT_TRY_COUNT
    auto mockSysCall = [](std::atomic<uint32_t> *, int, int, const struct timespec *) -> long { return 0; };

    // Mock Time: Always return 0 to prevent timeout logic from kicking in
    auto mockTime = []() -> int64_t { return 0; };

    FutexTool::SetStubFunc(mockSysCall, mockTime);

    FutexCode ret = FutexTool::FutexWait(&testFutex_, 1000, pred);
    EXPECT_EQ(ret, FUTEX_OPERATION_FAILED); // "too much spurious wake-up"
}

/**
 * @tc.name: FutexWake_InvalidParams_001
 * @tc.desc: Test FutexWake with null pointer
 * @tc.type: FUNC
 */
HWTEST_F(FutexToolUnitTest, FutexWake_InvalidParams_001, TestSize.Level0)
{
    FutexCode ret = FutexTool::FutexWake(nullptr);
    EXPECT_EQ(ret, FUTEX_INVALID_PARAMS);
}

/**
 * @tc.name: FutexWake_InvalidParams_002
 * @tc.desc: Test FutexWake with corrupted atomic value
 * @tc.type: FUNC
 */
HWTEST_F(FutexToolUnitTest, FutexWake_InvalidParams_002, TestSize.Level0)
{
    testFutex_ = 888;
    FutexCode ret = FutexTool::FutexWake(&testFutex_);
    EXPECT_EQ(ret, FUTEX_INVALID_PARAMS);
}

/**
 * @tc.name: FutexWake_PreExit_001
 * @tc.desc: Test FutexWake with IS_PRE_EXIT command
 * @tc.type: FUNC
 */
HWTEST_F(FutexToolUnitTest, FutexWake_PreExit_001, TestSize.Level0)
{
    bool syscallCalled = false;
    auto mockSysCall = [&syscallCalled](std::atomic<uint32_t> *uaddr, int op, int val,
                                        const struct timespec *) -> long {
        syscallCalled = true;
        EXPECT_EQ(op, FUTEX_WAKE);
        return 1; // Woke 1 process
    };
    FutexTool::SetStubFunc(mockSysCall, nullptr);

    FutexCode ret = FutexTool::FutexWake(&testFutex_, IS_PRE_EXIT);
    EXPECT_EQ(ret, FUTEX_SUCCESS);
    EXPECT_EQ(testFutex_.load(), IS_PRE_EXIT);
    EXPECT_TRUE(syscallCalled);
}

/**
 * @tc.name: FutexWake_Normal_001
 * @tc.desc: Test FutexWake when thread is waiting (IS_NOT_READY)
 * @tc.type: FUNC
 */
HWTEST_F(FutexToolUnitTest, FutexWake_Normal_001, TestSize.Level0)
{
    testFutex_ = IS_NOT_READY;

    bool syscallCalled = false;
    auto mockSysCall = [&syscallCalled](std::atomic<uint32_t> *, int op, int, const struct timespec *) -> long {
        syscallCalled = true;
        EXPECT_EQ(op, FUTEX_WAKE);
        return 1;
    };
    FutexTool::SetStubFunc(mockSysCall, nullptr);

    FutexCode ret = FutexTool::FutexWake(&testFutex_); // default wakeVal is IS_READY
    EXPECT_EQ(ret, FUTEX_SUCCESS);
    EXPECT_TRUE(syscallCalled);
    // Should transition back to IS_READY logic (via compare_exchange_strong in impl)
    // Note: Implementation stores IS_READY if it was IS_NOT_READY
    // Actually, implementation does: if (compare_exchange_strong(IS_NOT_READY, IS_READY)) -> syscall
    // So current value should be IS_READY.
    EXPECT_EQ(testFutex_.load(), IS_READY);
}

/**
 * @tc.name: FutexWake_NoWait_001
 * @tc.desc: Test FutexWake when no thread is waiting (IS_READY)
 * @tc.type: FUNC
 */
HWTEST_F(FutexToolUnitTest, FutexWake_NoWait_001, TestSize.Level0)
{
    testFutex_ = IS_READY;

    bool syscallCalled = false;
    auto mockSysCall = [&syscallCalled](std::atomic<uint32_t> *, int, int, const struct timespec *) -> long {
        syscallCalled = true;
        return 0;
    };
    FutexTool::SetStubFunc(mockSysCall, nullptr);

    FutexCode ret = FutexTool::FutexWake(&testFutex_);
    EXPECT_EQ(ret, FUTEX_SUCCESS);
    // Syscall should NOT be called because compare_exchange_strong fails (expecting IS_NOT_READY)
    EXPECT_FALSE(syscallCalled);
}

/**
 * @tc.name: FutexWake_SyscallFailed_001
 * @tc.desc: Test FutexWake when syscall fails
 * @tc.type: FUNC
 */
HWTEST_F(FutexToolUnitTest, FutexWake_SyscallFailed_001, TestSize.Level0)
{
    testFutex_ = IS_NOT_READY;

    auto mockSysCall = [](std::atomic<uint32_t> *, int, int, const struct timespec *) -> long {
        errno = EACCES; // Simulate some error
        return -1;
    };
    FutexTool::SetStubFunc(mockSysCall, nullptr);

    FutexCode ret = FutexTool::FutexWake(&testFutex_);
    EXPECT_EQ(ret, FUTEX_OPERATION_FAILED);
}

/**
 * @tc.name: FutexWait_NegativeTimeout_001
 * @tc.desc: Test FutexWait with 0 or negative timeout (Infinite wait behavior check)
 * @tc.type: FUNC
 */
HWTEST_F(FutexToolUnitTest, FutexWait_NegativeTimeout_001, TestSize.Level0)
{
    // Predicate becomes true after one check
    int checks = 0;
    auto pred = [&checks]() {
        checks++;
        return checks > 1;
    };

    // RecalculateWaitTime should return true immediately for timeout <= 0
    // Syscall should receive NULL timeout pointer
    bool nullTimeoutPtr = false;
    auto mockSysCall = [&nullTimeoutPtr](std::atomic<uint32_t> *, int, int, const struct timespec *timeout) -> long {
        if (timeout == nullptr) {
            nullTimeoutPtr = true;
        }
        return 0;
    };

    FutexTool::SetStubFunc(mockSysCall, nullptr);

    // Timeout = 0
    FutexCode ret = FutexTool::FutexWait(&testFutex_, 0, pred);
    EXPECT_EQ(ret, FUTEX_SUCCESS);
    EXPECT_TRUE(nullTimeoutPtr);
}