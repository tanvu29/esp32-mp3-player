#pragma once

#include <Arduino.h>

enum class ButtonEvent {
  NONE,
  SINGLE_PRESS,
  DOUBLE_PRESS,
  LONG_PRESS
};

class Controls {
public:
  Controls(uint8_t clkPin, uint8_t dtPin, uint8_t swPin);

  // Called in setup()
  void begin();

  // Called in loop()
  void update();

  // Returns accumulated encoder rotation in detents
  int getRotation();

  // Volume: 0-100
  int volume() const;
  void setVolume(int value);

  // Returns and clears the pending button event
  ButtonEvent getButtonEvent();

private:
  uint8_t _clk;
  uint8_t _dt;
  uint8_t _sw;

  volatile uint8_t _lastEncoderState = 0; // 2-bit quadrature state: [CLK][DT]
  volatile int _encoderDelta = 0; // Accumulated rotation: CW (-), CCW (+)

  int _volume = 0;

  bool _lastButtonReading = HIGH;
  bool _buttonState = HIGH;

  unsigned long _lastButtonChange = 0;
  unsigned long _buttonDownTime = 0;
  unsigned long _firstClickTime = 0;

  uint8_t _clickCount = 0;

  bool _longPress = false;

  ButtonEvent _buttonEvent = ButtonEvent::NONE;

  static constexpr unsigned long DEBOUNCE_MS = 50;
  static constexpr unsigned long LONG_PRESS_MS = 700;
  static constexpr unsigned long DOUBLE_CLICK_MS = 300;

  static Controls* _instance; // Used by ISR
  static void IRAM_ATTR isr();

  void handleEncoder();
  void handleButton();
};