#ifndef Blinds_h
#define Blinds_h

#include <Arduino.h>

#include "core/Logger.h"
#include "Button.h"
#include "Switch.h"
#include "Timer.h"
#include "core/State.h"

enum Blinds_State { Stopped = 2, Opening = 1, Closing = 0 };

// Lowercase, matching DeviceIQ's own Blinds component state strings.
// Shared by /api/blinds, the webhooks endpoint, and (in a position-aware
// form of its own) MQTT discovery - one canonical name per motion state.
inline const char* BlindStateName(Blinds_State state) {
    switch(state) {
        case Blinds_State::Opening: return "opening";
        case Blinds_State::Closing: return "closing";
        default: return "stopped";
    }
}

const String DEF_Blind_Name = "DefaultBlinds";
const uint8_t DEF_Max_Position = 100;
const uint16_t DEF_Step_Ms = 250;

class Blinds {
    private:
        volatile uint8_t mPosition, mNewPosition;
        uint8_t mPinButtonOpen = 0, mPinButtonClose = 0, mPinSwitchOpen = 0, mPinSwitchClose = 0;
        uint8_t mTmpPinButtonOpen = 0, mTmpPinButtonClose = 0;
        volatile uint16_t mStep_Ms = DEF_Step_Ms;

        Button *ButtonOpen, *ButtonClose;
        Switch *SwitchOpen, *SwitchClose;
        Timer *TimerOpen, *TimerClose;

        Blinds_State mState = Blinds_State::Stopped;

        bool mButtonOpenEnabled = true;
        bool mButtonCloseEnabled = true;
        bool mInvertButtons = false;

        BlindSlot mSlot;

        void Initialize();

    public:
        Blinds(uint8_t pinButtonOpen, uint8_t pinButtonClose, uint8_t pinSwitchOpen, uint8_t pinSwitchClose, BlindSlot slot);
        Blinds(String name, uint8_t pinButtonOpen, uint8_t pinButtonClose, uint8_t pinSwitchOpen, uint8_t pinSwitchClose, BlindSlot slot);
        ~Blinds() {}

        // Properties
        String Name = "";
        inline void Step_Ms(uint16_t value) { mStep_Ms = value; TimerOpen->SetTimeout(mStep_Ms); TimerClose->SetTimeout(mStep_Ms); }
        inline uint16_t Step_Ms() { return mStep_Ms; }
        inline bool ButtonOpenEnabled() { return mButtonOpenEnabled; }
        inline void ButtonOpenEnabled(bool Value) { mButtonOpenEnabled = Value; }
        inline bool ButtonCloseEnabled() { return mButtonCloseEnabled; }
        inline void ButtonCloseEnabled(bool Value) { mButtonCloseEnabled = Value; }
        inline bool InvertButtons() { return mInvertButtons; }
        void InvertButtons(bool Value);
        
        // Methods
        void Control();
        void Position(uint8_t value);
        inline uint8_t Position() { return mPosition; }
        inline uint8_t TargetPosition() { return mNewPosition; }
        inline Blinds_State State() { return mState; }
        inline void Open() { mState = Blinds_State::Opening; Position(DEF_Max_Position); }
        inline void Close() { mState = Blinds_State::Closing; Position(0); }
        inline void Stop() { TimerOpen->Stop(); TimerClose->Stop(); SwitchOpen->SetState(Switch_State::Off); SwitchClose->SetState(Switch_State::Off); mNewPosition = mPosition; mState = Blinds_State::Stopped; }
};

#endif