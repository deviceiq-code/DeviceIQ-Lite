#pragma once

#include <Arduino.h>

#include "Component.h"

// A GPIO-driven relay (or any simple on/off output). Port of DeviceIQ's
// relay component, minus the I2C/Pcf8574 output path - Lite only drives
// onboard GPIO.
class relay final : public component {
    public:
        enum class RelayTypes : uint8_t { NormallyOpen, NormallyClosed };
        enum class DriveModes : uint8_t { ActiveHigh, ActiveLow };

        static constexpr uint32_t MIN_PULSE_TIME_MS = 20;
        static constexpr uint32_t MAX_PULSE_TIME_MS = 5000;

        relay(
            String name,
            int16_t id,
            uint8_t address,
            RelayTypes type = RelayTypes::NormallyOpen,
            DriveModes driveMode = DriveModes::ActiveHigh,
            bool initialState = false,
            bool enabled = true,
            uint32_t pulseTimeMs = 0
        );
        ~relay() override = default;

        [[nodiscard]] Classes Class() const override { return Classes::Relay; }
        // A momentary relay is never meaningfully "on" at rest - Control()
        // always releases it within PulseTimeMs - so persisting it would
        // only risk latching it on at boot.
        [[nodiscard]] bool HasPersistentState() const override { return pPulseTimeMs == 0; }
        [[nodiscard]] bool State() const { return pState; }
        [[nodiscard]] RelayTypes Type() const { return pType; }
        [[nodiscard]] DriveModes DriveMode() const { return pDriveMode; }
        [[nodiscard]] uint32_t PulseTime() const { return pPulseTimeMs; }

        void SetState(bool newState);
        void Toggle() { SetState(!pState); }
        void GetInfo(String& output) const override;

    protected:
        bool DoConfigure() override;
        bool DoInitialize() override;
        void EnabledChanged(bool enabled) override;
        void DoControl(uint32_t now) override;

    private:
        [[nodiscard]] bool ElectricalLevel(bool logicalState) const;
        void WriteOutput(bool logicalState);

        bool pState;
        const RelayTypes pType;
        const DriveModes pDriveMode;
        const bool pInitialState;
        const uint32_t pPulseTimeMs;
        uint32_t pPulseStartedAt = 0;
        bool pPulsePending = false;
};
