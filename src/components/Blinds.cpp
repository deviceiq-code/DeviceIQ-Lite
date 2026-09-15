#include "Blinds.h"

blinds::blinds(
    String name, int16_t id,
    relay& relayOpen, relay& relayClose,
    button* buttonOpen, button* buttonClose,
    uint8_t initialPosition, uint16_t stepTimeMs,
    bool buttonOpenEnabled, bool buttonCloseEnabled, bool invertButtons, bool enabled
) :
    component(std::move(name), id, 0, enabled),
    pRelayOpen(relayOpen), pRelayClose(relayClose),
    pButtonOpen(buttonOpen), pButtonClose(buttonClose),
    pPosition(initialPosition), pTargetPosition(initialPosition),
    pStepTimeMs(stepTimeMs),
    pButtonOpenEnabled(buttonOpenEnabled), pButtonCloseEnabled(buttonCloseEnabled), pInvertButtons(invertButtons) {}

void blinds::GetInfo(String& output) const {
    component::GetInfo(output);
    output += "State       | " + String(StateName(State())) + "\r\n";
    output += "Position    | " + String(Position()) + "\r\n";
    output += "StepTimeMs  | " + String(StepTime()) + "\r\n";
}

const char* blinds::StateName(Motion value) {
    switch(value) {
        case Motion::Opening: return "opening";
        case Motion::Closing: return "closing";
        default: return "stopped";
    }
}

void blinds::SetPosition(uint8_t value) {
    value = constrain(value, (uint8_t)0, MAX_POSITION);
    if(value == pPosition) { Stop(); return; }

    pTargetPosition = value;
    if(value > pPosition) {
        pMotion = Motion::Opening;
        pRelayClose.SetState(false);
        pRelayOpen.SetState(true);
    } else {
        pMotion = Motion::Closing;
        pRelayOpen.SetState(false);
        pRelayClose.SetState(true);
    }
    pLastStepAt = millis();
}

void blinds::Stop() {
    pRelayOpen.SetState(false);
    pRelayClose.SetState(false);
    pTargetPosition = pPosition;
    pMotion = Motion::Stopped;
}

void blinds::HandleButtons() {
    button* openButton = EffectiveButtonOpen();
    button* closeButton = EffectiveButtonClose();

    if(pButtonOpenEnabled && openButton != nullptr && openButton->WasPressed()) {
        if(pMotion == Motion::Stopped) SetPosition(MAX_POSITION);
        else Stop();
    }

    if(pButtonCloseEnabled && closeButton != nullptr && closeButton->WasPressed()) {
        if(pMotion == Motion::Stopped) SetPosition(0);
        else Stop();
    }
}

void blinds::DoControl(uint32_t now) {
    HandleButtons();

    if(pMotion == Motion::Stopped) return;
    if((uint32_t)(now - pLastStepAt) < pStepTimeMs) return;
    pLastStepAt = now;

    if(pMotion == Motion::Opening) {
        if(pPosition < pTargetPosition) pPosition++;
        if(pPosition >= pTargetPosition) Stop();
    } else {
        if(pPosition > pTargetPosition) pPosition--;
        if(pPosition <= pTargetPosition) Stop();
    }

    // Written to state.json at most once every STATE_SAVE_INTERVAL_MS by
    // loop(), not on every step - see settings::SaveComponentsState().
    MarkStateChanged();
}
