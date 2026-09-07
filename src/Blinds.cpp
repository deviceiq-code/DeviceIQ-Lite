#include "Blinds.h"

Blinds::Blinds(uint8_t pinButtonOpen, uint8_t pinButtonClose, uint8_t pinSwitchOpen, uint8_t pinSwitchClose, uint32_t EEPROMAddress) : mPinButtonOpen(pinButtonOpen), mPinButtonClose(pinButtonClose), mPinSwitchOpen(pinSwitchOpen), mPinSwitchClose(pinSwitchClose), Name(DEF_Blind_Name), EEPROMSaveStateAddress(EEPROMAddress) {
    Initialize();
}

Blinds::Blinds(String name, uint8_t pinButtonOpen, uint8_t pinButtonClose, uint8_t pinSwitchOpen, uint8_t pinSwitchClose, uint32_t EEPROMAddress) : mPinButtonOpen(pinButtonOpen), mPinButtonClose(pinButtonClose), mPinSwitchOpen(pinSwitchOpen), mPinSwitchClose(pinSwitchClose), Name(name), EEPROMSaveStateAddress(EEPROMAddress) {
    Initialize();
}

void Blinds::Control() {
    ButtonOpen->Control(); ButtonClose->Control();
    SwitchOpen->Control(); SwitchClose->Control();
    TimerOpen->Control(); TimerClose->Control();
}

void Blinds::Position(uint8_t value) {
    if(mState != Blinds_State::Stopped) {
        SwitchOpen->SetState(Switch_State::Off);
        SwitchClose->SetState(Switch_State::Off);

        TimerOpen->Stop();
        TimerClose->Stop();
    }

    if(value > mPosition) { // Opening
        SwitchClose->SetState(Switch_State::Off);
        mNewPosition = constrain(value, 0, DEF_Max_Position);
        
        TimerOpen->Start();
        TimerClose->Stop();
    }
    
    if(value < mPosition) { // Closing
        SwitchOpen->SetState(Switch_State::Off);
        mNewPosition = constrain(value, 0, DEF_Max_Position);

        TimerOpen->Stop();
        TimerClose->Start();
        mState = Blinds_State::Closing;
    }
}

void Blinds::InvertButtons(bool Value) {
    mInvertButtons = Value;

    if(mInvertButtons == true) {
        mPinButtonOpen = mTmpPinButtonClose;
        mPinButtonClose = mTmpPinButtonOpen;        
    } else {
        mPinButtonOpen = mTmpPinButtonOpen;
        mPinButtonClose = mTmpPinButtonClose;
    }

    ButtonOpen->Pin(mPinButtonOpen);
    ButtonClose->Pin(mPinButtonClose);
}

void Blinds::Initialize() {
    EEPROM.begin(4096);

    mPosition = EEPROM.read(EEPROMSaveStateAddress);
    if(mPosition > DEF_Max_Position) mPosition = DEF_Max_Position;

    mNewPosition = 0;

    mState = Blinds_State::Stopped;

    // Setting Switch Open and Close
    SwitchOpen = new Switch(Name + "-SwitchOpen", mPinSwitchOpen);
    SwitchClose = new Switch(Name + "-SwitchClose", mPinSwitchClose);

    SwitchOpen->SaveState = false; 
    SwitchClose->SaveState = false;

    SwitchOpen->Type = Switch_Type::Simple;
    SwitchClose->Type = Switch_Type::Simple;

    SwitchOpen->OnStateOn([&] { mState = Blinds_State::Opening; Logger.Write("Switch 'Open' is ON"); });
    SwitchOpen->OnStateOff([&] { mState = Blinds_State::Stopped; Logger.Write("Switch 'Open' is OFF"); });
    SwitchClose->OnStateOn([&] { mState = Blinds_State::Closing; Logger.Write("Switch 'Close' is ON");});
    SwitchClose->OnStateOff([&] { mState = Blinds_State::Stopped; Logger.Write("Switch 'Close' is Off");});

    // Setting Timer Open and Close
    TimerOpen = new Timer(mStep_Ms);
    TimerClose = new Timer(mStep_Ms);

    TimerOpen->OnTimeout([&] {
        if(mPosition < mNewPosition) { mPosition++; SwitchOpen->SetState(Switch_State::On); SwitchClose->SetState(Switch_State::Off); } else { TimerOpen->Stop(); SwitchOpen->SetState(Switch_State::Off); }
        EEPROM.write(EEPROMSaveStateAddress, mPosition); EEPROM.commit();
        Logger.Write(Name + "-Position: " + String(mPosition));
    });

    TimerClose->OnTimeout([&] {
        if(mPosition > mNewPosition ) { mPosition--; SwitchOpen->SetState(Switch_State::Off); SwitchClose->SetState(Switch_State::On); } else { TimerClose->Stop(); SwitchClose->SetState(Switch_State::Off); }
        EEPROM.write(EEPROMSaveStateAddress, mPosition); EEPROM.commit();
        Logger.Write(Name + "-Position: " + String(mPosition));
    });

    // Setting Button Open and Close
    ButtonOpen = new Button(Name + "-ButtonOpen", mPinButtonOpen);
    ButtonClose = new Button(Name + "-ButtonClose", mPinButtonClose);

    mTmpPinButtonClose = mPinButtonClose;
    mTmpPinButtonOpen = mPinButtonOpen;

    ButtonOpen->OnPressed([&] {
        Logger.Write(ButtonOpen->Name + " pressed");

        if(mButtonOpenEnabled == false) return;

        if(TimerClose->State() == Timer_State::TimerRunning) {
            SwitchClose->SetState(Switch_State::Off);
            TimerClose->Stop();
            return;
        }

        switch(TimerOpen->State()) {
            case Timer_State::TimerStopped:
                mNewPosition = DEF_Max_Position;

                SwitchClose->SetState(Switch_State::Off);
                SwitchOpen->SetState(Switch_State::On);

                TimerClose->Stop();
                TimerOpen->Start();
                break;
                    
            case Timer_State::TimerRunning:
                SwitchClose->SetState(Switch_State::Off);
                SwitchOpen->SetState(Switch_State::Off);

                TimerClose->Stop();
                TimerOpen->Stop();
                break;
        }
    });

    ButtonClose->OnPressed([&] {
        Logger.Write(ButtonClose->Name + " pressed");

        if(mButtonCloseEnabled == false) return;

        if(TimerOpen->State() == Timer_State::TimerRunning) {
            SwitchOpen->SetState(Switch_State::Off);
            TimerOpen->Stop();
            return;
        }

        switch(TimerClose->State()) {
            case Timer_State::TimerStopped:
                mNewPosition = 0;

                SwitchOpen->SetState(Switch_State::Off);
                SwitchClose->SetState(Switch_State::On);

                TimerOpen->Stop();
                TimerClose->Start();
                break;
                    
            case Timer_State::TimerRunning:
                SwitchOpen->SetState(Switch_State::Off);
                SwitchClose->SetState(Switch_State::Off);

                TimerOpen->Stop();
                TimerClose->Stop();
                break;
        }
    });
}