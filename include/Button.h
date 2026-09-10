#ifndef Button_h
#define Button_h

#include <Arduino.h>

enum Button_Type { Normal = 0, Inverted = 1 };

const String DEF_Button_Name = "DefaultButton";
const uint32_t DEF_FilterDelay = 200;
const bool DEF_InternalPullUp = true;
const Button_Type DEF_ButtonType = Button_Type::Inverted;

const uint8_t DEF_Button_Special_Pins[] = { 1, 3, 10 };

class Button {
    private:
		typedef std::function<void()> callback_t;	

        uint8_t _Pin;
	    uint32_t _short_press_count = 0;
	    uint32_t _first_press_time = 0;
	    uint8_t _press_sequences = 0;
	    uint32_t _press_sequence_duration = 0;
	    uint32_t _held_threshold = 0;
	    bool _was_btn_held = false;
	    bool _held_callback_called = false;
	    bool _current_state;
	    bool _last_state;
	    bool _changed;
	    uint32_t _time;
	    uint32_t _last_change;
	    callback_t mPressedCallback;
	    callback_t mPressedForCallback;
	    callback_t mPressedSequenceCallback;
		
    public:
        Button(uint8_t pin);
        Button(String name, uint8_t pin);
	    ~Button() {}
	
        // Properties
        String Name;
        uint32_t FilterDelay;
        Button_Type Type;
        bool InternalPullUp;

        inline uint8_t Pin() { return _Pin; }
        void Pin(uint8_t NewPin);

        // Methods
        bool Control();

        // Events
	    inline void OnPressed(callback_t callback) { mPressedCallback = callback; }
	    inline void OnPressedFor(uint32_t duration, callback_t callback) { _held_threshold = duration; mPressedForCallback = callback; }
	    inline void OnSequence(uint8_t sequences, uint32_t duration, callback_t callback) { _press_sequences = sequences; _press_sequence_duration = duration; mPressedSequenceCallback = callback; }
	    inline bool IsPressed() { return _current_state; }
	    inline bool IsReleased() { return !_current_state; }
	    inline bool WasPressed() { return _current_state && _changed; }
	    inline bool WasReleased() { return !_current_state && _changed; }
	    inline bool PressedFor(uint32_t duration) { return _current_state && _time - _last_change >= duration; }
	    inline bool ReleasedFor(uint32_t duration) { return !_current_state && _time - _last_change >= duration; }
};

#endif