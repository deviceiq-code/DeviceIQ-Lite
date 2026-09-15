#pragma once

#include <Arduino.h>

#include "Component.h"
#include "Relay.h"
#include "Button.h"

// A motorized blind, composed from two Relay components (open/close) and up
// to two Button components (open/close), referenced by ID from config.json.
// Keeps Lite's original single-StepTimeMs motion model rather than
// DeviceIQ's separate open/close step times, correction factors and endstop
// margin - a deliberate simplification.
class blinds final : public component {
    public:
        enum class Motion : uint8_t { Stopped, Opening, Closing };

        static constexpr uint8_t MAX_POSITION = 100;
        static constexpr uint16_t DEFAULT_STEP_TIME_MS = 250;

        blinds(
            String name,
            int16_t id,
            relay& relayOpen,
            relay& relayClose,
            button* buttonOpen,
            button* buttonClose,
            uint8_t initialPosition = 0,
            uint16_t stepTimeMs = DEFAULT_STEP_TIME_MS,
            bool buttonOpenEnabled = true,
            bool buttonCloseEnabled = true,
            bool invertButtons = false,
            bool enabled = true
        );
        ~blinds() override = default;

        [[nodiscard]] Classes Class() const override { return Classes::Blinds; }
        [[nodiscard]] bool HasPersistentState() const override { return true; }
        [[nodiscard]] Motion State() const { return pMotion; }
        [[nodiscard]] uint8_t Position() const { return pPosition; }
        [[nodiscard]] uint8_t TargetPosition() const { return pTargetPosition; }
        [[nodiscard]] uint16_t StepTime() const { return pStepTimeMs; }
        [[nodiscard]] bool ButtonOpenEnabled() const { return pButtonOpenEnabled; }
        [[nodiscard]] bool ButtonCloseEnabled() const { return pButtonCloseEnabled; }
        [[nodiscard]] bool InvertButtons() const { return pInvertButtons; }
        [[nodiscard]] const relay& RelayOpen() const { return pRelayOpen; }
        [[nodiscard]] const relay& RelayClose() const { return pRelayClose; }
        [[nodiscard]] const button* ButtonOpen() const { return pButtonOpen; }
        [[nodiscard]] const button* ButtonClose() const { return pButtonClose; }

        void StepTime(uint16_t value) { pStepTimeMs = value; }
        void ButtonOpenEnabled(bool value) { pButtonOpenEnabled = value; }
        void ButtonCloseEnabled(bool value) { pButtonCloseEnabled = value; }
        void InvertButtons(bool value) { pInvertButtons = value; }

        void SetPosition(uint8_t value);
        void Open() { SetPosition(MAX_POSITION); }
        void Close() { SetPosition(0); }
        void Stop();
        void GetInfo(String& output) const override;
        [[nodiscard]] static const char* StateName(Motion value);

    protected:
        void DoControl(uint32_t now) override;

    private:
        void HandleButtons();
        [[nodiscard]] button* EffectiveButtonOpen() const { return pInvertButtons ? pButtonClose : pButtonOpen; }
        [[nodiscard]] button* EffectiveButtonClose() const { return pInvertButtons ? pButtonOpen : pButtonClose; }

        relay& pRelayOpen;
        relay& pRelayClose;
        button* const pButtonOpen;
        button* const pButtonClose;

        uint8_t pPosition;
        uint8_t pTargetPosition;
        uint16_t pStepTimeMs;
        Motion pMotion = Motion::Stopped;
        bool pButtonOpenEnabled;
        bool pButtonCloseEnabled;
        bool pInvertButtons;
        uint32_t pLastStepAt = 0;
};
