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
#define LOG_TAG "MidiDeviceConnection"
#endif

#include <algorithm>
#include <cerrno>
#include <cstring>

#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <unistd.h>

#include "native_midi_base.h"
#include "midi_log.h"
#include "midi_device_connection.h"

namespace OHOS {
namespace MIDI {

// ====== UniqueFd ======
UniqueFd::~UniqueFd() { Reset(); }

UniqueFd::UniqueFd(UniqueFd&& other) noexcept
{
    fd_ = other.fd_;
    other.fd_ = -1;
}

UniqueFd& UniqueFd::operator=(UniqueFd&& other) noexcept
{
    if (this != &other) {
        Reset();
        fd_ = other.fd_;
        other.fd_ = -1;
    }
    return *this;
}

void UniqueFd::Reset(int fd)
{
    if (fd_ >= 0) {
        ::close(fd_);
    }
    fd_ = fd;
}


void DrainCounterFd(int fd)
{
    if (fd < 0) {
        return;
    }
    while (true) {
        uint64_t counter = 0;
        const ssize_t ret = ::read(fd, &counter, sizeof(counter));
        if (ret < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            break;
        }
        if (ret <= 0) {
            break;
        }
    }
}


// ====== DeviceConnectionBase ======
DeviceConnectionBase::DeviceConnectionBase(DeviceConnectionInfo info) : info_(info) {}


int32_t DeviceConnectionBase::AddClientConnection(uint32_t clientId, int64_t deviceHandle,
                                                    std::shared_ptr<SharedMidiRing> &buffer)
{
    std::lock_guard<std::mutex> lock(clientsMutex_);
    auto clientConnection = std::make_shared<ClientConnectionInServer>(clientId, deviceHandle, GetInfo().portIndex);
    CHECK_AND_RETURN_RET_LOG(clientConnection != nullptr, MIDI_STATUS_UNKNOWN_ERROR,
        "creat client connection fail");
    CHECK_AND_RETURN_RET_LOG(clientConnection->CreateRingBuffer() == MIDI_STATUS_OK, MIDI_STATUS_UNKNOWN_ERROR,
        "init client connection fail");
    clients_.push_back(std::move(clientConnection));
    buffer = clientConnection->GetRingBuffer();
    return MIDI_STATUS_OK;
}


void DeviceConnectionBase::RemoveClientConnection(uint32_t clientId)
{
    std::lock_guard<std::mutex> lock(clientsMutex_);
    clients_.erase(std::remove_if(clients_.begin(), clients_.end(),
        [&](const std::shared_ptr<ClientConnectionInServer>& c) {
            return c && c->GetClientId() == clientId;
        }), clients_.end());
}

bool DeviceConnectionBase::IsEmptyClientConections()
{
    std::lock_guard<std::mutex> lock(clientsMutex_);
    return clients_.empty();
}

std::vector<std::shared_ptr<ClientConnectionInServer>> DeviceConnectionBase::SnapshotClients() const
{
    std::lock_guard<std::mutex> lock(clientsMutex_);
    return clients_;
}

// ====== DeviceConnectionForInput ======
DeviceConnectionForInput::DeviceConnectionForInput(DeviceConnectionInfo info) : DeviceConnectionBase(info) {}

void DeviceConnectionForInput::HandleDeviceUmpInput(std::vector<MidiEventInner> &events)
{
    for (auto &event: events) {
        BroadcastToClients(event);
    }
}

void DeviceConnectionForInput::BroadcastToClients(const MidiEventInner& ev)
{
    auto clients = SnapshotClients();
    for (auto& c : clients) {
        if (!c) continue;
        c->TrySendToClient(ev); // debug: check return value
    }
}

// ====== DeviceConnectionForOutput ======
DeviceConnectionForOutput::DeviceConnectionForOutput(DeviceConnectionInfo info) : DeviceConnectionBase(info) {}

DeviceConnectionForOutput::~DeviceConnectionForOutput()
{
    (void)Stop();
}

int32_t DeviceConnectionForOutput::Start()
{
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        return MIDI_STATUS_OK;
    }

    int32_t rc = InitEpollAndFds();
    if (rc != MIDI_STATUS_OK) {
        running_.store(false);
        return rc;
    }

    worker_ = std::thread(&DeviceConnectionForOutput::ThreadMain, this);
    return MIDI_STATUS_OK;
}

int32_t DeviceConnectionForOutput::Stop()
{
    bool expected = true;
    if (!running_.compare_exchange_strong(expected, false)) {
        return MIDI_STATUS_OK;
    }

    WakeWorkerByEventFd();

    if (worker_.joinable()) {
        worker_.join();
    }
    return MIDI_STATUS_OK;
}

int DeviceConnectionForOutput::GetNotifyEventFdForClients() const
{
    return notifyEventFd_.Get();
}


void DeviceConnectionForOutput::SetPerClientMaxPendingEvents(size_t maxPendingEvents)
{
    perClientMaxPendingEvents_ = maxPendingEvents;
}

void DeviceConnectionForOutput::SetMaxSendCacheBytes(size_t maxSendCacheBytes)
{
    maxSendCacheBytes_ = maxSendCacheBytes;
}


int32_t DeviceConnectionForOutput::InitEpollAndFds()
{
    int eventFd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (eventFd < 0) {
        return MIDI_STATUS_UNKNOWN_ERROR;
    }
    notifyEventFd_.Reset(eventFd);

    int tfd = ::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (tfd < 0) {
        return MIDI_STATUS_UNKNOWN_ERROR;
    }
    timerFd_.Reset(tfd);

    int epfd = ::epoll_create1(EPOLL_CLOEXEC);
    if (epfd < 0) {
        return MIDI_STATUS_UNKNOWN_ERROR;
    }
    epollFd_.Reset(epfd);

    epoll_event evNotify {};
    evNotify.events = EPOLLIN;
    evNotify.data.u64 = kEpollTagNotifyEventFd;
    if (::epoll_ctl(epollFd_.Get(), EPOLL_CTL_ADD, notifyEventFd_.Get(), &evNotify) != 0) {
        return MIDI_STATUS_UNKNOWN_ERROR;
    }

    epoll_event evTimer {};
    evTimer.events = EPOLLIN;
    evTimer.data.u64 = kEpollTagTimerFd;
    if (::epoll_ctl(epollFd_.Get(), EPOLL_CTL_ADD, timerFd_.Get(), &evTimer) != 0) { // todo: timer epoll maybe no need, one fd enough?
        return MIDI_STATUS_UNKNOWN_ERROR;
    }

    return MIDI_STATUS_OK;
}


void DeviceConnectionForOutput::WakeWorkerByEventFd()
{
    if (!notifyEventFd_.Valid()) {
        return;
    }
    const uint64_t one = 1;
    (void)::write(notifyEventFd_.Get(), &one, sizeof(one));
}


void DeviceConnectionForOutput::DrainEventFd()
{
    DrainCounterFd(notifyEventFd_.Get());
}

void DeviceConnectionForOutput::DrainTimerFd()
{
    DrainCounterFd(timerFd_.Get());
}

void DeviceConnectionForOutput::ThreadMain()
{
    UpdateNextTimer(); // 初始 disarm 或设定

    while (running_.load()) {
        epoll_event events[8] {};
        const int readyCount = ::epoll_wait(epollFd_.Get(), events, 8, -1);
        if (readyCount < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }

        // 不区分触发源：统一 drain 可读 fd，避免重复触发
        DrainEventFd();
        DrainTimerFd();

        HandleWakeupOnce();
    }
}

void DeviceConnectionForOutput::HandleWakeupOnce()
{
    DrainAllClientsRings();
    CollectDueEventsFromClientHeaps();
    FlushSendCacheToDriver();
    UpdateNextTimer();
}

// ---------------- Step1: drain ring ----------------
void DeviceConnectionForOutput::DrainAllClientsRings()
{
    const auto clientsSnapshot = SnapshotClients();
    for (const auto& clientConnection : clientsSnapshot) {
        if (!clientConnection) {
            continue;
        }
        DrainSingleClientRing(*clientConnection);
    }
}


void DeviceConnectionForOutput::DrainSingleClientRing(ClientConnectionInServer& clientConnection)
{
    std::shared_ptr<SharedMidiRing> ringShared = clientConnection.GetRingBuffer();
    if (!ringShared) {
        return;
    }
    SharedMidiRing& clientRing = *ringShared;

    while (true) {
        SharedMidiRing::PeekedEvent ringEvent {};
        const auto status = clientRing.PeekNext(ringEvent);

        if (status == MidiStatusCode::WOULD_BLOCK) { // todo: no need
            break;
        }
        if (status != MidiStatusCode::OK) {
            break;
        }

        if (ringEvent.timestamp == 0) {     // todo: use func and judge if timestamp + 1 < now
            if (!ConsumeRealtimeEvent(clientRing, ringEvent)) {
                break;
            }
            continue;
        }

        if (!ConsumeNonRealtimeEvent(clientConnection, clientRing, ringEvent)) {
            // 堆满/入堆失败：不 CommitRead，保留共享内存，停止读取该 client
            break;
        }
    }
}

bool DeviceConnectionForOutput::ConsumeRealtimeEvent(SharedMidiRing& clientRing,
                                                    const SharedMidiRing::PeekedEvent& ringEvent)
{
    std::vector<uint8_t> payload; // todo: do not create vector here

    payload.assign(ringEvent.payloadPtr, ringEvent.payloadPtr + ringEvent.length);

    if (!TryAppendToSendCache(payload)) {
        FlushSendCacheToDriver(); // todo: flush outside
    }
    if (!TryAppendToSendCache(payload)) { //todo: no need if, send directly
        SendToDriver(payload);
    }

    clientRing.CommitRead(ringEvent);
    return true;
}

bool DeviceConnectionForOutput::ConsumeNonRealtimeEvent(ClientConnectionInServer& clientConnection,
                                                       SharedMidiRing& clientRing,
                                                       const SharedMidiRing::PeekedEvent& ringEvent)
{
    if (clientConnection.IsPendingFull()) {
        return false;
    }

    // timestamp: relative delay
    const auto now = std::chrono::steady_clock::now();
    const auto delay = std::chrono::nanoseconds(static_cast<int64_t>(ringEvent.timestamp));
    const auto dueTime = now + delay;

    const bool enqueued = clientConnection.EnqueueNonRealtime(
        ringEvent.payloadPtr, ringEvent.length, dueTime, ringEvent.timestamp);
    if (!enqueued) {
        return false;
    }

    clientRing.CommitRead(ringEvent);
    return true;
}

// ---------------- Step2: collect due from per-client heaps ----------------
void DeviceConnectionForOutput::CollectDueEventsFromClientHeaps()
{
    const auto clientsSnapshot = SnapshotClients(); // todo: make clientsSnapshot be clientsSnapshot_
    auto now = std::chrono::steady_clock::now();

    while (true) {
        std::chrono::steady_clock::time_point earliestDueTime {};
        auto earliestClient = FindClientWithEarliestDue(clientsSnapshot, earliestDueTime);
        if (!earliestClient) {
            break;
        }
        if (earliestDueTime > now) { // todo: earliestDueTime  > now + 1, new a function
            break;
        }

        ClientConnectionInServer::PendingEvent dueEvent;
        if (!earliestClient->PopPendingTop(dueEvent)) {
            break;
        }

        const std::vector<uint8_t>& payload = dueEvent.data;

        if (!TryAppendToSendCache(payload)) {
            FlushSendCacheToDriver();
        }
        if (!TryAppendToSendCache(payload)) { // todo: no need if, send directly
            SendToDriver(payload);
        }

        now = std::chrono::steady_clock::now();
    }
}


// ---------------- Step3: flush cache ----------------
bool DeviceConnectionForOutput::TryAppendToSendCache(const std::vector<uint8_t> &payload)
{
    if (payload.empty()) {
        return true;
    }

    const size_t payloadSize = payload.size();
    if (payloadSize > maxSendCacheBytes_) {
        return false;
    }
    if (currentSendCacheBytes_ + payloadSize > maxSendCacheBytes_) {
        return false;
    }

    SendItem item;
    item.data = payload;
    sendCache_.push_back(std::move(item));
    currentSendCacheBytes_ += payloadSize;
    return true;
}

std::shared_ptr<ClientConnectionInServer> DeviceConnectionForOutput::FindClientWithEarliestDue(
    const std::vector<std::shared_ptr<ClientConnectionInServer>>& clientsSnapshot,
    std::chrono::steady_clock::time_point& outEarliestDueTime)
{
    std::shared_ptr<ClientConnectionInServer> bestClient;
    bool hasCandidate = false;

    for (const auto& clientConnection : clientsSnapshot) {
        if (!clientConnection) {
            continue;
        }
        const auto* top = clientConnection->PeekPendingTop();
        if (!top) {
            continue;
        }

        if (!hasCandidate || top->due < outEarliestDueTime) {
            hasCandidate = true;
            outEarliestDueTime = top->due;
            bestClient = clientConnection;
        }
    }
    return bestClient;
}


void DeviceConnectionForOutput::FlushSendCacheToDriver()
{
    if (sendCache_.empty()) {
        return;
    }

    for (const auto& item : sendCache_) {
        SendToDriver(item.data);
    }

    sendCache_.clear();
    currentSendCacheBytes_ = 0;
}

void DeviceConnectionForOutput::SendToDriver(const std::vector<uint8_t> &payload)
{
    // TODO:
    // GetInfo().driver->SendUmpOutput(GetInfo().deviceId, GetInfo().portIndex, payload.data(), payload.size());
    (void)payload;
}


// ---------------- Step4: timerfd ----------------
void DeviceConnectionForOutput::UpdateNextTimer()
{
    const auto clientsSnapshot = SnapshotClients();

    bool hasDue = false;
    std::chrono::steady_clock::time_point earliestDueTime {};

    for (const auto& clientConnection : clientsSnapshot) {
        if (!clientConnection) {
            continue;
        }
        const auto* top = clientConnection->PeekPendingTop();
        if (!top) {
            continue;
        }
        if (!hasDue || top->due < earliestDueTime) {
            hasDue = true;
            earliestDueTime = top->due;
        }
    }

    itimerspec newValue {}; // defaul all zero, hasDue == false to disarm
    if (hasDue) {
        auto now = std::chrono::steady_clock::now();
        if (earliestDueTime < now) {
            earliestDueTime = now;
        }
        const auto deltaNs = std::chrono::duration_cast<std::chrono::nanoseconds>(earliestDueTime - now).count();
        newValue.it_value.tv_sec = static_cast<time_t>(deltaNs / MIDI_NS_PER_SECOND);
        newValue.it_value.tv_nsec = static_cast<long>(deltaNs % MIDI_NS_PER_SECOND);
    }
    
    (void)::timerfd_settime(timerFd_.Get(), 0, &newValue, nullptr);
}
} // namespace MIDI
} // namespace OHOS