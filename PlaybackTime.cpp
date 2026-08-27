#include "PlaybackTime.h"

PlaybackProgressStream::PlaybackProgressStream(AudioStream &target)
    : target(target) {}

size_t PlaybackProgressStream::write(const uint8_t *data, size_t len) {
  size_t written = target.write(data, len);
  pcmBytesWritten += written;
  return written;
}

void PlaybackProgressStream::setAudioInfo(AudioInfo newInfo) {
  AudioStream::setAudioInfo(newInfo);
  target.setAudioInfo(newInfo);
}

int PlaybackProgressStream::available() {
  return target.available();
}

int PlaybackProgressStream::availableForWrite() {
  return target.availableForWrite();
}

void PlaybackProgressStream::flush() {
  target.flush();
}

void PlaybackProgressStream::reset() {
  pcmBytesWritten = 0;
}

uint32_t PlaybackProgressStream::currentSeconds() const {
  uint32_t bytesPerFrame =
      info.channels * (info.bits_per_sample / 8);

  if (info.sample_rate == 0 || bytesPerFrame == 0) return 0;

  return pcmBytesWritten / bytesPerFrame / info.sample_rate;
}