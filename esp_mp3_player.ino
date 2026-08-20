#include "PlaybackTime.h"
#include "AudioTools.h"
#include "AudioTools/Disk/AudioSourceSDFAT.h"
#include "AudioTools/AudioCodecs/CodecMP3Helix.h"
#include <SPI.h>
#include <Wire.h>
#include "LiquidCrystal_I2C.h"

// I2C LCD display
#define I2C_SDA   32
#define I2C_SCL   33

// I2S audio out
#define I2S_WS    15
#define I2S_BCK   14
#define I2S_DATA  22

// microSD (SPI)
#define SD_CS     5
#define SD_MOSI   23
#define SD_SCK    18
#define SD_MISO   19

// Rotary encoder (KY-040)
#define ENC_CLK   36
#define ENC_DT    39
#define ENC_SW    34  // button

uint32_t totalSeconds = 0;

// LCD1602
int lcdColumns = 16;
int lcdRows = 2;

// LCD address, number of columns and rows
// If you don't know your display address, run an I2C scanner sketch
LiquidCrystal_I2C lcd(0x27, lcdColumns, lcdRows); 

// Button init
bool lastButtonState = HIGH;
bool buttonState = HIGH;
unsigned long lastButtonTime = 0;
const unsigned long buttonDebounce = 50;

// Volume init
int volume = 0;
int encoderState;    // 2-bit state: [CLK][DT]
int encoderPos = 0;  // Counter tracking cumulative turns - CW (-), CCW (+)

// State transition table for quadrature encoder
// =====================================================
// Change in encoder position    Change in encoder state
//   0                             No change (stable)
//   1                             Next state
//  -1                             Previous state
const int8_t encoderStateTable[16]{
   0,  1, -1,  0,
  -1,  0,  0,  1,
   1,  0,  0, -1,
   0, -1,  1,  0
};

const char* startFilePath = "/";
const char* ext = "mp3";
AudioSourceSDFAT<SdFs, FsFile> source(startFilePath, ext, SD_CS);

I2SStream i2s;
PlaybackProgressStream progress(i2s);

MP3DecoderHelix decoder;
AudioPlayer *player = nullptr;

// ISR
void read_encoder() {
  // Read new encoder state
  int newEncoderState = (digitalRead(ENC_CLK) << 1) | digitalRead(ENC_DT);

  // State transition table index = [current state][new state]
  int index = (encoderState << 2) | newEncoderState;

  // Update encoder position based on state transition
  encoderPos += encoderStateTable[index];

  // Update encoder state
  encoderState = newEncoderState;
}

// Runs in loop()
void update_volume() {
  // One complete quadrature cycle = 4 state transitions
  if (encoderPos >= 4) {
    volume = max(volume - 1, 0);  // CCW turn
    Serial.print("Volume: ");
    Serial.println(volume);

    // 0-100 -> 0.0-1.0
    player->setVolume(volume / 100.0f);

    encoderPos = 0;
  } 
  else if (encoderPos <= -4) {
    volume = min(volume + 1, 100);  // CW turn
    Serial.print("Volume: ");
    Serial.println(volume);

    // 0-100 -> 0.0-1.0
    player->setVolume(volume / 100.0f);

    encoderPos = 0;
  }
}

// Runs in loop()
void update_button() {
  // Read button
  bool buttonReading = digitalRead(ENC_SW);

  if (buttonReading != lastButtonState) {
    // Reset debounce timer
    lastButtonTime = millis();
    lastButtonState = buttonReading;
    Serial.println("button pressed");
  }

  if (millis() - lastButtonTime >= buttonDebounce) {

    if (buttonReading != buttonState) {
      buttonState = buttonReading;
      Serial.print("Button state:");
      Serial.println(buttonState);

      // Play or pause on falling edge
      if (buttonState == LOW) {  // Actively pressed, not yet released
        if (player->isActive()) {
          player->stop();
        } else {
          player->play();
        }
      }
    }
  }
}

// LCD display
void updatePlaybackDisplay() {
  static uint32_t lastUpdate = 0;
  if (millis() - lastUpdate < 250) return;
  lastUpdate = millis();

  uint32_t currentSeconds = progress.currentSeconds();

  char line[17];
  snprintf(line, sizeof(line), "%02lu:%02lu/%02lu:%02lu",
           currentSeconds / 60, currentSeconds % 60,
           totalSeconds / 60, totalSeconds % 60);

  lcd.setCursor(0, 0);
  lcd.print("                ");
  lcd.setCursor(0, 0);
  lcd.print(line);
}

// MP3 metadata
void printMetaData(MetaDataType type, const char* str, int len) {
  Serial.print("==> ");
  Serial.print(toStr(type));
  Serial.print(": ");
  Serial.println(str);
}

void setup() {
  Serial.begin(115200);
  delay(2000);

  AudioToolsLogger.begin(Serial, AudioToolsLogLevel::Info);

  // Encoder setup
  pinMode(ENC_CLK, INPUT);
  pinMode(ENC_DT, INPUT);
  pinMode(ENC_SW, INPUT);
  attachInterrupt(digitalPinToInterrupt(ENC_CLK), read_encoder, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC_DT), read_encoder, CHANGE);

  // Initial encoder state
  encoderState = (digitalRead(ENC_CLK) << 1) | digitalRead(ENC_DT);

  // I2C setup
  Wire.begin(I2C_SDA, I2C_SCL);
  lcd.init();
  lcd.backlight();

  // SPI setup
  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);

  // I2S setup
  auto cfg = i2s.defaultConfig(TX_MODE);
  cfg.pin_ws = I2S_WS;
  cfg.pin_bck = I2S_BCK;
  cfg.pin_data = I2S_DATA;
  i2s.begin(cfg);

  player = new AudioPlayer(source, progress, decoder);

  if (player == nullptr) {
    Serial.println("ERROR: Could not allocate AudioPlayer.");
    while (true) delay(1000);
  }

  player->setMetadataCallback(printMetaData);

  if (!player->begin()) {
    Serial.println("ERROR: player.begin() failed.");
    while (true) delay(1000);
  }

  // AudioPlayer setup
  player->setMetadataCallback(printMetaData);
  player->begin();

  // Initial volume
  player->setVolume(volume / 100.0f);

  progress.reset();

  totalSeconds = mp3DurationSeconds(
      source.getAudioFs(),
      source.toStr()
  );

  Serial.printf("Duration: %lu:%02lu\n", totalSeconds / 60, totalSeconds % 60);
}

void loop() {
  update_button();
  update_volume();
  player->copy();
  updatePlaybackDisplay();
}