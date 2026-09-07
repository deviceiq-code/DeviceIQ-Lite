#ifndef Timer_h
#define Timer_h

#include <Arduino.h>

enum Timer_State { TimerStopped = 0, TimerRunning = 1 };

class Timer {
    private:
        typedef std::function<void()> callback_t;
        
        callback_t _OnTimeoutCallback;

        uint32_t _Timer = 0;
        uint32_t _Timeout = 0;
        Timer_State _State = Timer_State::TimerStopped;
        
    public:
        Timer() {};
        Timer(uint32_t timeout);
        ~Timer() {};

        inline void Start() { if(_State == Timer_State::TimerStopped) { _State = Timer_State::TimerRunning; _Timer = millis(); }}
        inline void Reset() { if(_State == Timer_State::TimerRunning) { _State = Timer_State::TimerRunning; _Timer = millis(); }}
        inline void Stop() { _State = Timer_State::TimerStopped; _Timer = 0; }
        inline uint32_t CurrentTimerMs() { if(_State == Timer_State::TimerRunning) return millis() - _Timer; else return 0; }
        inline void SetTimeout(uint32_t value) { _Timeout = value; }
        inline Timer_State State() { return _State; }

        // Methods
        void Control();

        // Events
        inline void OnTimeout(callback_t callback) { _OnTimeoutCallback = callback; }
};

#endif