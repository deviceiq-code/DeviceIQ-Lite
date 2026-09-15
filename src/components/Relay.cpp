#include "Relay.h"

relay::relay(String name, int16_t id, uint8_t address, RelayTypes type, DriveModes driveMode, bool initialState, bool enabled, uint32_t pulseTimeMs)
    : component(std::move(name), id, address, enabled), pState(initialState), pType(type), pDriveMode(driveMode), pInitialState(initialState), pPulseTimeMs(pulseTimeMs) {}

void relay::SetState(bool newState) {
    if(newState == pState) return;

    WriteOutput(newState);
    pState = newState;
    MarkStateChanged();
    if(newState && pPulseTimeMs > 0) {
        pPulseStartedAt = millis();
        pPulsePending = true;
    } else {
        pPulsePending = false;
    }
}

void relay::GetInfo(String& output) const {
    component::GetInfo(output);
    output += "State       | " + String(State() ? "on" : "off") + "\r\n";
    output += "RelayType   | " + String(Type() == RelayTypes::NormallyOpen ? "NormallyOpen" : "NormallyClosed") + "\r\n";
    output += "DriveMode   | " + String(DriveMode() == DriveModes::ActiveHigh ? "ActiveHigh" : "ActiveLow") + "\r\n";
    if(pPulseTimeMs > 0) output += "PulseTimeMs | " + String(pPulseTimeMs) + "\r\n";
}

bool relay::DoConfigure() {
    const bool startupState = Enabled() ? pInitialState : false;
    digitalWrite(Address(), ElectricalLevel(startupState) ? HIGH : LOW);
    pinMode(Address(), OUTPUT);
    return true;
}

bool relay::DoInitialize() {
    WriteOutput(Enabled() ? pInitialState : false);
    pState = Enabled() ? pInitialState : false;
    return true;
}

void relay::EnabledChanged(bool enabled) {
    if(!enabled) SetState(false);
}

void relay::DoControl(uint32_t now) {
    if(!pPulsePending) return;
    if((uint32_t)(now - pPulseStartedAt) >= pPulseTimeMs) SetState(false);
}

bool relay::ElectricalLevel(bool logicalState) const {
    return pDriveMode == DriveModes::ActiveHigh ? logicalState : !logicalState;
}

void relay::WriteOutput(bool logicalState) {
    digitalWrite(Address(), ElectricalLevel(logicalState) ? HIGH : LOW);
}
