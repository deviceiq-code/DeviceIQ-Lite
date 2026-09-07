#include "Switch.h"
#include <EEPROM.h>

Switch::Switch(uint8_t pin) : Name(DEF_Switch_Name), Type(DEF_SwitchType), DefaultState(DEF_SwitchState), SaveState(DEF_Switch_SaveState), StatelessDelay(DEF_StatelessDelay) {
    Pin(pin);
}

Switch::Switch(String name, uint8_t pin) : Name(name), Type(DEF_SwitchType), DefaultState(DEF_SwitchState), SaveState(DEF_Switch_SaveState), StatelessDelay(DEF_StatelessDelay) {
    Pin(pin);
}

void Switch::Pin(uint8_t NewPin) {
    EEPROM.begin(512);

    _Pin = NewPin;
    if(std::find(std::begin(DEF_Switch_Special_Pins), std::end(DEF_Switch_Special_Pins), _Pin) != std::end(DEF_Switch_Special_Pins)) pinMode(_Pin, FUNCTION_3);
    pinMode(_Pin, OUTPUT);

    _cancel_delay_timeout = false;
    _cancel_delay_reset = false;

    if(SaveState) {
        _State = (Switch_State)EEPROM.read(EEPROMSaveStateAddress);
        SetState(_State);
    } else {
        SetState(DefaultState);
    }
}

void Switch::SetState(Switch_State NewState) {
    _State = NewState;
    
    if(Type == Switch_Type::Stateless) {
        if(mStatelessDelayResetCallback) mStatelessDelayResetCallback();

        if(_cancel_delay_reset == false) {
            _LastState = _State; // DefaultState
            _time = millis();
            _changed = false;
            _last_change = _time;
        } else {
            _cancel_delay_reset = false;
        }
    }

    SetPin();
}

void Switch::InvertState() {
    _State = (_State == Switch_State::On ? Switch_State::Off : Switch_State::On);
    SetPin();
}

void Switch::Control() {
    if(_last_change == 0) _last_change = millis();

    if(Type == Switch_Type::Stateless) {
        if(_State == Switch_State::On) {
            uint32_t read_started_ms = millis();

            if(read_started_ms - _last_change < StatelessDelay) {
                _changed = false;
                _Timer = 1 + (read_started_ms - _last_change);
            } else {
                _changed = true;
                //_Timer = 0;
                
                if(mStatelessDelayTimeoutCallback) mStatelessDelayTimeoutCallback();

                if(_cancel_delay_timeout == false) {
                    _State = (_State == Switch_State::On ? Switch_State::Off : Switch_State::On);
                    SetPin();
                
                    _last_state = _current_state;
                    _current_state = (bool)_State;
                    if(_changed) _last_change = read_started_ms;
                } else {
                    _cancel_delay_timeout = false;
                }
            }
        }
    }
    
    if(_LastState != _State) {
        _LastState = _State;

        if(_State == Switch_State::On) {
            if(mStateOnCallback) mStateOnCallback();
            return;
        }
        if(_State == Switch_State::Off) {
            if(mStateOffCallback) mStateOffCallback();
            return;
        }
    }    
}

void Switch::SetPin() {
    digitalWrite(_Pin, _State);
    if(SaveState) { EEPROM.write(EEPROMSaveStateAddress, (byte)_State); EEPROM.commit(); }
    Control();
}

