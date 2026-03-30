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

#ifndef MIDI_SERVER_DUMP_H
#define MIDI_SERVER_DUMP_H

#include <string>
#include <map>
#include <queue>
#include <vector>
#include "nocopyable.h"

namespace OHOS {
namespace MIDI {

class MidiServiceController;

class MidiServerDump {
public:
    DISALLOW_COPY_AND_MOVE(MidiServerDump);

    MidiServerDump();
    ~MidiServerDump() = default;

    void HandleDump(std::string &dumpString, std::queue<std::u16string> &argQue);

private:
    void InitDumpFuncMap();

    // Dump methods for each module
    void HelpInfoDump(std::string &dumpString);
    void AllInfoDump(std::string &dumpString);
    void DeviceListDump(std::string &dumpString);
    void ClientInfoDump(std::string &dumpString);
    void PortMappingDump(std::string &dumpString);
    void StatisticsDump(std::string &dumpString);

    // Helper methods
    void DumpServiceStatus(std::string &dumpString);
    std::string DeviceTypeToString(int type) const;
    std::string ProtocolToString(int protocol) const;
    std::string PortDirectionToString(int direction) const;

    using DumpFunc = void(MidiServerDump::*)(std::string &);
    std::map<std::u16string, DumpFunc> dumpFuncMap_;
};

} // namespace MIDI
} // namespace OHOS

#endif // MIDI_SERVER_DUMP_H
