#include "Button.h"

button::button(String name, int16_t id, uint8_t address, ActiveLevels activeLevel, InputModes inputMode, uint32_t debounceTimeMs, uint32_t longClickTimeMs, uint32_t multiClickTimeMs, bool enabled)
    : component(std::move(name), id, address, enabled),
      pActiveLevel(activeLevel),
      pInputMode(inputMode),
      pDebounceTimeMs(debounceTimeMs),
      pLongClickTimeMs(longClickTimeMs),
      pMultiClickTimeMs(multiClickTimeMs) {}

void button::GetInfo(String& output) const {
    component::GetInfo(output);
    output += "State          | " + String(IsPressed() ? "pressed" : "released") + "\r\n";
    output += "ActiveLevel    | " + String(ActiveLevel() == ActiveLevels::High ? "High" : "Low") + "\r\n";
    output += "InputMode      | " + String(InputMode() == InputModes::PullUp ? "PullUp" : InputMode() == InputModes::PullDown ? "PullDown" : "Floating") + "\r\n";
    output += "DebounceTimeMs | " + String(DebounceTime()) + "\r\n";
}

bool button::DoConfigure() {
    switch(pInputMode) {
        case InputModes::PullUp:
            pinMode(Address(), INPUT_PULLUP);
            return true;
        case InputModes::PullDown:
            // ESP8266 only has an internal pulldown on GPIO16 (D0).
            if(Address() != 16) return false;
            pinMode(Address(), INPUT_PULLDOWN_16);
            return true;
        case InputModes::Floating:
        default:
            pinMode(Address(), INPUT);
            return true;
    }
}

bool button::DoInitialize() {
    pState = ReadLevel();
    pRawState = pState;
    pRawChangedAt = millis();
    pChanged = false;
    return true;
}

void button::EnabledChanged(bool enabled) {
    if(enabled) {
        pState = ReadLevel();
        pRawState = pState;
        pRawChangedAt = millis();
    } else {
        pState = false;
        pRawState = false;
    }
    pChanged = false;
}

void button::DoControl(uint32_t now) {
    const bool raw = ReadLevel();
    pChanged = false;

    if(raw != pRawState) {
        pRawState = raw;
        pRawChangedAt = now;
    }

    if(pRawState != pState && (uint32_t)(now - pRawChangedAt) >= pDebounceTimeMs) {
        pState = pRawState;
        pChanged = true;
    }
}

bool button::ReadLevel() const {
    const bool level = digitalRead(Address()) == HIGH;
    return pActiveLevel == ActiveLevels::High ? level : !level;
}
