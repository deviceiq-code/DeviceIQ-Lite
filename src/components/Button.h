#pragma once

#include <Arduino.h>

#include "Component.h"

// A debounced GPIO input. Port of DeviceIQ's button component, scoped down
// to press/release edge detection - Lite has no Automation engine yet to
// consume long-click/multi-click events, so LongClickTimeMs/MultiClickTimeMs
// are accepted in Setup (for schema parity) but not acted on.
class button final : public component {
    public:
        enum class ActiveLevels : uint8_t { High, Low };
        enum class InputModes : uint8_t { Floating, PullUp, PullDown };

        static constexpr uint32_t DEFAULT_DEBOUNCE_TIME_MS = 50;
        static constexpr uint32_t DEFAULT_LONG_CLICK_TIME_MS = 1000;
        static constexpr uint32_t DEFAULT_MULTI_CLICK_TIME_MS = 400;

        button(
            String name,
            int16_t id,
            uint8_t address,
            ActiveLevels activeLevel = ActiveLevels::Low,
            InputModes inputMode = InputModes::PullUp,
            uint32_t debounceTimeMs = DEFAULT_DEBOUNCE_TIME_MS,
            uint32_t longClickTimeMs = DEFAULT_LONG_CLICK_TIME_MS,
            uint32_t multiClickTimeMs = DEFAULT_MULTI_CLICK_TIME_MS,
            bool enabled = true
        );
        ~button() override = default;

        [[nodiscard]] Classes Class() const override { return Classes::Button; }
        [[nodiscard]] bool IsPressed() const { return pState; }
        [[nodiscard]] bool IsReleased() const { return !pState; }
        [[nodiscard]] bool WasPressed() const { return pState && pChanged; }
        [[nodiscard]] bool WasReleased() const { return !pState && pChanged; }
        [[nodiscard]] ActiveLevels ActiveLevel() const { return pActiveLevel; }
        [[nodiscard]] InputModes InputMode() const { return pInputMode; }
        [[nodiscard]] uint32_t DebounceTime() const { return pDebounceTimeMs; }
        [[nodiscard]] uint32_t LongClickTime() const { return pLongClickTimeMs; }
        [[nodiscard]] uint32_t MultiClickTime() const { return pMultiClickTimeMs; }
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
        const uint32_t pLongClickTimeMs;
        const uint32_t pMultiClickTimeMs;

        bool pState = false;
        bool pChanged = false;
        bool pRawState = false;
        uint32_t pRawChangedAt = 0;
};
