#include "Display.h"

// Constructor
Display::Display(uint8_t sdaPin, uint8_t sclPin)
  : _lcd(LCD_ADDRESS, LCD_COLUMNS, LCD_ROWS), _sda(sdaPin), _scl(sclPin) {}

void Display::begin() {
  Wire.begin(_sda, _scl);

  _lcd.init();
  _lcd.backlight();

  // Reset scrolling line for either row
  for (int row = 0; row < LCD_ROWS; row++) {
    resetScrollState(row);
  }
}

void Display::update() {
  unsigned long now = millis();

  if (now - _lastDisplayUpdate < REFRESH_MS) return;
  
  _lastDisplayUpdate = now;


  // Update scrolling line for either row
  for (int row = 0; row < LCD_ROWS; row++) {
    ScrollState& state = _scrollState[row];
    
    if (state.active) {
      updateScrollState(row);
      writeLine(row, state.window);
    }
  }
}

void Display::showPlayback(const char *title, uint32_t currentSeconds, uint32_t totalSeconds) {
  char timeText[LINE_BUFFER_SIZE];
  formatTime(timeText, currentSeconds, totalSeconds);

  showMessage(0, title);
  showMessage(1, timeText);
}

void Display::showMessage(uint8_t row, const char *text) {
  ScrollState& state = _scrollState[row];
  size_t length = strlen(text);

  // If static line, simply display the text
  if (length <= LCD_COLUMNS) {
    resetScrollState(row);
    writeLine(row, text);
    return;
  }

  // If new scrolling text, init new scroll state
  if (!state.active || strcmp(state.text, text) != 0) {
    initScrollState(row, text);
  }
}

void Display::writeLine(uint8_t row, const char *text) {
  char line[LINE_BUFFER_SIZE];
  fitTextToLine(line, text);
  _lcd.setCursor(0, row);
  _lcd.print(line);
}

void Display::fitTextToLine(char *line, const char *text) {
  size_t length = strlen(text);

  // Truncate line if necessary
  if (length > LCD_COLUMNS) {
    length = LCD_COLUMNS;
  }

  // Copies text into C string line
  memcpy(line, text, length);  

  // Pad text with spaces to fit line, if necessary
  for (int i = length; i < LCD_COLUMNS; i++) {
    line[i] = ' ';
  }

  // Terminate the C string
  line[LCD_COLUMNS] = '\0';
}

void Display::updateScrollState(uint8_t row) {
  // Get the scrolling state for this LCD row
  // Contains the message, current scroll position, and time of last update
  ScrollState& state = _scrollState[row];

  unsigned long now = millis();

  // Scrolling updates are timed separately from LCD refresh
  // Only update on the scrolling period
  if (now - state.lastScrollUpdate < SCROLL_MS) return;

  state.lastScrollUpdate = now;

  // End the scrolling text with extra spaces so it doesn't loop back immediately
  size_t totalLength = state.length + SCROLL_GAP;

  // Update portion of text currently visible, i.e. within the LCD window
  for (int i = 0; i < LCD_COLUMNS; i++) {
    size_t cycleIndex = (state.position + i) % totalLength;

    if (cycleIndex < state.length) {
      // We are still inside the actual message
      state.window[i] = state.text[cycleIndex];
    }

    else {
      // We are inside the gap after the message
      state.window[i] = ' ';
    }
  }

  // Terminate the C string
  state.window[LCD_COLUMNS] = '\0';
  
  // Shift the LCD window over one character to the right
  state.position++;

  if (state.position >= totalLength) {
    state.position = 0;
  }
}

void Display::initScrollState(uint8_t row, const char *text) {
  ScrollState& state = _scrollState[row];
  strncpy(state.text, text, SCROLL_BUFFER_SIZE - 1);
  state.text[SCROLL_BUFFER_SIZE - 1] = '\0';
  state.length = strlen(state.text);
  state.position = 0;
  state.lastScrollUpdate = millis();
  state.active = true;
  updateScrollState(row);
}

void Display::resetScrollState(uint8_t row) {
  ScrollState& state = _scrollState[row];
  state.text[0] = '\0';
  state.window[0] = '\0';
  state.length = 0;
  state.position = 0;
  state.lastScrollUpdate = 0;
  state.active = false;
}

// Format as MM:SS / MM:SS
void Display::formatTime(char* line, uint32_t currentSeconds, uint32_t totalSeconds) {
  snprintf(
    line, 
    LINE_BUFFER_SIZE, 
    "%02lu:%02lu/%02lu:%02lu",
    currentSeconds / 60, 
    currentSeconds % 60,
    totalSeconds / 60,
    totalSeconds % 60
  );
}

