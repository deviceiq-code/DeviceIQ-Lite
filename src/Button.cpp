#include "Button.h"

Button::Button(uint8_t pin) : Name(DEF_Button_Name), FilterDelay(DEF_FilterDelay), Type(DEF_ButtonType), InternalPullUp(DEF_InternalPullUp) {
    Pin(pin);
}

Button::Button(String name, uint8_t pin) : Name(name), FilterDelay(DEF_FilterDelay), Type(DEF_ButtonType), InternalPullUp(DEF_InternalPullUp) {
    Pin(pin);
}

void Button::Pin(uint8_t NewPin) {
    _Pin = NewPin;
    if(std::find(std::begin(DEF_Button_Special_Pins), std::end(DEF_Button_Special_Pins), _Pin) != std::end(DEF_Button_Special_Pins)) pinMode(_Pin, FUNCTION_3);
    pinMode(_Pin, InternalPullUp ? INPUT_PULLUP : INPUT);

    _current_state = false;
	if(Type) _current_state = !_current_state;
	_time = millis();
	_last_state = _current_state;
	_changed = false;
	_last_change = _time;
}

bool Button::Control() {
	uint32_t read_started_ms = millis();
	bool pinVal = digitalRead(_Pin);
	if(Type) pinVal = !pinVal;

	if(read_started_ms - _last_change < FilterDelay) {
		_changed = false;
	} else {
		_last_state = _current_state;
		_current_state = pinVal;
		_changed = (_current_state != _last_state);
		if(_changed) _last_change = read_started_ms;
	}

	if(!_current_state && _changed) {
		if(!_was_btn_held) {
			if(_short_press_count == 0) {
                _first_press_time = read_started_ms;
			}
			_short_press_count++;
			if(mPressedCallback) {
				mPressedCallback();
			}
			if(_short_press_count == _press_sequences && _press_sequence_duration >= (read_started_ms - _first_press_time)) {
				if(mPressedSequenceCallback) mPressedSequenceCallback();
				_short_press_count = 0;
				_first_press_time = 0;
			}
			else if(_press_sequence_duration <= (read_started_ms - _first_press_time)) {
                _short_press_count = 0;
				_first_press_time = 0;
			}
		} else {
			_was_btn_held = false;
		}
		_held_callback_called = false;
	}
	else if(_current_state && read_started_ms - _last_change >= _held_threshold && mPressedForCallback) {
		_was_btn_held = true;
		_short_press_count = 0;
		_first_press_time = 0;
		if (mPressedForCallback && !_held_callback_called) {
			_held_callback_called = true;
			mPressedForCallback();
		}
	}

	_time = read_started_ms;
	return _current_state;
}