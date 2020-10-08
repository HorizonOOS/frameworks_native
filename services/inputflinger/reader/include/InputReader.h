/*
 * Copyright (C) 2010 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _UI_INPUTREADER_INPUT_READER_H
#define _UI_INPUTREADER_INPUT_READER_H

#include <PointerControllerInterface.h>
#include <utils/Condition.h>
#include <utils/Mutex.h>

#include <memory>
#include <unordered_map>
#include <vector>

#include "EventHub.h"
#include "InputListener.h"
#include "InputReaderBase.h"
#include "InputReaderContext.h"
#include "InputThread.h"

namespace android {

class InputDevice;
class InputMapper;
struct StylusState;

/* The input reader reads raw event data from the event hub and processes it into input events
 * that it sends to the input listener.  Some functions of the input reader, such as early
 * event filtering in low power states, are controlled by a separate policy object.
 *
 * The InputReader owns a collection of InputMappers. InputReader starts its own thread, where
 * most of the work happens, but the InputReader can receive queries from other system
 * components running on arbitrary threads.  To keep things manageable, the InputReader
 * uses a single Mutex to guard its state.  The Mutex may be held while calling into the
 * EventHub or the InputReaderPolicy but it is never held while calling into the
 * InputListener. All calls to InputListener must happen from InputReader's thread.
 */
class InputReader : public InputReaderInterface {
public:
    InputReader(std::shared_ptr<EventHubInterface> eventHub,
                const sp<InputReaderPolicyInterface>& policy,
                const sp<InputListenerInterface>& listener);
    virtual ~InputReader();

    void dump(std::string& dump) override;
    void monitor() override;

    status_t start() override;
    status_t stop() override;

    std::vector<InputDeviceInfo> getInputDevices() const override;

    bool isInputDeviceEnabled(int32_t deviceId) override;

    int32_t getScanCodeState(int32_t deviceId, uint32_t sourceMask, int32_t scanCode) override;
    int32_t getKeyCodeState(int32_t deviceId, uint32_t sourceMask, int32_t keyCode) override;
    int32_t getSwitchState(int32_t deviceId, uint32_t sourceMask, int32_t sw) override;

    void toggleCapsLockState(int32_t deviceId) override;

    bool hasKeys(int32_t deviceId, uint32_t sourceMask, size_t numCodes, const int32_t* keyCodes,
                 uint8_t* outFlags) override;

    void requestRefreshConfiguration(uint32_t changes) override;

    void vibrate(int32_t deviceId, const std::vector<VibrationElement>& pattern, ssize_t repeat,
                 int32_t token) override;
    void cancelVibrate(int32_t deviceId, int32_t token) override;

    bool canDispatchToDisplay(int32_t deviceId, int32_t displayId) override;

protected:
    // These members are protected so they can be instrumented by test cases.
    virtual std::shared_ptr<InputDevice> createDeviceLocked(
            int32_t deviceId, const InputDeviceIdentifier& identifier);

    // With each iteration of the loop, InputReader reads and processes one incoming message from
    // the EventHub.
    void loopOnce();

    class ContextImpl : public InputReaderContext {
        InputReader* mReader;
        IdGenerator mIdGenerator;

    public:
        explicit ContextImpl(InputReader* reader);

        void updateGlobalMetaState() override;
        int32_t getGlobalMetaState() override;
        void disableVirtualKeysUntil(nsecs_t time) override;
        bool shouldDropVirtualKey(nsecs_t now, int32_t keyCode, int32_t scanCode) override;
        void fadePointer() override;
        std::shared_ptr<PointerControllerInterface> getPointerController(int32_t deviceId) override;
        void requestTimeoutAtTime(nsecs_t when) override;
        int32_t bumpGeneration() override;
        void getExternalStylusDevices(std::vector<InputDeviceInfo>& outDevices) override;
        void dispatchExternalStylusState(const StylusState& outState) override;
        InputReaderPolicyInterface* getPolicy() override;
        InputListenerInterface* getListener() override;
        EventHubInterface* getEventHub() override;
        int32_t getNextId() override;
        void updateLedMetaState(int32_t metaState) override;
        int32_t getLedMetaState() override;
    } mContext;

    friend class ContextImpl;

private:
    std::unique_ptr<InputThread> mThread;

    mutable Mutex mLock;

    Condition mReaderIsAliveCondition;

    // This could be unique_ptr, but due to the way InputReader tests are written,
    // it is made shared_ptr here. In the tests, an EventHub reference is retained by the test
    // in parallel to passing it to the InputReader.
    std::shared_ptr<EventHubInterface> mEventHub;
    sp<InputReaderPolicyInterface> mPolicy;
    sp<QueuedInputListener> mQueuedListener;

    InputReaderConfiguration mConfig;

    // The event queue.
    static const int EVENT_BUFFER_SIZE = 256;
    RawEvent mEventBuffer[EVENT_BUFFER_SIZE];

    // An input device can represent a collection of EventHub devices. This map provides a way
    // to lookup the input device instance from the EventHub device id.
    std::unordered_map<int32_t /*eventHubId*/, std::shared_ptr<InputDevice>> mDevices;

    // An input device contains one or more eventHubId, this map provides a way to lookup the
    // EventHubIds contained in the input device from the input device instance.
    std::unordered_map<std::shared_ptr<InputDevice>, std::vector<int32_t> /*eventHubId*/>
            mDeviceToEventHubIdsMap;

    // low-level input event decoding and device management
    void processEventsLocked(const RawEvent* rawEvents, size_t count);

    void addDeviceLocked(nsecs_t when, int32_t eventHubId);
    void removeDeviceLocked(nsecs_t when, int32_t eventHubId);
    void processEventsForDeviceLocked(int32_t eventHubId, const RawEvent* rawEvents, size_t count);
    void timeoutExpiredLocked(nsecs_t when);

    void handleConfigurationChangedLocked(nsecs_t when);

    int32_t mGlobalMetaState;
    void updateGlobalMetaStateLocked();
    int32_t getGlobalMetaStateLocked();

    int32_t mLedMetaState;
    void updateLedMetaStateLocked(int32_t metaState);
    int32_t getLedMetaStateLocked();

    void notifyExternalStylusPresenceChanged();
    void getExternalStylusDevicesLocked(std::vector<InputDeviceInfo>& outDevices);
    void dispatchExternalStylusState(const StylusState& state);

    // The PointerController that is shared among all the input devices that need it.
    std::weak_ptr<PointerControllerInterface> mPointerController;
    std::shared_ptr<PointerControllerInterface> getPointerControllerLocked(int32_t deviceId);
    void updatePointerDisplayLocked();
    void fadePointerLocked();

    int32_t mGeneration;
    int32_t bumpGenerationLocked();

    int32_t mNextInputDeviceId;
    int32_t nextInputDeviceIdLocked();

    std::vector<InputDeviceInfo> getInputDevicesLocked() const;

    nsecs_t mDisableVirtualKeysTimeout;
    void disableVirtualKeysUntilLocked(nsecs_t time);
    bool shouldDropVirtualKeyLocked(nsecs_t now, int32_t keyCode, int32_t scanCode);

    nsecs_t mNextTimeout;
    void requestTimeoutAtTimeLocked(nsecs_t when);

    uint32_t mConfigurationChangesToRefresh;
    void refreshConfigurationLocked(uint32_t changes);

    // state queries
    typedef int32_t (InputDevice::*GetStateFunc)(uint32_t sourceMask, int32_t code);
    int32_t getStateLocked(int32_t deviceId, uint32_t sourceMask, int32_t code,
                           GetStateFunc getStateFunc);
    bool markSupportedKeyCodesLocked(int32_t deviceId, uint32_t sourceMask, size_t numCodes,
                                     const int32_t* keyCodes, uint8_t* outFlags);

    // find an InputDevice from an InputDevice id
    InputDevice* findInputDevice(int32_t deviceId);
};

} // namespace android

#endif // _UI_INPUTREADER_INPUT_READER_H
