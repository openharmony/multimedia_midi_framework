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
#define LOG_TAG "MidiListenerCallback"
#endif

#include "midi_log.h"
#include "midi_listener_callback.h"
namespace OHOS {
namespace MIDI {
MidiListenerCallback::MidiListenerCallback(const sptr<IMidiCallback> &listener) : callback_(listener) {}

MidiListenerCallback::~MidiListenerCallback() {}

void MidiListenerCallback::NotifyDeviceChange(DeviceChangeType change, std::map<int32_t, std::string> deviceInfo)
{
    CHECK_AND_RETURN_LOG(callback_, "callback_ is nullptr");
    callback_->NotifyDeviceChange(change, deviceInfo);
}

void MidiListenerCallback::NotifyError(int32_t code)
{
    CHECK_AND_RETURN_LOG(callback_, "callback_ is nullptr");
    callback_->NotifyError(code);
}
} // namespace MIDI
} // namespace OHOS