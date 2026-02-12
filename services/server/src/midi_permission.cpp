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

#include "midi_permission.h"
#include "midi_log.h"
#include "ipc_skeleton.h"
#include "accesstoken_kit.h"

namespace OHOS {
namespace MIDI {
static constexpr int PERMISSION_GRANTED = 0;
static constexpr const char *ACCESS_BLUETOOTH_PERMISSION = "ohos.permission.ACCESS_BLUETOOTH";

bool MidiPermissionManager::VerifyPermission(const std::string &permissionName)
{
    uint32_t callerToken = IPCSkeleton::GetCallingTokenID();
    int result = OHOS::Security::AccessToken::AccessTokenKit::VerifyAccessToken(callerToken, permissionName);
    MIDI_INFO_LOG("%{public}s VerifyAccessToken: %{public}d", __func__, result);
    return result == PERMISSION_GRANTED;
}

bool MidiPermissionManager::VerifyBluetoothPermission()
{
    return VerifyPermission(ACCESS_BLUETOOTH_PERMISSION);
}
} // namespace MIDI
} // namespace OHOS
