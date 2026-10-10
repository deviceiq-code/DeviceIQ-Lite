#pragma once

#include <Arduino.h>

#include "Component.h"

// A debounced GPIO input, mechanically identical to Button, but exposed to
// Home Assistant as an "event" entity (device_class "doorbell") instead of a
// binary_sensor - see MQTTClient::PublishDoorBellDiscovery(). That's what
// lets HA's HomeKit Bridge surface a native Doorbell accessory (push
// notification / HomePod chime) without a linked camera. No LongClickTimeMs/
// MultiClickTimeMs like Button has - a doorbell press is a single momentary
// event, not a state Automation could later read click patterns from.
class doorbell final : public component {
    public:
        enum class ActiveLevels : uint8_t { High, Low };
        enum class InputModes : uint8_t { Floating, PullUp, PullDown };

        static constexpr uint32_t DEFAULT_DEBOUNCE_TIME_MS = 50;

        doorbell(
            String name,
            int16_t id,
            uint8_t address,
            ActiveLevels activeLevel = ActiveLevels::Low,
            InputModes inputMode = InputModes::PullUp,
            uint32_t debounceTimeMs = DEFAULT_DEBOUNCE_TIME_MS,
            bool enabled = true
        );
        ~doorbell() override = default;

        [[nodiscard]] Classes Class() const override { return Classes::DoorBell; }
        [[nodiscard]] bool IsPressed() const { return pState; }
        [[nodiscard]] bool WasPressed() const { return pState && pChanged; }
        [[nodiscard]] ActiveLevels ActiveLevel() const { return pActiveLevel; }
        [[nodiscard]] InputModes InputMode() const { return pInputMode; }
        [[nodiscard]] uint32_t DebounceTime() const { return pDebounceTimeMs; }
        void GetInfo(String& output) const override;

    protected:
        bool DoConfigure() override;
        bool DoInitialize() override;
        void EnabledChanged(bool enabled) override;
        void DoControl(uint32_t now) override;

    private:
        [[nodiscard]] bool ReadLevel() const;

        const ActiveLevels pActiveLevel;
        const InputModes pInputMode;
        const uint32_t pDebounceTimeMs;

        bool pState = false;
        bool pChanged = false;
        bool pRawState = false;
        uint32_t pRawChangedAt = 0;
};
