#pragma once

#include <Arduino.h>
#include <Wire.h>
#include "LiquidCrystal_I2C.h"

class Display {
public:
  Display(uint8_t sdaPin, uint8_t sclPin);

  void begin();  // Initialize I2C protocol & LCD screen
  void refresh(); // Refreshes display and scrolls text when necessary

  /*
   * Display title on first row and current/total time on second row
   */
  void showPlayback(const char *title, uint32_t currentSeconds, uint32_t totalSeconds);
  
  /*
   * Display text on specified row
   * Short text is padded with spaces, long text scrolls
   */
  void showMessage(uint8_t row, const char *text);

  /*
   * Clear text on specified row
   */
  void clearLine(uint8_t row);

private:
  uint8_t _sda;
  uint8_t _scl;

  // LCD1602 config
  static constexpr int LCD_COLUMNS = 16;
  static constexpr int LCD_ROWS = 2;
  static constexpr int LCD_ADDRESS = 0x27;

  // Allows communication to display over I2C
  LiquidCrystal_I2C _lcd;

  // Scrolling text should update at a visibily slower rate than display refresh
  static constexpr unsigned long REFRESH_MS = 250;
  static constexpr unsigned long SCROLL_MS = 300;

  unsigned long _lastDisplayUpdate = 0;

  /*
   * This class represents text with C strings instead of the String class to avoid heap fragmentation
   *
   * LINE_BUFFER_SIZE reserves space for short text (up to 16 chars)
   * SCROLL_BUFFER_SIZE reserves space for long text (up to 63 chars)
   * SCROLL_GAP reserves space for blank chars at the end of a scrolling text
  */
  static constexpr size_t LINE_BUFFER_SIZE = LCD_COLUMNS + 1;
  static constexpr size_t SCROLL_BUFFER_SIZE = 128;
  static constexpr size_t SCROLL_GAP = 4;

  // Scrolling requires keeping track of state changes
  struct ScrollState {
    char text[SCROLL_BUFFER_SIZE];      // Complete scrolling message stored as C string
    char window[LINE_BUFFER_SIZE];      // Portion of message currently within LCD window
    size_t length = 0;                  // Number of characters stored in text
    int position = 0;                   // Index of the first character currently visible
    bool active = false;                // True when row contains scrolling text
    unsigned long lastScrollUpdate = 0; // Last time position was updated
  };

  // Each row is given an independent scroll state
  ScrollState _scrollState[LCD_ROWS];
  
  /*
   * Start scrolling a new message on the specified row
   * Copies the message into the row's scroll state
   */
  void initScrollState(uint8_t row, const char *text);

  /*
   * Update position index and rebuild the visible 16-character window
   */
  void updateScrollState(uint8_t row);

  /*
   * Disable scrolling on the specified row, clear any stored state
   */
  void resetScrollState(uint8_t row);

  /*
   * Format text to 16-character line and write to LCD
   */
  void writeLine(uint8_t row, const char *text);
  
  /*
   * Copy text into a 16-character LCD buffer
   * Pads short strings with spaces and truncates long strings
   */
  void fitTextToLine(char *line, const char *text);
  
  /*
   * Format song progress timer as MM:SS/MM:SS, write into caller-provided buffer
   */
  void formatTime(char* line, uint32_t currentSeconds, uint32_t totalSeconds);
};