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

#ifndef LOG_TAG
#define LOG_TAG "MidiServiceController"
#endif

#include "midi_service_controller.h"
#include "midi_service_death_recipent.h"
#include "iservice_registry.h"
#include "system_ability_definition.h"
#include "midi_info.h"
#include "midi_log.h"

namespace OHOS {
namespace MIDI {
std::atomic<uint32_t> MidiServiceController::currentClientId_ = 0;
static constexpr uint32_t MAX_CLIENTID = 0xFFFFFFFF;
DeviceClientContext::~DeviceClientContext()
{
    MIDI_INFO_LOG("~DeviceClientContext");
    clients.clear();
    inputDeviceconnections_.clear();
}

MidiServiceController::MidiServiceController() : deviceManager_() {}

MidiServiceController::~MidiServiceController() { clients_.clear(); }

MidiServiceController *MidiServiceController::GetInstance()
{
    static MidiServiceController instance;
    return &instance;
}

void MidiServiceController::Init()
{
    deviceManager_.Init();
}

int32_t MidiServiceController::CreateClientInServer(std::shared_ptr<MidiServiceCallback> callback,
                                                    sptr<IRemoteObject> &client, uint32_t &clientId)
{
    std::lock_guard lock(lock_);
    if (currentClientId_ >= MAX_CLIENTID) {
        currentClientId_ = 0;
    }

    clientId = ++currentClientId_; // todo 查看sessionId
    sptr<MidiClientInServer> midiClient = new (std::nothrow) MidiClientInServer(clientId, callback);
    CHECK_AND_RETURN_RET_LOG(midiClient != nullptr, MIDI_STATUS_UNKNOWN_ERROR, "midiClient nullptr");
    client = midiClient->AsObject();
    CHECK_AND_RETURN_RET_LOG(client != nullptr, MIDI_STATUS_UNKNOWN_ERROR, "midiClient->AsObject nullptr");
    sptr<MidiServiceDeathRecipient> deathRecipient_ = new (std::nothrow) MidiServiceDeathRecipient(clientId);
    CHECK_AND_RETURN_RET_LOG(deathRecipient_ != nullptr, MIDI_STATUS_UNKNOWN_ERROR, "deathRecipient_ nullptr");
    deathRecipient_->SetNotifyCb([this](uint32_t clientId) { this->DestroyMidiClient(clientId); });
    client->AddDeathRecipient(deathRecipient_);
    clients_.emplace(clientId, std::move(midiClient));
    MIDI_INFO_LOG("Create MIDI client success, clientId: %{public}u", clientId);
    return MIDI_STATUS_OK;
}

std::vector<std::map<int32_t, std::string>> MidiServiceController::GetDevices()
{
    std::vector<std::map<int32_t, std::string>> ret;
    auto result = deviceManager_.GetDevices();
    for (const auto &d : result) {
        std::map<int32_t, std::string> deviceInfo;
        deviceInfo[DEVICE_ID] = std::to_string(d.deviceId);
        deviceInfo[DEVICE_TYPE] = std::to_string(d.deviceType);
        deviceInfo[MIDI_PROTOCOL] = std::to_string(d.transportProtocol);
        deviceInfo[PRODUCT_NAME] = d.productName;
        deviceInfo[VENDOR_NAME] = d.vendorName;
        ret.push_back(std::move(deviceInfo));
    }
    return ret;
}

std::vector<std::map<int32_t, std::string>> MidiServiceController::GetDevicePorts(int64_t deviceId)
{
    std::vector<std::map<int32_t, std::string>> ret;
    auto result = deviceManager_.GetDevicePorts(deviceId);
    for (const auto &p : result) {
        std::map<int32_t, std::string> portInfo;
        portInfo[PORT_INDEX] = std::to_string(p.portId);
        portInfo[DIRECTION] = std::to_string(p.direction);
        portInfo[PORT_NAME] = p.name;
        ret.push_back(std::move(portInfo));
    }
    return ret;
}

int32_t MidiServiceController::OpenDevice(uint32_t clientId, int64_t deviceId)
{
    std::lock_guard lock(lock_);
    CHECK_AND_RETURN_RET_LOG(clients_.find(clientId) != clients_.end(), MIDI_STATUS_INVALID_CLIENT,
                             "Client not found: %{public}u", clientId);
    auto it = deviceClientContexts_.find(deviceId);
    if (it != deviceClientContexts_.end()) {
        CHECK_AND_RETURN_RET_LOG(
            it->second->clients.find(clientId) == it->second->clients.end(), MIDI_STATUS_DEVICE_ALREADY_OPEN,
            "Device already opened by client: deviceId=%{public}" PRId64 ", clientId=%{public}u", deviceId, clientId);
        it->second->clients.insert(clientId);
        MIDI_INFO_LOG("Client added to existing device: deviceId=%{public}" PRId64 ", clientId=%{public}u", deviceId,
                      clientId);
        return MIDI_STATUS_OK;
    }
    CHECK_AND_RETURN_RET_LOG(deviceManager_.OpenDevice(deviceId) == MIDI_STATUS_OK, MIDI_STATUS_UNKNOWN_ERROR,
                             "Open device failed: deviceId=%{public}" PRId64, deviceId);
    std::unordered_set<int32_t> clients = {static_cast<int32_t>(clientId)};
    auto context =  std::make_shared<DeviceClientContext>(deviceId, std::move(clients));
    deviceClientContexts_.emplace(deviceId, std::move(context));
    MIDI_INFO_LOG("Device opened successfully: deviceId=%{public}" PRId64 ", clientId=%{public}u", deviceId, clientId);
    return MIDI_STATUS_OK;
}

int32_t MidiServiceController::OpenInputPort(uint32_t clientId, std::shared_ptr<SharedMidiRing> &buffer,
                                             int64_t deviceId, uint32_t portIndex)
{
    MIDI_INFO_LOG("clientId: %{public}u, deviceId: %{public}" PRId64 " portIndex: %{public}u", clientId, deviceId,
                  portIndex);
    std::lock_guard lock(lock_);
    CHECK_AND_RETURN_RET_LOG(clients_.find(clientId) != clients_.end(), MIDI_STATUS_INVALID_CLIENT,
        "Client not found: %{public}u", clientId);
    auto it = deviceClientContexts_.find(deviceId);
    CHECK_AND_RETURN_RET_LOG(it != deviceClientContexts_.end(), MIDI_STATUS_INVALID_DEVICE_HANDLE,
                             "device %{public}" PRId64 "not opened", deviceId);
    CHECK_AND_RETURN_RET_LOG(it->second->clients.find(clientId) != it->second->clients.end(), MIDI_STATUS_UNKNOWN_ERROR,
                             "client %{public}u doesn't open device %{public}" PRId64 "", clientId, deviceId);

    auto &inputPortConnections = it->second->inputDeviceconnections_;
    auto inputPort = inputPortConnections.find(portIndex);
    if (inputPort != inputPortConnections.end()) {
        inputPort->second->AddClientConnection(clientId, deviceId, buffer); // todo 需要判断client是否有打开此端口。
        MIDI_INFO_LOG("InputPort already opened");
        return MIDI_STATUS_OK;
    }
    std::shared_ptr<DeviceConnectionForInput> inputConnection = nullptr;
    auto ret = deviceManager_.OpenInputPort(inputConnection, deviceId, portIndex);
    CHECK_AND_RETURN_RET_LOG(ret == MIDI_STATUS_OK, ret, "open input port fail!");

    if (inputConnection) {
        inputConnection->AddClientConnection(clientId, deviceId, buffer);
    }
    inputPortConnections.emplace(portIndex, std::move(inputConnection));
    MIDI_INFO_LOG("OpenInputPort Success");
    return MIDI_STATUS_OK;
}

int32_t MidiServiceController::CloseInputPort(uint32_t clientId, int64_t deviceId, uint32_t portIndex)
{
    MIDI_INFO_LOG("clientId: %{public}u, deviceId: %{public}" PRId64 " portIndex: %{public}u", clientId, deviceId,
                  portIndex);
    std::lock_guard lock(lock_);
    CHECK_AND_RETURN_RET_LOG(clients_.find(clientId) != clients_.end(), MIDI_STATUS_INVALID_CLIENT,
        "Client not found: %{public}u", clientId);
    return CloseInputPortInner(clientId, deviceId, portIndex);
}

int32_t MidiServiceController::CloseInputPortInner(uint32_t clientId, int64_t deviceId, uint32_t portIndex)
{
    auto it = deviceClientContexts_.find(deviceId);
    CHECK_AND_RETURN_RET_LOG(it != deviceClientContexts_.end(), MIDI_STATUS_INVALID_DEVICE_HANDLE,
                             "device %{public}" PRId64 "not opened", deviceId);
    CHECK_AND_RETURN_RET_LOG(it->second->clients.find(clientId) != it->second->clients.end(),
                             MIDI_STATUS_GENERIC_INVALID_ARGUMENT, "client %{public}u doesn't open device %{public}" PRId64,
                             clientId, deviceId);
    auto &inputPortConnections = it->second->inputDeviceconnections_;
    auto inputPort = inputPortConnections.find(portIndex);
    if (inputPort != inputPortConnections.end()) {
        inputPort->second->RemoveClientConnection(clientId);
        if (inputPort->second->IsEmptyClientConections()) {
            auto ret = deviceManager_.CloseInputPort(deviceId, portIndex);
            CHECK_AND_RETURN_RET_LOG(ret == MIDI_STATUS_OK, ret, "close input port fail!");
            inputPortConnections.erase(inputPort);
        }
    }
    return MIDI_STATUS_OK;
}

int32_t MidiServiceController::CloseDevice(uint32_t clientId, int64_t deviceId)
{
    std::lock_guard lock(lock_);
    CHECK_AND_RETURN_RET_LOG(clients_.find(clientId) != clients_.end(), MIDI_STATUS_INVALID_CLIENT,
        "Client not found: %{public}u", clientId);
    auto it = deviceClientContexts_.find(deviceId);
    CHECK_AND_RETURN_RET_LOG(it != deviceClientContexts_.end(), MIDI_STATUS_INVALID_DEVICE_HANDLE,
                             "Device not found: deviceId=%{public}" PRId64, deviceId);

    auto &clients = it->second->clients;
    auto clientIt = clients.find(clientId);
    
    CHECK_AND_RETURN_RET_LOG(clientIt != clients.end(), MIDI_STATUS_INVALID_DEVICE_HANDLE,
                             "Client not associated with device: deviceId=%{public}" PRId64 ", clientId=%{public}u", 
                             deviceId, clientId);

    ClosePortforDevice(clientId, deviceId, it->second);
    clients.erase(clientIt);
    MIDI_INFO_LOG("Client removed from device: deviceId=%{public}" PRId64 ", clientId=%{public}u", deviceId, clientId);
    CHECK_AND_RETURN_RET(clients.empty(), MIDI_STATUS_OK);
    deviceClientContexts_.erase(it);
    CHECK_AND_RETURN_RET_LOG(deviceManager_.CloseDevice(deviceId) == MIDI_STATUS_OK, MIDI_STATUS_UNKNOWN_ERROR,
                             "Close device failed: deviceId=%{public}" PRId64, deviceId);
    MIDI_INFO_LOG("Device closed: deviceId=%{public}" PRId64, deviceId);
    return MIDI_STATUS_OK;
}

void MidiServiceController::ClosePortforDevice(uint32_t clientId, int64_t deviceId,
                                               std::shared_ptr<DeviceClientContext> deviceClientContext)
{
    std::vector<uint32_t> portIndexs;
    for (auto const &inputPort : deviceClientContext->inputDeviceconnections_) {
        portIndexs.push_back(inputPort.first);
    }
    for (auto portIndex : portIndexs) {
        CloseInputPortInner(clientId, deviceId, portIndex);
    }
}

int32_t MidiServiceController::DestroyMidiClient(uint32_t clientId)
{
    MIDI_INFO_LOG("DestroyMidiClient: %{public}u enter", clientId);
    std::lock_guard lock(lock_);
    auto it = clients_.find(clientId);
    CHECK_AND_RETURN_RET_LOG(it != clients_.end(), MIDI_STATUS_INVALID_CLIENT,
                             "Client not found for destruction: %{public}u", clientId);

    for (auto deviceIt = deviceClientContexts_.begin(); deviceIt != deviceClientContexts_.end();) {
        auto &clients = deviceIt->second->clients;
        if (clients.find(clientId) != clients.end()) {
            int64_t deviceId = deviceIt->first;
            // todo 关闭打开的端口
            ClosePortforDevice(clientId, deviceId, deviceIt->second);
            clients.erase(clientId);
            if (clients.empty()) {
                deviceManager_.CloseDevice(deviceId);
                deviceIt = deviceClientContexts_.erase(deviceIt);
                continue;
            }
        }
        ++deviceIt;
    }
    clients_.erase(it);
    MIDI_INFO_LOG("Client destroyed: %{public}u", clientId);
    CHECK_AND_RETURN_RET(clients_.empty(), MIDI_STATUS_OK);
    auto samgr = SystemAbilityManagerClient::GetInstance().GetSystemAbilityManager();
    CHECK_AND_RETURN_RET_LOG(samgr != nullptr, MIDI_STATUS_GENERIC_IPC_FAILURE, "Get samgr failed.");
    MIDI_INFO_LOG("UnloadSystemAbility midi_server");
    samgr->UnloadSystemAbility(MIDI_SERVICE_ID);
    return MIDI_STATUS_OK;
}

void MidiServiceController::NotifyDeviceChange(DeviceChangeType change, DeviceInformation device)
{
    if (change == REMOVED) {
        std::lock_guard lock(lock_);
        MIDI_INFO_LOG("Device removed: deviceId=%{public}" PRId64, device.deviceId);
        auto it = deviceClientContexts_.find(device.deviceId);
        if (it != deviceClientContexts_.end()) {
            deviceClientContexts_.erase(it);
        }
    }
    std::map<int32_t, std::string> deviceInfo;
    deviceInfo[DEVICE_ID] = std::to_string(device.deviceId);
    deviceInfo[DEVICE_TYPE] = std::to_string(device.deviceType);
    deviceInfo[MIDI_PROTOCOL] = std::to_string(device.transportProtocol);
    deviceInfo[PRODUCT_NAME] = device.productName;
    deviceInfo[VENDOR_NAME] = device.vendorName;
    for (auto it : clients_) {
        CHECK_AND_CONTINUE(it.second != nullptr);
        it.second->NotifyDeviceChange(change, deviceInfo);
    }
}

void MidiServiceController::NotifyError(int32_t code)
{
    for (auto it : clients_) {
        CHECK_AND_CONTINUE(it.second != nullptr);
        it.second->NotifyError(code);
    }
}
} // namespace MIDI
} // namespace OHOS