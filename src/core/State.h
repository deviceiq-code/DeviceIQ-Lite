#pragma once

#include <Arduino.h>

const char* const STATE_FILE_NAME = "/state.json";
// Mirrors DeviceIQ's Settings.General.SaveStatePooling() (default 20s):
// state.json is flushed on this interval rather than on every change, since
// blind motion alone would otherwise rewrite it on every 1% step.
const uint32_t STATE_SAVE_INTERVAL_MS = 20000;

// Which blind a BlindPosition() call refers to - not the same thing as a
// blind's user-facing Name (Settings.BlindL_Name()/BlindR_Name()), which
// can be renamed and shouldn't ever affect where its position lives.
enum class BlindSlot : uint8_t { Left, Right };

// Frequently-changing runtime state, kept out of config.json on purpose -
// the same split DeviceIQ itself uses between Settings (rarely changes)
// and state.json (rewritten by SaveComponentsState() on a timer).
// Named devicestate/DeviceState, not state/State: Blinds already has a
// State() method of its own (the motion state - opening/closing/stopped),
// and every Blinds method body would otherwise resolve a bare "State" to
// that member instead of this global.
class devicestate {
    private:
        uint8_t mBlindPosition[2] = { 0, 0 };
        bool mDirty = false;
        uint32_t mLastSaveMs = 0;

    public:
        devicestate() {}
        ~devicestate() {}

        inline uint8_t BlindPosition(BlindSlot Slot) { return mBlindPosition[(uint8_t)Slot]; }
        void BlindPosition(BlindSlot Slot, uint8_t Value);

        bool Load(const String& StateFileName = STATE_FILE_NAME);
        bool Save(const String& StateFileName = STATE_FILE_NAME);

        // Flushes to flash at most once per STATE_SAVE_INTERVAL_MS, and only
        // when something actually changed since the last save - call once
        // per loop() iteration, same pattern as Blinds/Timer/Switch Control().
        void Control();
};

extern devicestate DeviceState;
