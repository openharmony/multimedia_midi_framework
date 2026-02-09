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
#define LOG_TAG "MidiServiceController"
#endif

#include "midi_service_controller.h"
#include "midi_service_death_recipent.h"
#include "iservice_registry.h"
#include "system_ability_definition.h"
#include "midi_info.h"
#include "midi_log.h"
#include "midi_utils.h"
#include "imidi_device_open_callback.h"
#include "midi_listener_callback.h"
#include "midi_permission.h"
#include "ipc_skeleton.h"
#include <chrono>

namespace OHOS {
namespace MIDI {
namespace {
constexpr uint32_t MAX_CLIENTID = 0xFFFFFFFF;
constexpr uint32_t UNLOAD_DELAY_DEFAULT_TIME_IN_MS = 60 * 1000;
}
std::atomic<uint32_t> MidiServiceController::currentClientId_ = 0;
static  std::map<int32_t, std::string> ConvertDeviceInfo(const DeviceInformation &device)
{
    std::map<int32_t, std::string> deviceInfo;

    // Convert numeric IDs to strings
    deviceInfo[DEVICE_ID] = std::to_string(device.deviceId);
    deviceInfo[DEVICE_TYPE] = std::to_string(device.deviceType);
    deviceInfo[MIDI_PROTOCOL] = std::to_string(device.transportProtocol);

    // Direct string assignments
    deviceInfo[ADDRESS] = device.address;
    deviceInfo[PRODUCT_NAME] = device.productName;
    deviceInfo[VENDOR_NAME] = device.vendorName;

    return deviceInfo;
}
DeviceClientContext::~DeviceClientContext()
{
    MIDI_INFO_LOG("~DeviceClientContext");
    inputDeviceconnections_.clear();
}

MidiServiceController::MidiServiceController()
    : unloadDelayTime_(UNLOAD_DELAY_DEFAULT_TIME_IN_MS)  // Default: 60 seconds (production)
{
    deviceManager_ = std::make_shared<MidiDeviceManager>();
}

MidiServiceController::~MidiServiceController()
{
    CancelUnloadTask();
    if (unloadThread_.joinable()) {
        unloadThread_.join();
    }
    clients_.clear();
}

void MidiServiceController::Init()
{
    deviceManager_->Init();
}

std::shared_ptr<MidiServiceController> MidiServiceController::GetInstance()
{
    static std::shared_ptr<MidiServiceController> instance = std::make_shared<MidiServiceController>();
    return instance;
}


void MidiServiceController::CancelUnloadTask()
{
    bool expected = true;
    if (isUnloadPending_.compare_exchange_strong(expected, false)) {
        {
            std::lock_guard<std::mutex> lk(unloadMutex_);
        }
        unloadCv_.notify_all();
        MIDI_INFO_LOG("Pending unload task cancelled.");
    }
}

void MidiServiceController::ScheduleUnloadTask()
{
    if (isUnloadPending_) {
        return;
    }
    isUnloadPending_ = true;
    if (unloadThread_.joinable()) {
        unloadThread_.join();
    }

    unloadThread_ = std::thread([this]() {
        MIDI_INFO_LOG("Unload timer started. Waiting for %{public}" PRId64 " ms...", unloadDelayTime_);
        std::unique_lock<std::mutex> lk(unloadMutex_);
        if (unloadCv_.wait_for(lk, std::chrono::milliseconds(unloadDelayTime_)) == std::cv_status::timeout) {
            CHECK_AND_RETURN(isUnloadPending_);
            MIDI_INFO_LOG("Unload timer triggered. Unloading System Ability.");
            auto samgr = SystemAbilityManagerClient::GetInstance().GetSystemAbilityManager();
            if (samgr != nullptr) {
                samgr->UnloadSystemAbility(MIDI_SERVICE_ID);
            } else {
                MIDI_ERR_LOG("Get samgr failed.");
            }
            isUnloadPending_ = false;
        } else {
            MIDI_INFO_LOG("Unload timer thread woke up early (Cancelled).");
        }
    });
}

int32_t MidiServiceController::CreateMidiInServer(const sptr<IRemoteObject> &object,
    sptr<IRemoteObject> &client, uint32_t &clientId)
{
    CHECK_AND_RETURN_RET_LOG(object, MIDI_STATUS_UNKNOWN_ERROR, "object is nullptr");
    sptr<IMidiCallback> listener = iface_cast<IMidiCallback>(object);
    CHECK_AND_RETURN_RET_LOG(listener, MIDI_STATUS_UNKNOWN_ERROR, "listener is nullptr");
    std::shared_ptr<MidiListenerCallback> callback = std::make_shared<MidiListenerCallback>(listener);
    CHECK_AND_RETURN_RET_LOG(callback, MIDI_STATUS_UNKNOWN_ERROR, "callback is nullptr");

    // Get calling application UID
    uint32_t callingUid = IPCSkeleton::GetCallingUid();
    MIDI_INFO_LOG("CreateMidiInServer called from UID: %{public}u", callingUid);

    std::lock_guard lock(lock_);
    CancelUnloadTask();

    // Check client count limit (overall system)
    if (clients_.size() >= MAX_CLIENTS) {
        MIDI_ERR_LOG("Maximum number of clients reached: %{public}u", MAX_CLIENTS);
        return MIDI_STATUS_TOO_MANY_CLIENTS;
    }

    // Check client count limit per application (UID)
    auto &appClients = appClientMap_[callingUid];
    if (appClients.size() >= MAX_CLIENTS_PER_APP) {
        MIDI_ERR_LOG("Application (UID=%{public}u) has reached maximum client count: %{public}u",
            callingUid, MAX_CLIENTS_PER_APP);
        return MIDI_STATUS_TOO_MANY_CLIENTS;
    }
    do {
        if (currentClientId_ >= MAX_CLIENTID) {
            currentClientId_ = 0;
        }
        clientId = ++currentClientId_;
    } while (clients_.find(clientId) != clients_.end());
    sptr<MidiInServer> midiClient = new (std::nothrow) MidiInServer(clientId, callback);
    CHECK_AND_RETURN_RET_LOG(midiClient != nullptr, MIDI_STATUS_UNKNOWN_ERROR, "midiClient nullptr");
    client = midiClient->AsObject();
    CHECK_AND_RETURN_RET_LOG(client != nullptr, MIDI_STATUS_UNKNOWN_ERROR, "midiClient->AsObject nullptr");
    sptr<MidiServiceDeathRecipient> deathRecipient_ = new (std::nothrow) MidiServiceDeathRecipient(clientId);
    CHECK_AND_RETURN_RET_LOG(deathRecipient_ != nullptr, MIDI_STATUS_UNKNOWN_ERROR, "deathRecipient_ nullptr");
    std::weak_ptr<MidiServiceController> weakSelf = weak_from_this();
    deathRecipient_->SetNotifyCb([weakSelf](uint32_t clientId) {
        auto self = weakSelf.lock();
        CHECK_AND_RETURN_LOG(self != nullptr, "MidiServiceController destroyed");
        self->DestroyMidiClient(clientId);
    });
    object->AddDeathRecipient(deathRecipient_);
    clients_.emplace(clientId, std::move(midiClient));

    // Store UID in client resource info and add to app client map
    clientResourceInfo_[clientId].uid = callingUid;
    appClientMap_[callingUid].insert(clientId);

    MIDI_INFO_LOG("Create MIDI client success, clientId: %{public}u, UID: %{public}u", clientId, callingUid);
    return MIDI_STATUS_OK;
}

std::vector<std::map<int32_t, std::string>> MidiServiceController::GetDevices()
{
    std::vector<std::map<int32_t, std::string>> ret;
    auto result = deviceManager_->GetDevices();
    for (const auto &d : result) {
        ret.push_back(ConvertDeviceInfo(d));
    }
    return ret;
}

std::vector<std::map<int32_t, std::string>> MidiServiceController::GetDevicePorts(int64_t deviceId)
{
    std::vector<std::map<int32_t, std::string>> ret;
    auto result = deviceManager_->GetDevicePorts(deviceId);
    for (const auto &p : result) {
        std::map<int32_t, std::string> portInfo;
        portInfo[PORT_INDEX] = std::to_string(p.portId);
        portInfo[DIRECTION] = std::to_string(p.direction);
        portInfo[PORT_NAME] = p.name;
        ret.push_back(std::move(portInfo));
    }
    return ret;
}

bool MidiServiceController::IsBluetoothDevice(int64_t deviceId) const
{
    for (const auto &[address, devId] : activeBleDevices_) {
        if (devId == deviceId) {
            return true;
        }
    }
    return false;
}

int32_t MidiServiceController::OpenDevice(uint32_t clientId, int64_t deviceId)
{
    std::lock_guard lock(lock_);
    CHECK_AND_RETURN_RET_LOG(clients_.find(clientId) != clients_.end(),
        MIDI_STATUS_INVALID_CLIENT,
        "Client not found: %{public}u",
        clientId);

    auto &resourceInfo = clientResourceInfo_[clientId];

    if (IsBluetoothDevice(deviceId)) {
        CHECK_AND_RETURN_RET_LOG(MidiPermissionManager::VerifyBluetoothPermission(),
            MIDI_STATUS_PERMISSION_DENIED,
            "Bluetooth permission denied for device: deviceId=%{public}" PRId64,
            deviceId);
    }

    auto it = deviceClientContexts_.find(deviceId);
    if (it != deviceClientContexts_.end()) {
        CHECK_AND_RETURN_RET_LOG(it->second->clients.find(clientId) == it->second->clients.end(),
            MIDI_STATUS_DEVICE_ALREADY_OPEN,
            "Device already opened by client: deviceId=%{public}" PRId64 ", clientId=%{public}u",
            deviceId,
            clientId);
        it->second->clients.insert(clientId);
        MIDI_INFO_LOG(
            "Client added to existing device: deviceId=%{public}" PRId64 ", clientId=%{public}u", deviceId, clientId);
        return MIDI_STATUS_OK;
    }

    // Check device count limit for this client
    if (resourceInfo.openDevices.size() >= MAX_DEVICES_PER_CLIENT) {
        MIDI_ERR_LOG("Client %{public}u has reached maximum device count: %{public}u",
            clientId, MAX_DEVICES_PER_CLIENT);
        return MIDI_STATUS_TOO_MANY_OPEN_DEVICES;
    }

    CHECK_AND_RETURN_RET_LOG(deviceManager_->OpenDevice(deviceId) == MIDI_STATUS_OK,
        MIDI_STATUS_GENERIC_INVALID_ARGUMENT,
        "Open device failed: deviceId=%{public}" PRId64,
        deviceId);
    std::unordered_set<int32_t> clients = {static_cast<int32_t>(clientId)};
    auto context = std::make_shared<DeviceClientContext>(deviceId, std::move(clients));
    deviceClientContexts_.emplace(deviceId, std::move(context));
    resourceInfo.openDevices.insert(deviceId);
    MIDI_INFO_LOG("Device opened successfully: deviceId=%{public}" PRId64 ", clientId=%{public}u", deviceId, clientId);
    return MIDI_STATUS_OK;
}

int32_t MidiServiceController::OpenBleDevice(uint32_t clientId, const std::string &address,
    const sptr<IRemoteObject> &object)
{
    MIDI_INFO_LOG("OpenBleDevice: clientId=%{public}u, device=%{public}s", clientId, GetEncryptStr(address).c_str());

    sptr<IMidiDeviceOpenCallback> callback = iface_cast<IMidiDeviceOpenCallback>(object);
    CHECK_AND_RETURN_RET_LOG(callback != nullptr, MIDI_STATUS_UNKNOWN_ERROR, "callback cast failed");

    std::unique_lock<std::mutex> lock(lock_);
    CHECK_AND_RETURN_RET_LOG(clients_.find(clientId) != clients_.end(), MIDI_STATUS_INVALID_CLIENT,
        "Client not found: %{public}u", clientId);

    auto activeIt = activeBleDevices_.find(address);
    if (activeIt != activeBleDevices_.end()) {
        int64_t deviceId = activeIt->second;
        auto ctxIt = deviceClientContexts_.find(deviceId);
        if (ctxIt != deviceClientContexts_.end()) {
            MIDI_INFO_LOG("BLE Device %{public}s is already active (id=%{public}" PRId64 "). Adding client.",
                GetEncryptStr(address).c_str(), deviceId);
            ctxIt->second->clients.insert(clientId);
            DeviceInformation device = deviceManager_->GetDeviceForDeviceId(deviceId);
            std::map<int32_t, std::string> deviceInfo = ConvertDeviceInfo(device);
            lock.unlock();
            callback->NotifyDeviceOpened(true, deviceInfo);
            return MIDI_STATUS_OK;
        }
    }

    // 2. Check if a connection is already PENDING for this address
    bool isFirstRequest = (pendingBleConnections_.find(address) == pendingBleConnections_.end());
    PendingBleConnection req = { clientId, callback };
    pendingBleConnections_[address].push_back(req);

    if (!isFirstRequest) {
        MIDI_INFO_LOG("Connection to %{public}s already pending. Added clientId %{public}u to queue.",
            GetEncryptStr(address).c_str(), clientId);
        return MIDI_STATUS_OK;
    }
    MIDI_INFO_LOG("Initiating new BLE connection to %{public}s", GetEncryptStr(address).c_str());

    // We use a lambda that captures 'this' to callback into the controller
    std::weak_ptr<MidiServiceController> weakSelf = weak_from_this();
    auto completeCallback = [weakSelf, address](bool success, int64_t deviceId,
        const std::map<int32_t, std::string> &info) {
        auto self = weakSelf.lock();
        CHECK_AND_RETURN_LOG(self != nullptr, "MidiServiceController destroyed");
        self->HandleBleOpenComplete(address, success, deviceId, info);
    };

    int32_t ret = deviceManager_->OpenBleDevice(address, completeCallback);
    if (ret != MIDI_STATUS_OK) {
        MIDI_ERR_LOG("Manager OpenBleDevice failed immediately: %{public}d", ret);
        // Clean up pending list immediately
        pendingBleConnections_.erase(address);
        return ret;
    }
    return MIDI_STATUS_OK;
}

void MidiServiceController::HandleBleOpenComplete(const std::string &address, bool success, int64_t deviceId,
    const std::map<int32_t, std::string> &deviceInfo)
{
    MIDI_INFO_LOG("HandleBleOpenComplete: addr=%{public}s, success=%{public}d, devId=%{public}" PRId64,
                GetEncryptStr(address).c_str(), success, deviceId);

    std::list<PendingBleConnection> waitingClients;

    {
        std::lock_guard lock(lock_);
        auto it = pendingBleConnections_.find(address);
        if (it != pendingBleConnections_.end()) {
            waitingClients = std::move(it->second);
            pendingBleConnections_.erase(it);
        } else {
            MIDI_WARNING_LOG("No pending clients found for %{public}s (maybe cancelled?)",
                GetEncryptStr(address).c_str());
        }

        if (success) {
            // Register map for quick lookup
            activeBleDevices_[address] = deviceId;

            // Create Context ONLY now
            std::unordered_set<int32_t> initialClients;
            for (const auto &req : waitingClients) {
                // Verify client still exists
                if (clients_.find(req.clientId) != clients_.end()) {
                    initialClients.insert(req.clientId);
                }
            }

            if (!initialClients.empty()) {
                auto context = std::make_shared<DeviceClientContext>(deviceId, std::move(initialClients));
                deviceClientContexts_.emplace(deviceId, std::move(context));
            } else {
                MIDI_WARNING_LOG("All waiting clients died before BLE connected.");
                // Should we close the device immediately? For now, keep it or let Manager handle.
                deviceManager_->CloseDevice(deviceId); // Optional strategy
            }
        }
    }
    // Notify clients outside the lock
    for (const auto &req : waitingClients) {
        if (req.callback) {
            req.callback->NotifyDeviceOpened(success, deviceInfo);
        }
    }
}

int32_t MidiServiceController::OpenInputPort(
    uint32_t clientId, std::shared_ptr<MidiSharedRing> &buffer, int64_t deviceId, uint32_t portIndex)
{
    MIDI_INFO_LOG(
        "clientId: %{public}u, deviceId: %{public}" PRId64 " portIndex: %{public}u", clientId, deviceId, portIndex);
    std::lock_guard lock(lock_);
    CHECK_AND_RETURN_RET_LOG(clients_.find(clientId) != clients_.end(),
        MIDI_STATUS_INVALID_CLIENT,
        "Client not found: %{public}u",
        clientId);
    auto it = deviceClientContexts_.find(deviceId);
    CHECK_AND_RETURN_RET_LOG(it != deviceClientContexts_.end(),
        MIDI_STATUS_INVALID_DEVICE_HANDLE,
        "device %{public}" PRId64 "not opened",
        deviceId);
    CHECK_AND_RETURN_RET_LOG(it->second->clients.find(clientId) != it->second->clients.end(),
        MIDI_STATUS_UNKNOWN_ERROR,
        "client %{public}u doesn't open device %{public}" PRId64 "",
        clientId,
        deviceId);

    auto &inputPortConnections = it->second->inputDeviceconnections_;
    auto inputPort = inputPortConnections.find(portIndex);
    if (inputPort != inputPortConnections.end()) {
        CHECK_AND_RETURN_RET_LOG(inputPort->second->HasClientConnection(clientId) != true,
            MIDI_STATUS_PORT_ALREADY_OPEN, "already connected inputport");
        inputPort->second->AddClientConnection(clientId, deviceId, buffer);
        MIDI_INFO_LOG("connect inputport success");
        return MIDI_STATUS_OK;
    }

    // Check port count limit for this client
    auto &resourceInfo = clientResourceInfo_[clientId];
    if (resourceInfo.openPortCount >= MAX_PORTS_PER_CLIENT) {
        MIDI_ERR_LOG("Client %{public}u has reached maximum port count: %{public}u",
            clientId, MAX_PORTS_PER_CLIENT);
        return MIDI_STATUS_TOO_MANY_OPEN_PORTS;
    }

    std::shared_ptr<DeviceConnectionForInput> inputConnection = nullptr;
    auto ret = deviceManager_->OpenInputPort(inputConnection, deviceId, portIndex);
    CHECK_AND_RETURN_RET_LOG(ret == MIDI_STATUS_OK, ret, "open input port fail!");

    inputConnection->AddClientConnection(clientId, deviceId, buffer);
    resourceInfo.openPortCount++;

    inputPortConnections.emplace(portIndex, std::move(inputConnection));
    MIDI_INFO_LOG("OpenInputPort Success");
    return MIDI_STATUS_OK;
}

int32_t MidiServiceController::OpenOutputPort(
    uint32_t clientId, std::shared_ptr<MidiSharedRing> &buffer, int64_t deviceId, uint32_t portIndex)
{
    MIDI_INFO_LOG(
        "clientId: %{public}u, deviceId: %{public}" PRId64 " portIndex: %{public}u", clientId, deviceId, portIndex);
    std::lock_guard lock(lock_);
    CHECK_AND_RETURN_RET_LOG(clients_.find(clientId) != clients_.end(),
        MIDI_STATUS_INVALID_CLIENT,
        "Client not found: %{public}u",
        clientId);
    auto it = deviceClientContexts_.find(deviceId);
    CHECK_AND_RETURN_RET_LOG(it != deviceClientContexts_.end(),
        MIDI_STATUS_INVALID_DEVICE_HANDLE,
        "device %{public}" PRId64 "not opened",
        deviceId);
    CHECK_AND_RETURN_RET_LOG(it->second->clients.find(clientId) != it->second->clients.end(),
        MIDI_STATUS_UNKNOWN_ERROR,
        "client %{public}u doesn't open device %{public}" PRId64 "",
        clientId,
        deviceId);

    auto &outputPortConnections = it->second->outputDeviceconnections_;
    auto outputPort = outputPortConnections.find(portIndex);
    if (outputPort != outputPortConnections.end()) {
        CHECK_AND_RETURN_RET_LOG(outputPort->second->HasClientConnection(clientId) != true,
            MIDI_STATUS_PORT_ALREADY_OPEN, "already connected outputport");
        outputPort->second->AddClientConnection(clientId, deviceId, buffer);
        MIDI_INFO_LOG("connect outputport success");
        return MIDI_STATUS_OK;
    }

    // Check port count limit for this client
    auto &resourceInfo = clientResourceInfo_[clientId];
    if (resourceInfo.openPortCount >= MAX_PORTS_PER_CLIENT) {
        MIDI_ERR_LOG("Client %{public}u has reached maximum port count: %{public}u",
            clientId, MAX_PORTS_PER_CLIENT);
        return MIDI_STATUS_TOO_MANY_OPEN_PORTS;
    }

    std::shared_ptr<DeviceConnectionForOutput> outputConnection = nullptr;
    auto ret = deviceManager_->OpenOutputPort(outputConnection, deviceId, portIndex);
    CHECK_AND_RETURN_RET_LOG(ret == MIDI_STATUS_OK, ret, "open output port fail!");
    // start events handle thread of output port
    outputConnection->Start();
    outputConnection->AddClientConnection(clientId, deviceId, buffer);
    resourceInfo.openPortCount++;
    outputPortConnections.emplace(portIndex, std::move(outputConnection));
    MIDI_INFO_LOG("OpenOutputPort Success");
    return MIDI_STATUS_OK;
}


int32_t MidiServiceController::CloseInputPort(uint32_t clientId, int64_t deviceId, uint32_t portIndex)
{
    MIDI_INFO_LOG(
        "clientId: %{public}u, deviceId: %{public}" PRId64 " portIndex: %{public}u", clientId, deviceId, portIndex);
    std::lock_guard lock(lock_);
    CHECK_AND_RETURN_RET_LOG(clients_.find(clientId) != clients_.end(),
        MIDI_STATUS_INVALID_CLIENT,
        "Client not found: %{public}u",
        clientId);
    return CloseInputPortInner(clientId, deviceId, portIndex);
}

int32_t MidiServiceController::CloseOutputPort(uint32_t clientId, int64_t deviceId, uint32_t portIndex)
{
    MIDI_INFO_LOG(
        "clientId: %{public}u, deviceId: %{public}" PRId64 " portIndex: %{public}u", clientId, deviceId, portIndex);
    std::lock_guard lock(lock_);
    CHECK_AND_RETURN_RET_LOG(clients_.find(clientId) != clients_.end(),
        MIDI_STATUS_INVALID_CLIENT,
        "Client not found: %{public}u",
        clientId);
    return CloseOutputPortInner(clientId, deviceId, portIndex);
}

int32_t MidiServiceController::CloseInputPortInner(uint32_t clientId, int64_t deviceId, uint32_t portIndex)
{
    auto it = deviceClientContexts_.find(deviceId);
    CHECK_AND_RETURN_RET_LOG(it != deviceClientContexts_.end(),
        MIDI_STATUS_INVALID_DEVICE_HANDLE,
        "device %{public}" PRId64 "not opened",
        deviceId);
    CHECK_AND_RETURN_RET_LOG(it->second->clients.find(clientId) != it->second->clients.end(),
        MIDI_STATUS_GENERIC_INVALID_ARGUMENT,
        "client %{public}u doesn't open device %{public}" PRId64,
        clientId,
        deviceId);
    auto &inputPortConnections = it->second->inputDeviceconnections_;
    auto inputPort = inputPortConnections.find(portIndex);
    if (inputPort != inputPortConnections.end()) {
        inputPort->second->RemoveClientConnection(clientId);
        if (inputPort->second->IsEmptyClientConnections()) {
            auto ret = deviceManager_->CloseInputPort(deviceId, portIndex);
            CHECK_AND_RETURN_RET_LOG(ret == MIDI_STATUS_OK, ret, "close input port fail!");
            inputPortConnections.erase(inputPort);

            // Decrement port count when port is fully closed
            auto &resourceInfo = clientResourceInfo_[clientId];
            if (resourceInfo.openPortCount > 0) {
                resourceInfo.openPortCount--;
            }
        }
    }
    return MIDI_STATUS_OK;
}

int32_t MidiServiceController::CloseOutputPortInner(uint32_t clientId, int64_t deviceId, uint32_t portIndex)
{
    auto it = deviceClientContexts_.find(deviceId);
    CHECK_AND_RETURN_RET_LOG(it != deviceClientContexts_.end(),
        MIDI_STATUS_INVALID_DEVICE_HANDLE,
        "device %{public}" PRId64 "not opened",
        deviceId);
    CHECK_AND_RETURN_RET_LOG(it->second->clients.find(clientId) != it->second->clients.end(),
        MIDI_STATUS_GENERIC_INVALID_ARGUMENT,
        "client %{public}u doesn't open device %{public}" PRId64,
        clientId,
        deviceId);
    auto &outputPortConnections = it->second->outputDeviceconnections_;
    auto outputPort = outputPortConnections.find(portIndex);
    if (outputPort != outputPortConnections.end()) {
        outputPort->second->RemoveClientConnection(clientId);
        if (outputPort->second->IsEmptyClientConnections()) {
            auto ret = deviceManager_->CloseOutputPort(deviceId, portIndex);
            CHECK_AND_RETURN_RET_LOG(ret == MIDI_STATUS_OK, ret, "close output port fail!");
            outputPortConnections.erase(outputPort);

            // Decrement port count when port is fully closed
            auto &resourceInfo = clientResourceInfo_[clientId];
            if (resourceInfo.openPortCount > 0) {
                resourceInfo.openPortCount--;
            }
        }
    }
    return MIDI_STATUS_OK;
}

int32_t MidiServiceController::CloseDevice(uint32_t clientId, int64_t deviceId)
{
    std::unique_lock<std::mutex> lock(lock_);
    CHECK_AND_RETURN_RET_LOG(clients_.find(clientId) != clients_.end(),
        MIDI_STATUS_INVALID_CLIENT,
        "Client not found: %{public}u",
        clientId);
    auto it = deviceClientContexts_.find(deviceId);
    CHECK_AND_RETURN_RET_LOG(it != deviceClientContexts_.end(),
        MIDI_STATUS_INVALID_DEVICE_HANDLE,
        "Device not found: deviceId=%{public}" PRId64,
        deviceId);

    auto &clients = it->second->clients;
    auto clientIt = clients.find(clientId);

    CHECK_AND_RETURN_RET_LOG(clientIt != clients.end(),
        MIDI_STATUS_INVALID_DEVICE_HANDLE,
        "Client not associated with device: deviceId=%{public}" PRId64 ", clientId=%{public}u",
        deviceId,
        clientId);

    ClosePortforDevice(clientId, deviceId, it->second);
    clients.erase(clientIt);

    // Remove device from client's resource tracking
    auto &resourceInfo = clientResourceInfo_[clientId];
    resourceInfo.openDevices.erase(deviceId);

    MIDI_INFO_LOG("Client removed from device: deviceId=%{public}" PRId64 ", clientId=%{public}u", deviceId, clientId);
    CHECK_AND_RETURN_RET(clients.empty(), MIDI_STATUS_OK);
    deviceClientContexts_.erase(it);
    for (auto bleIt = activeBleDevices_.begin(); bleIt != activeBleDevices_.end(); bleIt++) {
        if (bleIt->second == deviceId) {
            activeBleDevices_.erase(bleIt);
            break;
        }
    }
    lock.unlock();
    CHECK_AND_RETURN_RET_LOG(deviceManager_->CloseDevice(deviceId) == MIDI_STATUS_OK,
        MIDI_STATUS_UNKNOWN_ERROR,
        "Close device failed: deviceId=%{public}" PRId64,
        deviceId);
    MIDI_INFO_LOG("Device closed: deviceId=%{public}" PRId64, deviceId);
    return MIDI_STATUS_OK;
}

void MidiServiceController::ClosePortforDevice(
    uint32_t clientId, int64_t deviceId, std::shared_ptr<DeviceClientContext> deviceClientContext)
{
    std::vector<uint32_t> portIndexes;
    for (const auto &[portIndex, _] : deviceClientContext->inputDeviceconnections_) {
        portIndexes.push_back(portIndex);
    }
    for (auto portIndex: portIndexes) {
        CloseInputPortInner(clientId, deviceId, portIndex);
    }
    portIndexes.clear();
    for (const auto &[portIndex, _]: deviceClientContext->outputDeviceconnections_) {
        portIndexes.push_back(portIndex);
    }
    for (auto portIndex: portIndexes) {
        CloseOutputPortInner(clientId, deviceId, portIndex);
    }
}

void MidiServiceController::CollectDevicesForClientDestruction(uint32_t clientId,
    std::vector<int64_t> &devicesToClose, std::vector<int64_t> &devicesToClean)
{
    for (auto& [deviceId, context] : deviceClientContexts_) {
        if (context->clients.find(clientId) != context->clients.end()) {
            if (context->clients.size() == 1) {
                devicesToClose.push_back(deviceId);
            }
            devicesToClean.push_back(deviceId);
        }
    }
}

void MidiServiceController::CleanupDeviceForClient(uint32_t clientId, int64_t deviceId)
{
    auto it = deviceClientContexts_.find(deviceId);
    if (it == deviceClientContexts_.end()) {
        return;
    }
    ClosePortforDevice(clientId, deviceId, it->second);
    auto& clients = it->second->clients;
    clients.erase(clientId);
    if (clients.empty()) {
        deviceClientContexts_.erase(it);
        auto bleIt = std::find_if(activeBleDevices_.begin(), activeBleDevices_.end(),
            [deviceId](const auto& pair) { return pair.second == deviceId; });
        if (bleIt != activeBleDevices_.end()) {
            activeBleDevices_.erase(bleIt);
        }
    }
}

void MidiServiceController::CleanupClientResources(uint32_t clientId, uint32_t clientUid)
{
    auto appIt = appClientMap_.find(clientUid);
    if (appIt != appClientMap_.end()) {
        appIt->second.erase(clientId);
        if (appIt->second.empty()) {
            appClientMap_.erase(appIt);
        }
    }
    clientResourceInfo_.erase(clientId);
}

int32_t MidiServiceController::DestroyMidiClient(uint32_t clientId)
{
    MIDI_INFO_LOG("DestroyMidiClient: %{public}u enter", clientId);

    std::vector<int64_t> devicesToClose;
    std::vector<int64_t> devicesToClean;
    uint32_t clientUid = 0;

    {
        std::lock_guard lock(lock_);
        auto it = clients_.find(clientId);
        CHECK_AND_RETURN_RET_LOG(
            it != clients_.end(), MIDI_STATUS_INVALID_CLIENT, "Client not found: %{public}u", clientId);

        CollectDevicesForClientDestruction(clientId, devicesToClose, devicesToClean);

        auto resourceInfoIt = clientResourceInfo_.find(clientId);
        if (resourceInfoIt != clientResourceInfo_.end()) {
            clientUid = resourceInfoIt->second.uid;
        }
        clients_.erase(it);
    }

    for (auto deviceId : devicesToClose) {
        deviceManager_->CloseDevice(deviceId);
    }

    {
        std::lock_guard lock(lock_);
        for (auto deviceId : devicesToClean) {
            CleanupDeviceForClient(clientId, deviceId);
        }
        CleanupClientResources(clientId, clientUid);
    }

    MIDI_INFO_LOG("Client destroyed: %{public}u", clientId);
    CHECK_AND_RETURN_RET(clients_.empty(), MIDI_STATUS_OK);
    ScheduleUnloadTask();
    return MIDI_STATUS_OK;
}

void MidiServiceController::NotifyDeviceChange(DeviceChangeType change, DeviceInformation device)
{
    std::map<int32_t, std::string> deviceInfo = ConvertDeviceInfo(device);
    std::vector<sptr<MidiInServer>> clientsToNotify;

    {
        std::lock_guard lock(lock_);
        if (change == REMOVED) {
            MIDI_INFO_LOG("Device removed: deviceId=%{public}" PRId64, device.deviceId);
            for (auto it = activeBleDevices_.begin(); it != activeBleDevices_.end(); it++) {
                if (it->second == device.deviceId) {
                    activeBleDevices_.erase(it);
                    break;
                }
            }
            auto it = deviceClientContexts_.find(device.deviceId);
            if (it != deviceClientContexts_.end()) {
                deviceClientContexts_.erase(it);
            }
        }
        clientsToNotify.reserve(clients_.size());
        for (auto& [id, client] : clients_) {
            if (client != nullptr) {
                clientsToNotify.push_back(client);
            }
        }
    }
    for (auto& client : clientsToNotify) {
        client->NotifyDeviceChange(change, deviceInfo);
    }
}

void MidiServiceController::NotifyError(int32_t code)
{
    std::vector<sptr<MidiInServer>> clientsToNotify;
    {
        std::lock_guard lock(lock_);
        clientsToNotify.reserve(clients_.size());
        for (auto& [id, client] : clients_) {
            if (client != nullptr) {
                clientsToNotify.push_back(client);
            }
        }
    }
    for (auto& client : clientsToNotify) {
        client->NotifyError(code);
    }
}

#ifdef UNIT_TEST_SUPPORT
void MidiServiceController::ClearStateForTest()
{
    // Cancel any pending unload task before clearing state
    CancelUnloadTask();

    // Wait for unload thread to complete (immediate in test mode since UNLOAD_DELAY_TIME = 0)
    if (unloadThread_.joinable()) {
        unloadThread_.join();
    }

    std::lock_guard<std::mutex> lock(lock_);
    deviceClientContexts_.clear();
    activeBleDevices_.clear();
    pendingBleConnections_.clear();
    clients_.clear();
    if (deviceManager_) {
        deviceManager_->ClearStateForTest();
    }
    currentClientId_.store(0);
}

bool MidiServiceController::HasDeviceContextForTest(int64_t deviceId) const
{
    std::lock_guard<std::mutex> lock(lock_);
    return deviceClientContexts_.find(deviceId) != deviceClientContexts_.end();
}

bool MidiServiceController::HasClientForDeviceForTest(int64_t deviceId, uint32_t clientId) const
{
    std::lock_guard<std::mutex> lock(lock_);
    auto it = deviceClientContexts_.find(deviceId);
    if (it == deviceClientContexts_.end()) {
        return false;
    }
    return it->second->clients.find(static_cast<int32_t>(clientId)) != it->second->clients.end();
}
#endif
}  // namespace MIDI
}  // namespace OHOS