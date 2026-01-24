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
#include <securec.h>
#include <unistd.h>

#include "native_midi_base.h"
#include "midi_log.h"
#include "midi_device_connection.h"

namespace OHOS {
namespace MIDI {

void DrainCounterFd(int fd)
{
    if (fd < 0) {
        return;
    }
    const int kMaxLoop = 16;
    int loops = 0;

    while (loops < kMaxLoop) {
        uint64_t counter = 0;
        const ssize_t ret = ::read(fd, &counter, sizeof(counter));
        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            break;
        }
        if (ret == 0) {
            break;
        }
        loops++;
    }
}

// ====== DeviceConnectionBase ======
DeviceConnectionBase::DeviceConnectionBase(DeviceConnectionInfo info) : info_(info)
{}

int32_t DeviceConnectionBase::AddClientConnection(
    uint32_t clientId, int64_t deviceHandle, std::shared_ptr<MidiSharedRing> &buffer)
{
    std::lock_guard<std::mutex> lock(clientsMutex_);
    auto clientConnection = std::make_shared<ClientConnectionInServer>(clientId, deviceHandle, GetInfo().portIndex);
    CHECK_AND_RETURN_RET_LOG(clientConnection != nullptr, MIDI_STATUS_UNKNOWN_ERROR, "creat client connection fail");
    CHECK_AND_RETURN_RET_LOG(clientConnection->CreateRingBuffer() == MIDI_STATUS_OK,
        MIDI_STATUS_UNKNOWN_ERROR,
        "init client connection fail");
    buffer = clientConnection->GetRingBuffer();
    clients_.push_back(std::move(clientConnection));
    return MIDI_STATUS_OK;
}

void DeviceConnectionBase::RemoveClientConnection(uint32_t clientId)
{
    std::lock_guard<std::mutex> lock(clientsMutex_);
    clients_.erase(
        std::remove_if(clients_.begin(),
            clients_.end(),
            [&](const std::shared_ptr<ClientConnectionInServer> &c) { return c && c->GetClientId() == clientId; }),
        clients_.end());
}

bool DeviceConnectionBase::HasClientConnection(uint32_t clientId) const
{
    std::lock_guard<std::mutex> lock(clientsMutex_);
    return std::any_of(clients_.begin(), clients_.end(),
        [&](const std::shared_ptr<ClientConnectionInServer> &c) {
            return c && c->GetClientId() == clientId;
        });
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
DeviceConnectionForInput::DeviceConnectionForInput(DeviceConnectionInfo info) : DeviceConnectionBase(info)
{}

void DeviceConnectionForInput::HandleDeviceUmpInput(std::vector<MidiEventInner> &events)
{
    for (auto &event : events) {
        BroadcastToClients(event);
    }
}

void DeviceConnectionForInput::BroadcastToClients(const MidiEventInner &ev)
{
    auto clients = SnapshotClients();
    for (auto &c : clients) {
        if (!c)
            continue;
        c->TrySendToClient(ev);  // debug: check return value
    }
}

// ====== DeviceConnectionForOutput ======
DeviceConnectionForOutput::DeviceConnectionForOutput(DeviceConnectionInfo info) : DeviceConnectionBase(info)
{}

DeviceConnectionForOutput::~DeviceConnectionForOutput()
{
    MIDI_INFO_LOG("DeviceConnectionForOutput Destroy");
    (void)Stop();
}


int32_t DeviceConnectionForOutput::AddClientConnection(
    uint32_t clientId, int64_t deviceHandle, std::shared_ptr<MidiSharedRing> &buffer)
{
    std::lock_guard<std::mutex> lock(clientsMutex_);
    int fd = dup(notifyEventFd_.Get());
    auto clientConnection = std::make_shared<ClientConnectionInServer>(clientId, deviceHandle, GetInfo().portIndex);
    CHECK_AND_RETURN_RET_LOG(clientConnection != nullptr, MIDI_STATUS_UNKNOWN_ERROR, "creat client connection fail");
    CHECK_AND_RETURN_RET_LOG(clientConnection->CreateRingBuffer(fd) == MIDI_STATUS_OK,
        MIDI_STATUS_UNKNOWN_ERROR,
        "init client connection fail");
    buffer = clientConnection->GetRingBuffer();
    clients_.push_back(std::move(clientConnection));
    return MIDI_STATUS_OK;
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

    epoll_event evNotify{};
    evNotify.events = EPOLLIN;
    evNotify.data.u64 = kEpollTagNotifyEventFd;
    if (::epoll_ctl(epollFd_.Get(), EPOLL_CTL_ADD, notifyEventFd_.Get(), &evNotify) != 0) {
        return MIDI_STATUS_UNKNOWN_ERROR;
    }

    epoll_event evTimer{};
    evTimer.events = EPOLLIN;
    evTimer.data.u64 = kEpollTagTimerFd;
    if (::epoll_ctl(epollFd_.Get(), EPOLL_CTL_ADD, timerFd_.Get(), &evTimer) !=
        0) {  // todo: timer epoll maybe no need, one fd enough?
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
    UpdateNextTimer();

    while (running_.load()) {
        epoll_event events[8]{};
        const int readyCount = ::epoll_wait(epollFd_.Get(), events, 8, -1);
        if (readyCount < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }

        DrainEventFd();
        DrainTimerFd();

        HandleWakeupOnce();
    }
}

void DeviceConnectionForOutput::HandleWakeupOnce()
{
    DrainAllClientsRings(); // read event from shared_rings
    CollectDueEventsFromClientHeaps(); // collect due events
    FlushSendCacheToDriver(); // send to driver
    UpdateNextTimer(); // update timer
}

// ---------------- Step1: drain ring ----------------
void DeviceConnectionForOutput::DrainAllClientsRings()
{
    const auto clientsSnapshot = SnapshotClients();
    for (const auto &clientConnection : clientsSnapshot) {
        if (!clientConnection) {
            continue;
        }
        DrainSingleClientRing(*clientConnection);
    }
}

void DeviceConnectionForOutput::DrainSingleClientRing(ClientConnectionInServer &clientConnection)
{
    std::shared_ptr<MidiSharedRing> ringShared = clientConnection.GetRingBuffer();
    if (!ringShared) {
        return;
    }
    MidiSharedRing &clientRing = *ringShared;
    MidiSharedRing::PeekedEvent ringEvent{};
    MidiStatusCode status = MidiStatusCode::OK;
    while ((status = clientRing.PeekNext(ringEvent)) == MidiStatusCode::OK) {
        if (status != MidiStatusCode::OK) {
            break;
        }
        if (ringEvent.timestamp == 0) {  // todo: use func and judge if timestamp + 1 < now
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

bool DeviceConnectionForOutput::ConsumeRealtimeEvent(MidiSharedRing& clientRing,
                                                     const MidiSharedRing::PeekedEvent& ringEvent)
{
    const size_t payloadWordCount = static_cast<size_t>(ringEvent.length);
    const uint32_t* payloadWords =
        reinterpret_cast<const uint32_t*>(ringEvent.payloadPtr);

    // try enqueue send cache
    auto res = TryAppendToSendCache(ringEvent.timestamp, payloadWords, payloadWordCount);
    if (res) {
        clientRing.CommitRead(ringEvent);
        return true;
    }
    // if unable to enqueue, flush the send cache, and try again
    FlushSendCacheToDriver();
    if (!TryAppendToSendCache(ringEvent.timestamp, payloadWords, payloadWordCount)) {
        MidiEventInner directEvent {};
        directEvent.timestamp = ringEvent.timestamp;
        directEvent.length = payloadWordCount;
        directEvent.data = payloadWords;
        SendToDriver(directEvent);
    }

    clientRing.CommitRead(ringEvent);
    return true;
}


bool DeviceConnectionForOutput::ConsumeNonRealtimeEvent(ClientConnectionInServer &clientConnection,
    MidiSharedRing &clientRing, const MidiSharedRing::PeekedEvent &ringEvent)
{
    if (clientConnection.IsPendingFull()) {
        return false;
    }

    const auto dueTime = std::chrono::steady_clock::time_point(std::chrono::nanoseconds(ringEvent.timestamp));

    const size_t payloadWordCount = static_cast<size_t>(ringEvent.length);
    const size_t payloadBytes = payloadWordCount * sizeof(uint32_t);

    std::vector<uint32_t> payloadWords;
    payloadWords.resize(payloadWordCount);

    if (payloadBytes > 0) {
        (void)memcpy_s(payloadWords.data(),
                       payloadBytes,
                       ringEvent.payloadPtr,
                       payloadBytes);
    }

    const bool enqueued = clientConnection.EnqueueNonRealtime(std::move(payloadWords), dueTime, ringEvent.timestamp);
    if (!enqueued) {
        return false;
    }

    clientRing.CommitRead(ringEvent);
    return true;
}

// ---------------- Step2: collect due from per-client heaps ----------------
void DeviceConnectionForOutput::CollectDueEventsFromClientHeaps()
{
    const auto clientsSnapshot = SnapshotClients();  // todo: make clientsSnapshot be clientsSnapshot_
    auto now = std::chrono::steady_clock::now();

    while (true) {
        std::chrono::steady_clock::time_point earliestDueTime {};
        auto earliestClient = FindClientWithEarliestDue(clientsSnapshot, earliestDueTime);
        if (earliestClient == nullptr) {
            break;
        }
        if (earliestDueTime > now) {
            break;
        }

        ClientConnectionInServer::PendingEvent dueEvent;
        if (!earliestClient->PopPendingTop(dueEvent)) {
            break;
        }

        MidiEventInner dueMidiEvent;
        dueMidiEvent.timestamp = dueEvent.timestamp;
        dueMidiEvent.length = dueEvent.data.size();
        dueMidiEvent.data = dueEvent.data.data();

        // try enqueue send cache
        auto res = TryAppendToSendCache(dueEvent.timestamp, dueEvent.data.data(), dueEvent.data.size());
        CHECK_AND_RETURN(res != true);
        FlushSendCacheToDriver();

        if (!TryAppendToSendCache(dueEvent.timestamp, dueEvent.data.data(), dueEvent.data.size())) {
            SendToDriver(dueMidiEvent);
        }

        now = std::chrono::steady_clock::now();
    }
}

// ---------------- Step3: flush cache ----------------
bool DeviceConnectionForOutput::TryAppendToSendCache(uint64_t timestamp,
                                                     const uint32_t* payloadWords,
                                                     size_t payloadWordCount)
{
    if (payloadWordCount == 0) {
        return true;
    }
    if (payloadWords == nullptr) {
        return false;
    }

    const size_t payloadBytes = payloadWordCount * sizeof(uint32_t);

    if (payloadBytes > maxSendCacheBytes_) {
        return false;
    }
    if (currentSendCacheBytes_ + payloadBytes > maxSendCacheBytes_) {
        return false;
    }

    std::vector<uint32_t> payloadBuffer;
    payloadBuffer.resize(payloadWordCount);
    auto ret = memcpy_s(payloadBuffer.data(), payloadBytes, payloadWords, payloadBytes);
    CHECK_AND_RETURN_RET_LOG(ret == 0, false, "copy error");
    MidiEventInner cachedEvent {};
    cachedEvent.timestamp = timestamp;
    cachedEvent.length = payloadWordCount;
    cachedEvent.data = payloadBuffer.data();

    sendCachePayloadBuffers_.push_back(std::move(payloadBuffer));
    sendCache_.push_back(cachedEvent);

    currentSendCacheBytes_ += payloadBytes;
    return true;
}

std::shared_ptr<ClientConnectionInServer> DeviceConnectionForOutput::FindClientWithEarliestDue(
    const std::vector<std::shared_ptr<ClientConnectionInServer>> &clientsSnapshot,
    std::chrono::steady_clock::time_point &outEarliestDueTime)
{
    std::shared_ptr<ClientConnectionInServer> bestClient = nullptr;
    bool hasCandidate = false;

    for (const auto &clientConnection : clientsSnapshot) {
        if (!clientConnection) {
            continue;
        }
        const auto *top = clientConnection->PeekPendingTop();
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
    CHECK_AND_RETURN_LOG(info_.driver != nullptr, "driver is null!");
    info_.driver->HanleUmpInput(info_.deviceId, info_.portIndex, sendCache_);
    sendCache_.clear();
    sendCachePayloadBuffers_.clear();
    currentSendCacheBytes_ = 0;
}

void DeviceConnectionForOutput::SendToDriver(MidiEventInner event)
{
    (void)event;
}

// ---------------- Step4: timerfd ----------------
void DeviceConnectionForOutput::UpdateNextTimer()
{
    const auto clientsSnapshot = SnapshotClients();

    bool hasDue = false;
    std::chrono::steady_clock::time_point earliestDueTime{};

    for (const auto &clientConnection : clientsSnapshot) {
        if (!clientConnection) {
            continue;
        }
        const auto *top = clientConnection->PeekPendingTop();
        if (!top) {
            continue;
        }
        if (!hasDue || top->due < earliestDueTime) {
            hasDue = true;
            earliestDueTime = top->due;
        }
    }

    itimerspec newValue{};  // defaul all zero, hasDue == false to disarm
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
}  // namespace MIDI
}  // namespace OHOS