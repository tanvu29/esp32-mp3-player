#include "Controls.h"

// Quadrature encoder state transition table
// ==============================================
// Encoder delta    Meaning
//   0              No transition / invalid transition
//  +1              Forward transition
//  -1              Reverse transition
static const int8_t encoderStateTable[16] = {
   0,  1, -1,  0,
  -1,  0,  0,  1,
   1,  0,  0, -1,
   0, -1,  1,  0
};

Controls* Controls::_instance = nullptr;

// Constructor
Controls::Controls(uint8_t clkPin, uint8_t dtPin, uint8_t swPin)
    : _clk(clkPin), _dt(dtPin), _sw(swPin) {}

// Initialize pins and encoder interrupt
void Controls::begin() {
  pinMode(_clk, INPUT);
  pinMode(_dt, INPUT);
  pinMode(_sw, INPUT);

  // Encoder's initial quadrature state
  _lastEncoderState = (digitalRead(_clk) << 1) | digitalRead(_dt);

  // Attach interrupt handler to encoder signals
  _instance = this;
  attachInterrupt(digitalPinToInterrupt(_clk), isr, CHANGE);
  attachInterrupt(digitalPinToInterrupt(_dt), isr, CHANGE);
}

// Encoder ISR
// attachInterrupt() cannot directly call instance methods like handleEncoder()
// Instead it calls a static method, isr()
void IRAM_ATTR Controls::isr() {
  if (_instance != nullptr) {
    // Since isr() is static, explicitly pass _instance
    // _instance points to a single Controls object
    _instance->handleEncoder(); 
  }
}

// Encoder state machine
void Controls::handleEncoder() {
  // Read encoder state
  uint8_t encoderState = (digitalRead(_clk) << 1) | digitalRead(_dt);

  // State transition table index = [last state][current state]
  int index = (_lastEncoderState << 2) | encoderState;

  // Accumulate encoder rotation based on state transition
  _encoderDelta += encoderStateTable[index];

  // Record encoder state
  _lastEncoderState = encoderState;
}

// Convert accumulated encoder rotation into detents
int Controls::getRotation() {
  noInterrupts();

  // Four state transitions represent one physical encoder detent
  // Preserve any partial rotation using modulo
  int tempDelta = _encoderDelta;
  int detents = tempDelta / 4;
  _encoderDelta = tempDelta % 4;

  interrupts();

  return detents * -1;
}

// Button state machine
void Controls::handleButton() {
  unsigned long now = millis();
  bool buttonReading = digitalRead(_sw);

  // Detect change in raw button reading
  if (buttonReading != _lastButtonReading) {
    _lastButtonChange = now; // Reset debounce timer
    _lastButtonReading = buttonReading; // Record button reading
  }

  // Raw reading has not been stable long enough to accept as a state change
  if (now - _lastButtonChange < DEBOUNCE_MS) {
    return;
  }

  // Case 1: Button state has changed
  if (buttonReading != _buttonState) {
    _buttonState = buttonReading;

    // Button is now pressed
    if (_buttonState == LOW) {
      _buttonDownTime = now; // Start long press timer
      _longPress = false;
    }

    // Button is now released
    else {
      // If long press was already reported while the button was held
      // Ignore the release so it doesn't generate a click
      if (_longPress) {
        return;
      }

      // A release completes a click
      // Determine whether this is the first or second click
      _clickCount++;

      if (_clickCount == 1) {
        _firstClickTime = now; // Start double click timer after first click completes
      }

      else if (_clickCount == 2) {
        _clickCount = 0;
        _buttonEvent = ButtonEvent::DOUBLE_PRESS;
      }
    }
  }

  // Case 2: Button state has not changed
  else {
    // If button is still held beyond the long press period, report a long press
    if (_buttonState == LOW && !_longPress && now - _buttonDownTime >= LONG_PRESS_MS) {
      _longPress = true;
      _clickCount = 0;
      _buttonEvent = ButtonEvent::LONG_PRESS;
    }

    // If click count is still 1 beyond the double click period, report a single press 
    if (_clickCount == 1 && now - _firstClickTime >= DOUBLE_CLICK_MS) {
      _clickCount = 0;
      _buttonEvent = ButtonEvent::SINGLE_PRESS;
    }
  }
}

// Return pending button event and clear it
ButtonEvent Controls::getButtonEvent() {
  ButtonEvent event = _buttonEvent;
  _buttonEvent = ButtonEvent::NONE;
  return event;
}

// Poll button state
void Controls::update() {
  handleButton();
}

// Volume getter
int Controls::volume() const {
  return _volume;
}

// Volume setter
void Controls::setVolume(int value) {
  _volume = constrain(value, 0, 100);
}