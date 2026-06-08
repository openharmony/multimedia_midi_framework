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

#ifndef MIDI_IN_SERVER_H
#define MIDI_IN_SERVER_H

#include <mutex>
#include <condition_variable>
#include "midi_info.h"
#include "midi_shared_ring.h"
#include "ipc_midi_in_server_stub.h"

namespace OHOS {
namespace MIDI {

/**
 * @brief Thread-safe callback lifecycle manager with RAII acquire semantics.
 *
 * Provides a close-and-drain barrier: after CloseAndDrain() returns, no thread
 * holds a Guard and the callback can be safely released (e.g. before dlclose).
 */
class CallbackSlot {
public:
    /**
     * @brief RAII guard that holds a reference to the callback.
     * Move-only. On destruction, decrements the active count and may notify
     * a waiting CloseAndDrain().
     */
    class Guard {
    public:
        Guard() = default;
        Guard(Guard &&other) noexcept
            : callback_(std::move(other.callback_)), slot_(other.slot_)
        {
            other.slot_ = nullptr;
        }
        Guard &operator=(Guard &&other) noexcept
        {
            if (this != &other) {
                Release();
                callback_ = std::move(other.callback_);
                slot_ = other.slot_;
                other.slot_ = nullptr;
            }
            return *this;
        }
        Guard(const Guard &) = delete;
        Guard &operator=(const Guard &) = delete;

        explicit operator bool() const { return callback_ != nullptr; }
        MidiServiceCallback *operator->() const { return callback_.get(); }
        ~Guard() { Release(); }

    private:
        friend class CallbackSlot;
        Guard(std::shared_ptr<MidiServiceCallback> cb, CallbackSlot *slot)
            : callback_(std::move(cb)), slot_(slot)
        {
        }
        void Release()
        {
            if (slot_ != nullptr) {
                slot_->OnGuardReleased();
                slot_ = nullptr;
            }
            callback_.reset();
        }
        std::shared_ptr<MidiServiceCallback> callback_;
        CallbackSlot *slot_ = nullptr;
    };

    explicit CallbackSlot(std::shared_ptr<MidiServiceCallback> callback)
        : callback_(std::move(callback))
    {
    }

    ~CallbackSlot();
    /**
     * @brief Acquire a Guard for the callback.
     * @return Non-empty Guard if callback is valid and slot is not closing;
     *         empty Guard otherwise.
     */
    Guard Acquire()
    {
        std::lock_guard<std::mutex> lk(mutex_);
        if (closing_ || !callback_) {
            return Guard();
        }
        activeCallbacks_++;
        return Guard(callback_, this);
    }

    /**
     * @brief Block new acquires and wait for all active Guards to be released.
     *
     * Must NOT be called while holding MidiServiceController::lock_ to avoid
     * deadlock when in-flight callbacks re-enter controller APIs.
     */
    void CloseAndDrain();

    static constexpr int32_t DRAIN_TIMEOUT_SEC = 3;

private:
    void OnGuardReleased()
    {
        std::lock_guard<std::mutex> lk(mutex_);
        activeCallbacks_--;
        if (closing_ && activeCallbacks_ == 0) {
            cv_.notify_one();
        }
    }

    std::mutex mutex_;
    std::condition_variable cv_;
    std::shared_ptr<MidiServiceCallback> callback_;
    bool closing_ = false;
    uint32_t activeCallbacks_ = 0;
};

class MidiInServer : public IpcMidiInServerStub {
public:
    MidiInServer(uint32_t id, std::shared_ptr<MidiServiceCallback> callback);
    virtual ~MidiInServer();
    int32_t GetDevices(std::vector<MidiDeviceInfo> &devices) override;
    int32_t GetDevicePorts(int64_t deviceId, std::vector<MidiPortInfo> &ports) override;
    int32_t OpenDevice(int64_t deviceId, MidiDeviceInfo &deviceInfo) override;
    int32_t OpenBleDevice(const std::string &address, const sptr<IRemoteObject> &object) override;
    int32_t CloseDevice(int64_t deviceId) override;
    int32_t OpenInputPort(std::shared_ptr<MidiSharedRing> &buffer, int64_t deviceId, uint32_t portIndex) override;
    int32_t OpenOutputPort(std::shared_ptr<MidiSharedRing> &buffer, int64_t deviceId, uint32_t portIndex) override;
    int32_t FlushOutputPort(int64_t deviceId, uint32_t portIndex) override;
    int32_t CloseInputPort(int64_t deviceId, uint32_t portIndex) override;
    int32_t CloseOutputPort(int64_t deviceId, uint32_t portIndex) override;
    int32_t DestroyMidiClient() override;
    void NotifyDeviceChange(DeviceChangeType change, const MidiDeviceInfo &deviceInfo);
    void NotifyError(int32_t code);
    void UpdateBluetoothPermission(bool useFreshToken = false);
    void ClearCallback(); // BLOCKING: drains in-flight callbacks, do NOT call under controller lock
    void CloseCallbackAndDrain();

    bool IsBluetoothDevice(const MidiDeviceInfo &deviceInfo) const;

private:
    uint32_t clientId_;
    uint32_t callerTokenId_;
    CallbackSlot callbackSlot_;
    bool hasBluetoothPermission_;
};
} // namespace MIDI
} // namespace OHOS
#endif
