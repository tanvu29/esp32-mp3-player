#pragma once

#include <Arduino.h>
#include <AudioTools.h>
#include <SdFat.h>

class PlaybackProgressStream : public AudioStream {
public:
  explicit PlaybackProgressStream(AudioStream &target);

  size_t write(const uint8_t *data, size_t len) override;
  void setAudioInfo(AudioInfo newInfo) override;
  int available() override;
  int availableForWrite() override;
  void flush() override;

  void reset();
  uint32_t currentSeconds() const;

private:
  AudioStream &target;
  uint64_t pcmBytesWritten = 0;
};

// Works with AudioSourceSDFAT<SdFs, FsFile>.
uint32_t mp3DurationSeconds(SdFs &sd, const char *path);