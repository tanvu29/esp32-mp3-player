#include "PlaybackTime.h"
#include "Controls.h"
#include "Display.h"
#include "FileNavigation.h"

#include "AudioTools.h"
#include "AudioTools/Disk/AudioSourceSDFAT.h"
#include "AudioTools/AudioCodecs/CodecMP3Helix.h"
#include <SPI.h>

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


const char* startFilePath = "/";
const char* ext = "mp3";
AudioSourceSDFAT<SdFs, FsFile> source(startFilePath, ext, SD_CS);

I2SStream i2s;
PlaybackProgressStream progress(i2s);

MP3DecoderHelix decoder;
AudioPlayer *player = nullptr;


Controls controls(ENC_CLK, ENC_DT, ENC_SW);
Display display(I2C_SDA, I2C_SCL);
FileNavigation navigation(source);

/*
 * Player state
 */
enum PlayerMode {
  BROWSING,
  PLAYING
};
PlayerMode mode = BROWSING;
bool playbackStarted = false;

unsigned long lastPlaybackDisplayUpdate = 0;
const unsigned long PLAYBACK_DISPLAY_INTERVAL = 250;

int volume = 0;

void handleRotation();
void handleButton();

void handleRotationBrowsing(int direction);
void handleRotationPlaying(int direction);

void handleButtonBrowsing(ButtonEvent event);
void handleButtonPlaying(ButtonEvent event);

void playSelectedSong();
void updateDisplayContent();


void setup() {
  Serial.begin(115200);
  delay(2000);

  AudioToolsLogger.begin(Serial, AudioToolsLogLevel::Info);

  controls.begin();
  display.begin();

  // SPI setup
  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);

  source.begin();

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

  // Initialize FileNavigation
  if (!navigation.begin()) {
    Serial.println("ERROR: FileNavigation::begin() failed.");
    while (true) delay(1000);
  }

  //player->setMetadataCallback(printMetaData);

  player->setAutoNext(false);
  player->setVolume(0 / 100.0f);

  progress.reset();
  updateDisplayContent();
}


void loop() {
  player->copy();
  controls.update(); // button state machine
  handleRotation();
  handleButton();
  
  if (mode == PLAYING) {
    unsigned long now = millis();

    if (now - lastPlaybackDisplayUpdate >= PLAYBACK_DISPLAY_INTERVAL) {
      lastPlaybackDisplayUpdate = now;
      updateDisplayContent();
    }
  }
  
  display.refresh();
}

/*
 * Helpers
 */

void handleRotation() {
  int direction = controls.getRotation();

  switch (mode) {
    case BROWSING:
      handleRotationBrowsing(direction);
      break;

    case PLAYING:
      handleRotationPlaying(direction);
      break;
  }
}

void handleButton() {
  ButtonEvent event = controls.getButtonEvent();

  switch (mode) {
    case BROWSING:
      handleButtonBrowsing(event);
      break;

    case PLAYING:
      handleButtonPlaying(event);
      break;
  }
}

void handleRotationBrowsing(int direction) {
  navigation.move(direction);
  updateDisplayContent();
}

void handleRotationPlaying(int direction) {
  if (direction == 0) {
    return;
  }
  volume = constrain(volume + direction, 0, 100);
  player->setVolume(volume / 100.0f);
  Serial.print("Volume: ");
  Serial.println(volume);
}

void handleButtonBrowsing(ButtonEvent event) {
  switch (event) {
    case ButtonEvent::SINGLE_PRESS: {
      SelectResult result = navigation.select();

      if (result == SelectResult::SONG_SELECTED) {
        playSelectedSong();
        break;
      }

      updateDisplayContent();
      break;
    }
    
    case ButtonEvent::DOUBLE_PRESS: {
      break;
    }

    case ButtonEvent::LONG_PRESS: {
      navigation.back();
      updateDisplayContent();
      break;
    }     
  }
}

void handleButtonPlaying(ButtonEvent event) {
  switch (event) {
    case ButtonEvent::DOUBLE_PRESS: {
      if (navigation.nextSong()) {
        playSelectedSong();
        updateDisplayContent();
      }
      break;
    }
    
    case ButtonEvent::SINGLE_PRESS: {
      if (player->isActive()) {
        player->stop();
      } else {
        player->play();
      }
      break;
    }
      
    case ButtonEvent::LONG_PRESS:{
      player->stop();
      mode = BROWSING;
      display.clearLine(1);
      updateDisplayContent();
      break;
    }      
  }
}

void playSelectedSong() {
  const SelectedSongInfo &song = navigation.selectedSongInfo();

  if (!player->setPath(song.path)) {
    Serial.println("ERROR: player->setPath() failed.");
  }

  progress.reset();
  player->play();
  mode = PLAYING;
  updateDisplayContent();
}

void updateDisplayContent() {
  switch (mode) {
    case BROWSING: {
      const CurrentBrowserEntry &entry = navigation.currentBrowserEntry();
      display.showMessage(0, entry.name);
      break;
    }

    case PLAYING: {
      const SelectedSongInfo &song = navigation.selectedSongInfo();
      uint32_t currentSeconds = progress.currentSeconds();
      display.showPlayback(song.title, currentSeconds, song.durationSeconds);
      break;
    }   
  }
}