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

#ifndef OHOS_MIDI_LOG_H
#define OHOS_MIDI_LOG_H

#ifndef LOG_TAG
#define LOG_TAG "MidiFramework"
#endif

#include <stdio.h>
#include <inttypes.h>
#include "hilog/log.h"

#undef LOG_DOMAIN
#define LOG_DOMAIN 0xD002B8D
#ifndef OHOS_DEBUG
#define DECORATOR_HILOG(op, fmt, args...) \
    do {                                  \
        op(LOG_CORE, "[%{public}s]" fmt, __FUNCTION__, ##args);        \
    } while (0)
#else
#define DECORATOR_HILOG(op, fmt, args...)                                                \
    do {                                                                                 \
        op(LOG_CORE, "{%s()-%s:%d} " fmt, __FUNCTION__, __FILENAME__, __LINE__, ##args); \
    } while (0)
#endif

#define MIDI_DEBUG_LOG(fmt, ...) DECORATOR_HILOG(HILOG_DEBUG, fmt, ##__VA_ARGS__)
#define MIDI_ERR_LOG(fmt, ...) DECORATOR_HILOG(HILOG_ERROR, fmt, ##__VA_ARGS__)
#define MIDI_WARNING_LOG(fmt, ...) DECORATOR_HILOG(HILOG_WARN, fmt, ##__VA_ARGS__)
#define MIDI_INFO_LOG(fmt, ...) DECORATOR_HILOG(HILOG_INFO, fmt, ##__VA_ARGS__)
#define MIDI_FATAL_LOG(fmt, ...) DECORATOR_HILOG(HILOG_FATAL, fmt, ##__VA_ARGS__)

#define MIDI_OK 0
#define MIDI_INVALID_PARAM (-1)
#define MIDI_INIT_FAIL (-2)
#define MIDI_ERR (-3)
#define MIDI_PERMISSION_DENIED (-4)

#define CHECK_AND_RETURN_RET_LOG(cond, ret, fmt, ...)  \
    do {                                               \
        if (!(cond)) {                                 \
            MIDI_ERR_LOG(fmt, ##__VA_ARGS__);         \
            return ret;                                \
        }                                              \
    } while (0)

#define CHECK_AND_RETURN_LOG(cond, fmt, ...)           \
    do {                                               \
        if (!(cond)) {                                 \
            MIDI_ERR_LOG(fmt, ##__VA_ARGS__);         \
            return;                                    \
        }                                              \
    } while (0)

#define CHECK_AND_BREAK_LOG(cond, fmt, ...)            \
    if (1) {                                           \
        if (!(cond)) {                                 \
            MIDI_ERR_LOG(fmt, ##__VA_ARGS__);         \
            break;                                     \
        }                                              \
    } else void (0)

#define CHECK_AND_RETURN_RET(cond, ret, ...)           \
    do {                                               \
        if (!(cond)) {                                 \
            return ret;                                \
        }                                              \
    } while (0)

#define CHECK_AND_RETURN(cond, ...)                    \
    do {                                               \
        if (!(cond)) {                                 \
            return;                                    \
        }                                              \
    } while (0)

#define RETURN_RET_IF(cond, ret)                       \
    do {                                               \
        if ((cond)) {                                  \
            return ret;                                \
        }                                              \
    } while (0)

#define CHECK_AND_CONTINUE(cond)                       \
    if (1) {                                           \
        if (!(cond)) {                                 \
            continue;                                  \
        }                                              \
    } else void (0)

#define CHECK_AND_CONTINUE_LOG(cond, fmt, ...)         \
    if (1) {                                           \
        if (!(cond)) {                                 \
            MIDI_DEBUG_LOG(fmt, ##__VA_ARGS__);       \
            continue;                                  \
        }                                              \
    } else void (0)

#define JUDGE_AND_INFO_LOG(cond, fmt, ...)             \
    if (1) {                                           \
        if (cond) {                                    \
            MIDI_INFO_LOG(fmt, ##__VA_ARGS__);        \
        }                                              \
    } else void (0)

#define JUDGE_AND_ERR_LOG(cond, fmt, ...)              \
    if (1) {                                           \
        if (cond) {                                    \
            MIDI_ERR_LOG(fmt, ##__VA_ARGS__);         \
        }                                              \
    } else void (0)

#define JUDGE_AND_WARNING_LOG(cond, fmt, ...)              \
    if (1) {                                           \
        if (cond) {                                    \
            MIDI_WARNING_LOG(fmt, ##__VA_ARGS__);         \
        }                                              \
    } else void (0)

#ifndef OHOS_DEBUG
#define DECORATOR_PRERELEASE_HILOG(op, fmt, args...) \
    do {                                  \
        op(LOG_ONLY_PRERELEASE, "[%{public}s]" fmt, __FUNCTION__, ##args);        \
    } while (0)
#else
#define DECORATOR_PRERELEASE_HILOG(op, fmt, args...)                                                \
    do {                                                                                 \
        op(LOG_ONLY_PRERELEASE, "{%s()-%s:%d} " fmt, __FUNCTION__, __FILENAME__, __LINE__, ##args); \
    } while (0)
#endif

#define MIDI_PRERELEASE_LOGD(fmt, ...) DECORATOR_PRERELEASE_HILOG(HILOG_DEBUG, fmt, ##__VA_ARGS__)
#define MIDI_PRERELEASE_LOGE(fmt, ...) DECORATOR_PRERELEASE_HILOG(HILOG_ERROR, fmt, ##__VA_ARGS__)
#define MIDI_PRERELEASE_LOGW(fmt, ...) DECORATOR_PRERELEASE_HILOG(HILOG_WARN, fmt, ##__VA_ARGS__)
#define MIDI_PRERELEASE_LOGI(fmt, ...) DECORATOR_PRERELEASE_HILOG(HILOG_INFO, fmt, ##__VA_ARGS__)
#define MIDI_PRERELEASE_LOGF(fmt, ...) DECORATOR_PRERELEASE_HILOG(HILOG_FATAL, fmt, ##__VA_ARGS__)

#define CHECK_AND_RETURN_RET_PRELOG(cond, ret, fmt, ...)  \
    do {                                                  \
        if (!(cond)) {                                    \
            MIDI_PRERELEASE_LOGE(fmt, ##__VA_ARGS__);    \
            return ret;                                   \
        }                                                 \
    } while (0)

#define CHECK_AND_RETURN_PRELOG(cond, fmt, ...)           \
    do {                                                  \
        if (!(cond)) {                                    \
            MIDI_PRERELEASE_LOGE(fmt, ##__VA_ARGS__);    \
            return;                                       \
        }                                                 \
    } while (0)

#define CHECK_AND_BREAK_PRELOG(cond, fmt, ...)            \
    if (1) {                                              \
        if (!(cond)) {                                    \
            MIDI_PRERELEASE_LOGE(fmt, ##__VA_ARGS__);    \
            break;                                        \
        }                                                 \
    } else void (0)

#define CHECK_AND_CONTINUE_PRELOG(cond, fmt, ...)         \
    if (1) {                                              \
        if (!(cond)) {                                    \
            MIDI_PRERELEASE_LOGD(fmt, ##__VA_ARGS__);    \
            continue;                                     \
        }                                                 \
    } else void (0)
#endif // OHOS_MIDI_LOG_H
