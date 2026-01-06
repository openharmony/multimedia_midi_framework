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
#ifndef MIDI_SERVICE_CLIENT_H
#define MIDI_SERVICE_CLIENT_H

#include <memory>
#include "midi_service_interface.h"
#include "native_midi_base.h"
#include "midi_info.h"
#include "imidi_service.h"
#include "midi_callback_stub.h"
#include "iipc_midi_client_in_server.h"
#include "midi_shared_ring.h"
namespace OHOS {
namespace MIDI {
class MidiServiceClient : public MidiServiceInterface {
public:
    MidiServiceClient() = default;
    virtual ~MidiServiceClient() = default;
    OH_MidiStatusCode Init(sptr<MidiCallbackStub> callback,uint32_t &clientId) override;
    OH_MidiStatusCode GetDevices(std::vector<std::map<int32_t, std::string>> &deviceInfos) override;
    OH_MidiStatusCode OpenDevice(int64_t deviceId) override;
    OH_MidiStatusCode CloseDevice(int64_t deviceId) override;
    OH_MidiStatusCode GetDevicePorts(int64_t deviceId, std::vector<std::map<int32_t, std::string>> &portInfos) override;
    OH_MidiStatusCode OpenInputPort(std::shared_ptr<SharedMidiRing> &buffer, int64_t deviceId, uint32_t portIndex) override;
    OH_MidiStatusCode CloseInputPort(int64_t deviceId, uint32_t portIndex) override;
    OH_MidiStatusCode DestroyMidiClient() override;
private:
    sptr<IIpcMidiClientInServer> ipc_;
    sptr<MidiCallbackStub> callback_;
    std::mutex lock_;
};
} // namespace MIDI
} // namespace OHOS
#endif
