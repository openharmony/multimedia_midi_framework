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
#define LOG_TAG "MidiServer"
#endif

#include "midi_server.h"
#include "iservice_registry.h"
#include "system_ability_definition.h"
#include "midi_log.h"
#include <unistd.h>
#include <queue>
namespace OHOS {
namespace MIDI {
REGISTER_SYSTEM_ABILITY_BY_ID(MidiServer, MIDI_SERVICE_ID, false)

MidiServer::MidiServer(int32_t systemAbilityId, bool runOnCreate)
    : SystemAbility(systemAbilityId, runOnCreate), dumpHelper_(std::make_unique<MidiServerDump>()) {}

void MidiServer::OnStart()
{
    controller_ = MidiServiceController::GetInstance();
    CHECK_AND_RETURN_LOG(controller_ != nullptr,
        "Failed to get MidiServiceController instance");
    controller_->Init();
    auto result = Publish(this);
    CHECK_AND_RETURN_LOG(result,
        "Failed to publish MIDI service to SAMgr");
    MIDI_INFO_LOG("MIDI service started successfully");
}

void MidiServer::OnStop()
{
    MIDI_INFO_LOG("MIDI service stopping");
    controller_ = nullptr;
}

void MidiServer::OnDump()
{
    MIDI_INFO_LOG("MIDI service OnDump");
}

int32_t MidiServer::Dump(int32_t fd, const std::vector<std::u16string> &args)
{
    MIDI_INFO_LOG("MIDI service Dump invoked");
    if (fd < 0) {
        MIDI_ERR_LOG("Invalid fd: %{public}d", fd);
        return OH_MIDI_STATUS_INVALID_PARAM;
    }

    std::string dumpString;
    std::queue<std::u16string> argQue;
    for (const auto &arg : args) {
        argQue.push(arg);
    }

    if (dumpHelper_) {
        dumpHelper_->HandleDump(dumpString, argQue);
    } else {
        dumpString = "MIDI Server Dump helper not initialized\n";
    }

    return write(fd, dumpString.c_str(), dumpString.size());
}

int32_t MidiServer::CreateMidiInServer(const sptr<IRemoteObject> &object, sptr<IRemoteObject> &client,
    uint32_t &clientId)
{
    CHECK_AND_RETURN_RET_LOG(controller_, OH_MIDI_STATUS_SYSTEM_ERROR, "controller_ is nullptr");
    return controller_->CreateMidiInServer(object, client, clientId);
}

} // namespace MIDI
} // namespace OHOS