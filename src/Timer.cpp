#include "Timer.h"

Timer::Timer(uint32_t timeout) : _Timeout(timeout) {

}

void Timer::Control() {
    if(_State == Timer_State::TimerRunning) {
        if(CurrentTimerMs() >= _Timeout) {
            Reset();
            if(_OnTimeoutCallback) _OnTimeoutCallback();
        }
    }
}