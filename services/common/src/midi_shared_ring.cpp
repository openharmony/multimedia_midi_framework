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
#define LOG_TAG "MidiSharedRing"
#endif

#include "ashmem.h"
#include <atomic>
#include <cerrno>
#include <cinttypes>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <memory>
#include <securec.h>
#include <sys/mman.h>
#include <thread>

#include "futex_tool.h"
#include "message_parcel.h"
#include "midi_log.h"
#include "midi_shared_ring.h"
#include "native_midi_base.h"

namespace OHOS {
namespace MIDI {
namespace {
const uint32_t MAX_MMAP_BUFFER_SIZE = 0x2000;
static constexpr int INVALID_FD = -1;
static constexpr int MINFD = 2;
} // namespace

class MidiSharedMemoryImpl : public MidiSharedMemory {
public:
    uint8_t *GetBase() const override;
    size_t GetSize() const override;
    int GetFd() const override;
    std::string GetName() const override;

    MidiSharedMemoryImpl(size_t size, const std::string &name);

    MidiSharedMemoryImpl(int fd, size_t size, const std::string &name);

    ~MidiSharedMemoryImpl();

    int32_t Init();

    bool Marshalling(Parcel &parcel) const override;

private:
    void Close();

    uint8_t *base_;
    int fd_;
    size_t size_;
    std::string name_;
};

class ScopedFd {
public:
    explicit ScopedFd(int fd) : fd_(fd) {}
    ~ScopedFd()
    {
        if (fd_ > MINFD) {
            CloseFd(fd_);
        }
    }

private:
    int fd_ = -1;
};

MidiSharedMemoryImpl::MidiSharedMemoryImpl(size_t size, const std::string &name)
    : base_(nullptr), fd_(INVALID_FD), size_(size), name_(name)
{
    MIDI_DEBUG_LOG("MidiSharedMemory ctor with size: %{public}zu name: %{public}s", size_, name_.c_str());
}

MidiSharedMemoryImpl::MidiSharedMemoryImpl(int fd, size_t size, const std::string &name)
    : base_(nullptr), fd_(dup(fd)), size_(size), name_(name)
{
    MIDI_DEBUG_LOG("MidiSharedMemory ctor with fd %{public}d size %{public}zu name %{public}s", fd_, size_,
                   name_.c_str());
}

MidiSharedMemoryImpl::~MidiSharedMemoryImpl()
{
    MIDI_DEBUG_LOG(" %{public}s enter ~MidiSharedMemoryImpl()", name_.c_str());
    Close();
}

int32_t MidiSharedMemoryImpl::Init()
{
    CHECK_AND_RETURN_RET_LOG((size_ > 0 && size_ < MAX_MMAP_BUFFER_SIZE), OH_MIDI_STATUS_GENERIC_INVALID_ARGUMENT,
                             "Init falied: size out of range: %{public}zu", size_);
    bool isFromRemote = false;
    if (fd_ >= 0) {
        if (fd_ == STDIN_FILENO || fd_ == STDOUT_FILENO || fd_ == STDERR_FILENO) {
            MIDI_WARNING_LOG("fd is special fd: %{public}d", fd_);
        }
        isFromRemote = true;
        int size = AshmemGetSize(fd_); // hdi fd may not support
        if (size < 0 || static_cast<size_t>(size) != size_) {
            MIDI_WARNING_LOG("AshmemGetSize faied, get %{public}d", size);
        }
    } else {
        fd_ = AshmemCreate(name_.c_str(), size_);
        if (fd_ == STDIN_FILENO || fd_ == STDOUT_FILENO || fd_ == STDERR_FILENO) {
            MIDI_WARNING_LOG("fd is special fd: %{public}d", fd_);
        }
        CHECK_AND_RETURN_RET_LOG((fd_ >= 0), OH_MIDI_STATUS_SYSTEM_ERROR, "Init falied: fd %{public}d", fd_);
    }

    void *addr = mmap(nullptr, size_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
    CHECK_AND_RETURN_RET_LOG(addr != MAP_FAILED, OH_MIDI_STATUS_SYSTEM_ERROR,
                             "Init falied: fd %{public}d size %{public}zu", fd_, size_);
    base_ = static_cast<uint8_t *>(addr);
    MIDI_DEBUG_LOG("Init %{public}s <%{public}s> done.", (isFromRemote ? "remote" : "local"), name_.c_str());
    return OH_MIDI_STATUS_OK;
}

bool MidiSharedMemoryImpl::Marshalling(Parcel &parcel) const
{
    // Parcel -> MessageParcel
    MessageParcel &msgParcel = static_cast<MessageParcel &>(parcel);
    CHECK_AND_RETURN_RET_LOG((size_ > 0 && size_ < MAX_MMAP_BUFFER_SIZE), false, "invalid size: %{public}zu", size_);
    return msgParcel.WriteFileDescriptor(fd_) && msgParcel.WriteUint64(static_cast<uint64_t>(size_)) &&
           msgParcel.WriteString(name_);
}

void MidiSharedMemoryImpl::Close()
{
    if (base_ != nullptr) {
        (void)munmap(base_, size_);
        base_ = nullptr;
        size_ = 0;
        MIDI_DEBUG_LOG("%{public}s munmap done", name_.c_str());
    }

    if (fd_ >= 0) {
        (void)CloseFd(fd_);
        fd_ = INVALID_FD;
        MIDI_DEBUG_LOG("%{public}s close fd done", name_.c_str());
    }
}

uint8_t *MidiSharedMemoryImpl::GetBase() const { return base_; }

size_t MidiSharedMemoryImpl::GetSize() const { return size_; }

std::string MidiSharedMemoryImpl::GetName() const { return name_; }

int MidiSharedMemoryImpl::GetFd() const { return fd_; }

std::shared_ptr<MidiSharedMemory> MidiSharedMemory::CreateFromLocal(size_t size, const std::string &name)
{
    std::shared_ptr<MidiSharedMemoryImpl> sharedMemory = std::make_shared<MidiSharedMemoryImpl>(size, name);
    CHECK_AND_RETURN_RET_LOG(sharedMemory->Init() == OH_MIDI_STATUS_OK, nullptr, "CreateFromLocal failed");
    return sharedMemory;
}

std::shared_ptr<MidiSharedMemory> MidiSharedMemory::CreateFromRemote(int fd, size_t size, const std::string &name)
{
    int minfd = MINFD; // ignore stdout, stdin and stderr.
    CHECK_AND_RETURN_RET_LOG(fd > minfd, nullptr, "CreateFromRemote failed: invalid fd: %{public}d", fd);
    std::shared_ptr<MidiSharedMemoryImpl> sharedMemory = std::make_shared<MidiSharedMemoryImpl>(fd, size, name);
    if (sharedMemory->Init() != OH_MIDI_STATUS_OK) {
        MIDI_ERR_LOG("CreateFromRemote failed");
        return nullptr;
    }
    return sharedMemory;
}

bool MidiSharedMemory::Marshalling(Parcel &parcel) const { return true; }

MidiSharedMemory *MidiSharedMemory::Unmarshalling(Parcel &parcel)
{
    // Parcel -> MessageParcel
    MessageParcel &msgParcel = static_cast<MessageParcel &>(parcel);
    int fd = msgParcel.ReadFileDescriptor();
    int minfd = MINFD; // ignore stdout, stdin and stderr.
    CHECK_AND_RETURN_RET_LOG(fd > minfd, nullptr, "CreateFromRemote failed: invalid fd: %{public}d", fd);
    ScopedFd scopedFd(fd);

    uint64_t sizeTmp = msgParcel.ReadUint64();
    CHECK_AND_RETURN_RET_LOG((sizeTmp > 0 && sizeTmp < MAX_MMAP_BUFFER_SIZE), nullptr, "failed with invalid size");
    size_t size = static_cast<size_t>(sizeTmp);

    off_t actualSize = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    CHECK_AND_RETURN_RET_LOG((actualSize == (off_t)size) && size != 0, nullptr,
                             "CreateFromRemote failed: actualSize is not equal to declareSize");

    std::string name = msgParcel.ReadString();

    auto memory = new (std::nothrow) MidiSharedMemoryImpl(fd, size, name);
    if (memory == nullptr) {
        MIDI_ERR_LOG("not enough memory");
        return nullptr;
    }

    if (memory->Init() != OH_MIDI_STATUS_OK || memory->GetBase() == nullptr) {
        MIDI_ERR_LOG("Init failed or GetBase failed");
        delete memory;
        return nullptr;
    }
    return memory;
}

//==================== Ring Math ====================//
inline uint32_t RingUsed(uint32_t r, uint32_t w, uint32_t cap) { return (w >= r) ? (w - r) : (cap - (r - w)); }

inline uint32_t RingFree(uint32_t r, uint32_t w, uint32_t cap)
{
    const uint32_t used = RingUsed(r, w, cap);
    return (cap - 1u) - used;
}

inline bool IsValidOffset(uint32_t off, uint32_t cap) { return off < cap; }

//==================== MidiSharedRing Public ====================//

MidiSharedRing::MidiSharedRing(uint32_t ringCapacityBytes) : capacity_(ringCapacityBytes)
{
    totalMemorySize_ = sizeof(ControlHeader) + ringCapacityBytes;
}

MidiSharedRing::MidiSharedRing(uint32_t ringCapacityBytes, std::shared_ptr<UniqueFd> fd): capacity_(ringCapacityBytes)
{
    totalMemorySize_ = sizeof(ControlHeader) + ringCapacityBytes;
    notifyFd_ = fd;
}

int32_t MidiSharedRing::Init(int dataFd)
{
    CHECK_AND_RETURN_RET_LOG(totalMemorySize_ <= MAX_MMAP_BUFFER_SIZE, OH_MIDI_STATUS_GENERIC_INVALID_ARGUMENT,
                             "failed: invalid totalMemorySize_");
    if (dataFd == INVALID_FD) {
        dataMem_ = MidiSharedMemory::CreateFromLocal(totalMemorySize_, "midi_shared_buffer");
    } else {
        dataMem_ = MidiSharedMemory::CreateFromRemote(dataFd, totalMemorySize_, "midi_shared_buffer");
    }
    base_ = dataMem_->GetBase();
    controler_ = reinterpret_cast<ControlHeader *>(base_);
    controler_->capacity = capacity_;
    // Initialize with release ordering for proper synchronization
    controler_->readPosition.store(0, std::memory_order_release);
    controler_->writePosition.store(0, std::memory_order_release);
    controler_->flushFlag.store(0, std::memory_order_release);

    ringBase_ = base_ + sizeof(ControlHeader);

    MIDI_DEBUG_LOG("Init done.");
    return OH_MIDI_STATUS_OK;
}

int MidiSharedRing::GetEventFd() const
{
    CHECK_AND_RETURN_RET_LOG(notifyFd_, -1, "notifyFd_ is nullptr"); // -1 is invalid fd
    return notifyFd_->Get();
}

std::shared_ptr<MidiSharedRing> MidiSharedRing::CreateFromLocal(size_t ringCapacityBytes)
{
    MIDI_DEBUG_LOG("ringCapacityBytes %{public}zu", ringCapacityBytes);

    std::shared_ptr<MidiSharedRing> buffer = std::make_shared<MidiSharedRing>(ringCapacityBytes);
    CHECK_AND_RETURN_RET_LOG(buffer->Init(INVALID_FD) == OH_MIDI_STATUS_OK, nullptr, "failed to init.");
    return buffer;
}

std::shared_ptr<MidiSharedRing> MidiSharedRing::CreateFromLocal(size_t ringCapacityBytes, std::shared_ptr<UniqueFd> fd)
{
    MIDI_DEBUG_LOG("ringCapacityBytes %{public}zu", ringCapacityBytes);
    std::shared_ptr<MidiSharedRing> buffer = std::make_shared<MidiSharedRing>(ringCapacityBytes, fd);
    CHECK_AND_RETURN_RET_LOG(buffer->Init(INVALID_FD) == OH_MIDI_STATUS_OK, nullptr, "failed to init.");
    return buffer;
}

std::shared_ptr<MidiSharedRing> MidiSharedRing::CreateFromRemote(size_t ringCapacityBytes, int dataFd)
{
    MIDI_DEBUG_LOG("dataFd %{public}d", dataFd);

    int minfd = MINFD; // ignore stdout, stdin and stderr.
    CHECK_AND_RETURN_RET_LOG(dataFd > minfd, nullptr, "invalid dataFd: %{public}d", dataFd);

    std::shared_ptr<MidiSharedRing> buffer = std::make_shared<MidiSharedRing>(ringCapacityBytes);
    if (buffer->Init(dataFd) != OH_MIDI_STATUS_OK) {
        MIDI_ERR_LOG("failed to init.");
        return nullptr;
    }
    return buffer;
}

bool MidiSharedRing::Marshalling(Parcel &parcel) const
{
    MessageParcel &messageParcel = static_cast<MessageParcel &>(parcel);
    CHECK_AND_RETURN_RET_LOG(dataMem_ != nullptr, false, "dataMem_ is nullptr.");
    if (notifyFd_ == nullptr) {
        return messageParcel.WriteUint32(capacity_) && messageParcel.WriteFileDescriptor(dataMem_->GetFd());
    }
    return messageParcel.WriteUint32(capacity_) &&
        messageParcel.WriteFileDescriptor(dataMem_->GetFd()) && messageParcel.WriteFileDescriptor(notifyFd_->Get());
}

MidiSharedRing *MidiSharedRing::Unmarshalling(Parcel &parcel)
{
    MIDI_DEBUG_LOG("ReadFromParcel start.");
    MessageParcel &messageParcel = static_cast<MessageParcel &>(parcel);
    uint32_t ringSize = messageParcel.ReadUint32();
    int dataFd = messageParcel.ReadFileDescriptor();
    int eventFd = messageParcel.ReadFileDescriptor();

    int minfd = MINFD; // ignore stdout, stdin and stderr.
    CHECK_AND_RETURN_RET_LOG(dataFd > minfd, nullptr, "invalid dataFd: %{public}d", dataFd);

    auto notifyFd = std::make_shared<UniqueFd>(eventFd);
    auto buffer = new (std::nothrow) MidiSharedRing(ringSize, notifyFd);
    if (buffer == nullptr || buffer->Init(dataFd) != OH_MIDI_STATUS_OK) {
        MIDI_ERR_LOG("failed to init.");
        if (buffer != nullptr)
            delete buffer;
        CloseFd(dataFd);
        return nullptr;
    }

    if (ringSize != buffer->capacity_) {
        MIDI_WARNING_LOG("data in shared memory wrong");
    }
    CloseFd(dataFd);
    MIDI_DEBUG_LOG("ReadFromParcel done.");
    return buffer;
}

uint32_t MidiSharedRing::GetCapacity() const
{
    return capacity_;
}

uint32_t MidiSharedRing::GetReadPosition() const
{
    // Use acquire ordering to ensure visibility of data written by other threads
    return controler_->readPosition.load(std::memory_order_acquire);
}

uint32_t MidiSharedRing::GetWritePosition() const
{
    // Use acquire ordering to ensure visibility of data written by other threads
    return controler_->writePosition.load(std::memory_order_acquire);
}

uint8_t *MidiSharedRing::GetDataBase() const
{
    return ringBase_;
}

bool MidiSharedRing::IsEmpty() const
{
    return GetReadPosition() == GetWritePosition();
}

std::atomic<uint32_t> *MidiSharedRing::GetFutex() const
{
    if (!controler_) {
        return nullptr;
    }
    return &controler_->futexObj;
}

ControlHeader *MidiSharedRing::GetControlHeader() const
{
    return controler_;
}

FutexCode MidiSharedRing::WaitFor(int64_t timeoutInNs, const std::function<bool(void)> &pred)
{
    return FutexTool::FutexWait(GetFutex(), timeoutInNs, [&pred]() { return pred(); });
}

FutexCode MidiSharedRing::WaitForSpace(int64_t timeoutInNs, uint32_t neededBytes)
{
    CHECK_AND_RETURN_RET_LOG(controler_ != nullptr, FUTEX_INVALID_PARAMS, "controler_ is null");
    CHECK_AND_RETURN_RET_LOG(neededBytes > 0, FUTEX_INVALID_PARAMS, "neededBytes invalid");

    auto pred = [this, neededBytes]() -> bool {
        // Use acquire ordering for position indices
        uint32_t r = controler_->readPosition.load(std::memory_order_acquire);
        uint32_t w = controler_->writePosition.load(std::memory_order_acquire);
        return RingFree(r, w, capacity_) >= neededBytes;
    };
    return FutexTool::FutexWait(GetFutex(), timeoutInNs, pred);
}

void MidiSharedRing::WakeFutex(uint32_t wakeVal)
{
    if (controler_) {
        FutexTool::FutexWake(GetFutex(), wakeVal);
    }
}

void MidiSharedRing::NotifyConsumer(uint32_t wakeVal)
{
    WakeFutex(wakeVal);
}

//==================== Write Side ====================//

MidiStatusCode MidiSharedRing::TryWriteEvent(const MidiEventInner &event, bool notify)
{
    uint32_t written = 0;
    return TryWriteEvents(&event, 1, &written, notify);
}

MidiStatusCode MidiSharedRing::TryWriteEvents(
    const MidiEventInner *events, uint32_t eventCount, uint32_t *eventsWritten, bool notify)
{
    if (eventsWritten) {
        *eventsWritten = 0;
    }
    MidiStatusCode status = ValidateWriteArgs(events, eventCount);
    if (status != MidiStatusCode::OK) {
        return status;
    }

    uint32_t localWritten = 0;
    // Use acquire ordering for position indices
    uint32_t readIndex = controler_->readPosition.load(std::memory_order_acquire);
    uint32_t writeIndex = controler_->writePosition.load(std::memory_order_acquire);

    for (uint32_t i = 0; i < eventCount; ++i) {
        const MidiEventInner &event = events[i];
        if (!ValidateOneEvent(event)) {
            break;
        }
        CHECK_AND_BREAK_LOG(ValidateOneEvent(event), "invalid envent");
        const size_t payloadBytesSize = event.length * sizeof(uint32_t);
        const uint32_t needed = static_cast<uint32_t>(sizeof(ShmMidiEventHeader) + payloadBytesSize);

        auto ret = TryWriteOneEvent(event, needed, readIndex, writeIndex);
        CHECK_AND_BREAK_LOG(ret == MidiStatusCode::OK, "write event fail");
        ++localWritten;
        // Reload readPosition with acquire
        readIndex = controler_->readPosition.load(std::memory_order_acquire);
    }

    if (eventsWritten) {
        *eventsWritten = localWritten;
    }

    if (localWritten == 0) {
        // If eventCount was 0, this is a valid empty write, not a "would block" situation
        if (eventCount == 0) {
            return MidiStatusCode::OK;
        }
        return MidiStatusCode::WOULD_BLOCK;
    }

    if (notify) {
        NotifyConsumer();
        if (notifyFd_ && notifyFd_->Valid()) {
            MIDI_DEBUG_LOG("notify server to consume midi events");
            uint64_t writed = 1;
            (void)::write(notifyFd_->Get(), &writed, sizeof(writed));
        }
    }
    return (localWritten == eventCount) ? MidiStatusCode::OK : MidiStatusCode::WOULD_BLOCK;
}

//==================== Read Side (Peek + Commit) ====================//

// Helper to safely read header with sequence verification (TOCTOU protection)
static bool ReadHeaderSafely(const ShmMidiEventHeader *header, ShmMidiEventHeader &outHeader, uint32_t &outSeq)
{
    // Read sequence first (acquire ensures visibility of header data)
    uint32_t seq1 = header->sequence.load(std::memory_order_acquire);

    // Check if sequence is odd (writer is currently modifying)
    // Writer pattern: seq+1 (odd) -> write data -> seq+2 (even)
    if (seq1 & 1) {
        return false; // Writer is active, retry needed
    }

    std::atomic_thread_fence(std::memory_order_acquire);

    // Copy header data
    outHeader.timestamp = header->timestamp;
    outHeader.length = header->length;
    outHeader.flags = header->flags;
    outHeader.sequence = seq1;

    std::atomic_thread_fence(std::memory_order_acquire);

    // Read sequence again
    uint32_t seq2 = header->sequence.load(std::memory_order_acquire);

    // If sequence changed, header was being modified
    if (seq1 != seq2) {
        return false; // Retry needed
    }

    outSeq = seq1;
    return true;
}

MidiStatusCode MidiSharedRing::PeekNext(PeekedEvent &outEvent)
{
    outEvent = PeekedEvent{};

    CHECK_AND_RETURN_RET(capacity_ >= (sizeof(ShmMidiEventHeader) + 1u), MidiStatusCode::SHM_BROKEN);

    int retryCount = 3; // Limit retries to prevent infinite loop
    while (retryCount-- > 0) {
        // Use acquire ordering for position indices
        uint32_t readIndex = controler_->readPosition.load(std::memory_order_acquire);
        uint32_t writeIndex = controler_->writePosition.load(std::memory_order_acquire);

        auto ret = UpdateReadIndexIfNeed(readIndex, writeIndex);
        if (ret != MidiStatusCode::OK) {
            return ret;
        }

        // Read header safely with sequence verification (TOCTOU protection)
        const ShmMidiEventHeader *header = reinterpret_cast<const ShmMidiEventHeader *>(ringBase_ + readIndex);
        ShmMidiEventHeader localHeader;
        uint32_t seq;
        if (!ReadHeaderSafely(header, localHeader, seq)) {
            // Header was being modified, retry
            continue;
        }

        ret = HandleWrapIfNeeded(localHeader, readIndex);
        if (ret == MidiStatusCode::OK) {
            continue; // Wrapped, retry with new position
        }
        if (ret == MidiStatusCode::SHM_BROKEN) {
            return ret;
        }

        // Store local copy and sequence for verification
        outEvent.localHeader = localHeader;
        outEvent.sequence = seq;
        return BuildPeekedEvent(localHeader, readIndex, outEvent);
    }

    return MidiStatusCode::WOULD_BLOCK; // Too many retries
}

bool MidiSharedRing::CommitRead(const PeekedEvent &ev)
{
    // Verify header hasn't changed before committing (TOCTOU protection)
    uint32_t readIndex = ev.beginOffset;
    const ShmMidiEventHeader *header = reinterpret_cast<const ShmMidiEventHeader *>(ringBase_ + readIndex);
    uint32_t currentSeq = header->sequence.load(std::memory_order_acquire);
    if (currentSeq != ev.sequence) {
        // Header was modified, data may be corrupt - reject this event
        MIDI_WARNING_LOG("Header modified between peek and commit, discarding event");
        return false;
    }

    uint32_t end = ev.endOffset;
    if (end >= capacity_) {
        end = 0;
    }
    // Use release ordering
    controler_->readPosition.store(end, std::memory_order_release);
    WakeFutex(); // wake who is waiting to write data
    return true;
}

void MidiSharedRing::DrainToBatch(
    std::vector<MidiEvent> &outEvents, std::vector<std::vector<uint32_t>> &outPayloadBuffers, uint32_t maxEvents)
{
    uint32_t count = 0;
    while (maxEvents == 0 || count < maxEvents) {
        PeekedEvent peekedEvent;
        MidiStatusCode status = PeekNext(peekedEvent);
        if (status == MidiStatusCode::WOULD_BLOCK) {
            break;
        }
        if (status != MidiStatusCode::OK) {
            break;
        }

        std::vector<uint32_t> payloadBuffer;
        MidiEvent copiedEvent = CopyOut(peekedEvent, payloadBuffer);

         // Only add event to output if commit succeeds (sequence verified)
        if (CommitRead(peekedEvent)) {
            outEvents.push_back(copiedEvent);
            outPayloadBuffers.push_back(std::move(payloadBuffer));
            ++count;
        } else {
            // Event was corrupted between peek and commit, discard it
            // The writer has modified the data, we can't trust copiedEvent
            MIDI_INFO_LOG("Discarding corrupted event at offset %{public}u", peekedEvent.beginOffset);
            // Continue to try next event
        }
    }
}

void MidiSharedRing::Flush()
{
    MIDI_INFO_LOG("reset data cache");

    // Use atomic flag to synchronize with other operations
    uint32_t expected = 0;
    while (!controler_->flushFlag.compare_exchange_weak(expected, 1,
                                                        std::memory_order_acquire,
                                                        std::memory_order_relaxed)) {
        expected = 0;
        // Yield CPU to prevent busy-wait spin
        std::this_thread::yield();
    }

    // Now we have exclusive access
    controler_->readPosition.store(0, std::memory_order_release);
    controler_->writePosition.store(0, std::memory_order_release);
    memset_s(GetDataBase(), GetCapacity(), 0, GetCapacity());

    // Release the flag
    controler_->flushFlag.store(0, std::memory_order_release);
}

//==================== Private Helpers (All <= 50 lines) ====================//

MidiStatusCode MidiSharedRing::ValidateWriteArgs(const MidiEventInner *events, uint32_t eventCount) const
{
    if (eventCount == 0) {
        return MidiStatusCode::OK;
    }
    if (!events) {
        return MidiStatusCode::INVALID_ARGUMENT;
    }
    if (capacity_ < (sizeof(ShmMidiEventHeader) + 1u)) {
        return MidiStatusCode::SHM_BROKEN;
    }
    return MidiStatusCode::OK;
}

bool MidiSharedRing::ValidateOneEvent(const MidiEventInner &event) const
{
    CHECK_AND_RETURN_RET_LOG(event.data != nullptr, false, "invalid event!");

    if (event.length > (std::numeric_limits<size_t>::max() / sizeof(uint32_t))) {
        return false;
    }
    const size_t payloadBytes = event.length * sizeof(uint32_t);
    const size_t maxLeftBytes = static_cast<size_t>(capacity_) - 1u - sizeof(ShmMidiEventHeader);
    CHECK_AND_RETURN_RET_LOG(payloadBytes <= maxLeftBytes, false, "event length overflow");
    return true;
}

MidiStatusCode MidiSharedRing::TryWriteOneEvent(
    const MidiEventInner &event, uint32_t totalBytes, uint32_t readIndex, uint32_t &writeIndex)
{
    const uint32_t freeSize = RingFree(readIndex, writeIndex, capacity_);
    CHECK_AND_RETURN_RET(freeSize >= totalBytes, MidiStatusCode::WOULD_BLOCK);

    UpdateWriteIndexIfNeed(writeIndex, totalBytes);
    const uint32_t writeSize = (writeIndex < readIndex) ? (readIndex - writeIndex - 1u) : (capacity_ - writeIndex);
    CHECK_AND_RETURN_RET(writeSize >= totalBytes, MidiStatusCode::WOULD_BLOCK);

    WriteEvent(writeIndex, event);

    writeIndex += totalBytes;
    writeIndex = writeIndex == capacity_ ? 0 : writeIndex;
    // Use release ordering to ensure data is visible before position update
    controler_->writePosition.store(writeIndex, std::memory_order_release);
    return MidiStatusCode::OK;
}

bool MidiSharedRing::UpdateWriteIndexIfNeed(uint32_t &writeIndex, uint32_t totalBytes)
{
    const uint32_t tail = capacity_ - writeIndex;
    if (tail >= totalBytes) {
        return false;
    }

    // if tailBytes not enough, wrap and update writeIndex
    if (tail >= sizeof(ShmMidiEventHeader)) {
        auto *header = reinterpret_cast<ShmMidiEventHeader *>(ringBase_ + writeIndex);
    // Use atomic write for wrap marker with sequence
        uint32_t seq = header->sequence.load(std::memory_order_relaxed);
        header->sequence.store(seq + 1, std::memory_order_release);
        std::atomic_thread_fence(std::memory_order_release);
        header->timestamp = 0;
        header->length = 0;
        header->flags = SHM_EVENT_FLAG_WRAP;
        std::atomic_thread_fence(std::memory_order_release);
        header->sequence.store(seq + 2, std::memory_order_release);
    }
    writeIndex = 0;
    // Use release ordering
    controler_->writePosition.store(writeIndex, std::memory_order_release);
    return true;
}

void MidiSharedRing::WriteEvent(uint32_t writeIndex, const MidiEventInner &event)
{
    uint8_t *dst = ringBase_ + writeIndex;
    auto *header = reinterpret_cast<ShmMidiEventHeader *>(dst);

    // Increment sequence before writing (release ensures prior writes are visible)
    uint32_t seq = header->sequence.load(std::memory_order_relaxed);
    header->sequence.store(seq + 1, std::memory_order_release);
    std::atomic_thread_fence(std::memory_order_release);

    header->timestamp = event.timestamp;
    header->length = static_cast<uint32_t>(event.length);
    header->flags = SHM_EVENT_FLAG_NONE;

    uint8_t *payload = dst + sizeof(ShmMidiEventHeader);
    const size_t payloadBytes = event.length * sizeof(uint32_t);
    if (payloadBytes > 0) {
        memcpy_s(payload, payloadBytes, reinterpret_cast<const void *>(event.data), payloadBytes);
    }

    // Increment sequence after writing
    std::atomic_thread_fence(std::memory_order_release);
    header->sequence.store(seq + 2, std::memory_order_release);
}

MidiStatusCode MidiSharedRing::UpdateReadIndexIfNeed(uint32_t &readIndex, uint32_t writeIndex)
{
    if (!IsValidOffset(readIndex, capacity_) || !IsValidOffset(writeIndex, capacity_)) {
        return MidiStatusCode::SHM_BROKEN;
    }
    if (readIndex == writeIndex) {
        return MidiStatusCode::WOULD_BLOCK;
    }
    CHECK_AND_RETURN_RET_LOG(readIndex != writeIndex, MidiStatusCode::WOULD_BLOCK, "no event in ring buffer");

    const uint32_t tail = capacity_ - readIndex;
    if (tail < sizeof(ShmMidiEventHeader)) {
        readIndex = 0;
        CHECK_AND_RETURN_RET_LOG(readIndex != writeIndex, MidiStatusCode::WOULD_BLOCK, "no event in ring buffer");
    }
    return MidiStatusCode::OK;
}

MidiStatusCode MidiSharedRing::HandleWrapIfNeeded(const ShmMidiEventHeader &header, uint32_t &readIndex)
{
    if ((header.flags & SHM_EVENT_FLAG_WRAP) == 0) {
        return MidiStatusCode::WOULD_BLOCK; // no wrap
    }
    if (header.length != 0) {
        return MidiStatusCode::SHM_BROKEN;
    }
    // Use release ordering
    controler_->readPosition.store(0, std::memory_order_release);
    readIndex = 0;
    return MidiStatusCode::OK; // wrap, continue
}

MidiStatusCode MidiSharedRing::BuildPeekedEvent(
    const ShmMidiEventHeader &header, uint32_t readIndex, PeekedEvent &outEvent)
{
    // Validate length against capacity (prevent overflow)
    if (header.length > (capacity_ / sizeof(uint32_t))) {
        return MidiStatusCode::SHM_BROKEN;
    }

    const uint32_t needed = static_cast<uint32_t>(sizeof(ShmMidiEventHeader) + header.length * sizeof(uint32_t));
    if (needed > (capacity_ - 1u)) {
        return MidiStatusCode::SHM_BROKEN;
    }
    if (readIndex + needed > capacity_) {
        return MidiStatusCode::SHM_BROKEN;
    }

    outEvent.payloadPtr = ringBase_ + readIndex + sizeof(ShmMidiEventHeader);
    outEvent.beginOffset = readIndex;

    uint32_t g_end = readIndex + needed;
    if (g_end == capacity_) {
        g_end = 0;
    }
    outEvent.endOffset = g_end;
    return MidiStatusCode::OK;
}

MidiEvent MidiSharedRing::CopyOut(const PeekedEvent &peekedEvent, std::vector<uint32_t> &outPayloadBuffer) const
{
    MidiEvent event{};
    event.timestamp = peekedEvent.localHeader.timestamp;

    const size_t wordCount = static_cast<size_t>(peekedEvent.localHeader.length);
    event.length = wordCount;

    const size_t payloadBytes = wordCount * sizeof(uint32_t);
    outPayloadBuffer.resize(wordCount);

    if (payloadBytes > 0) {
        (void)memcpy_s(outPayloadBuffer.data(), payloadBytes, peekedEvent.payloadPtr, payloadBytes);
    }

    event.data = outPayloadBuffer.data();
    return event;
}
} // namespace MIDI
} // namespace OHOS
