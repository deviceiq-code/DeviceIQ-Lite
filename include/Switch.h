#ifndef Switch_h
#define Switch_h

#include <Arduino.h>

enum Switch_Type { Simple = 0, Stateless = 1 };
enum Switch_State { On = LOW, Off = HIGH };
//enum Switch_State { On = HIGH, Off = LOW };

const String DEF_Switch_Name = "DefaultSwitch";
const Switch_Type DEF_SwitchType = Switch_Type::Simple;
const Switch_State DEF_SwitchState = Switch_State::Off;
const bool DEF_Switch_SaveState = false;
const uint32_t DEF_Switch_EEPROMSaveStateAddress = 0;
const uint32_t DEF_StatelessDelay = 100;

const uint8_t DEF_Switch_Special_Pins[] = { 1, 3, 10 };

class Switch {
    private:
        typedef std::function<void()> callback_t;
        
        uint32_t _Timer = 0;
        uint8_t _Pin;
        Switch_State _State;
        Switch_State _LastState;
        bool _current_state;
	    bool _last_state;
        bool _changed;
        bool _cancel_delay_timeout;
        bool _cancel_delay_reset;
        uint32_t _time;
        uint32_t _last_change;
        callback_t mStateOnCallback;
        callback_t mStateOffCallback;
        callback_t mStatelessDelayResetCallback;
        callback_t mStatelessDelayTimeoutCallback;
        void SetPin();
        
    public:
        Switch(uint8_t pin);
        Switch(String name, uint8_t pin);
        ~Switch() {}
        
        // Properties
        String Name;
        Switch_Type Type;
        Switch_State DefaultState;
        bool SaveState;
        uint32_t StatelessDelay;
        uint32_t EEPROMSaveStateAddress = 0;

        inline uint8_t Pin() { return _Pin; }
        void Pin(uint8_t NewPin);
        inline Switch_State State() { return _State; }

        // Methods
        void Control();
        void SetState(Switch_State NewState);
        void InvertState();
        inline void CancelDelayReset() { _cancel_delay_reset = true; }
        inline void CancelDelayTimeout() { _cancel_delay_timeout = true; }
        inline uint32_t Timer() { return _Timer; }

        // Events
        inline void OnStateOn(callback_t callback) { mStateOnCallback = callback; }
        inline void OnStateOff(callback_t callback) { mStateOffCallback = callback; }
        inline void OnStatelessDelayReset(callback_t callback) { mStatelessDelayResetCallback = callback; }
        inline void OnStatelessDelayTimeout(callback_t callback) { mStatelessDelayTimeoutCallback = callback; }
};

#endif