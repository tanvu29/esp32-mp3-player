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

uint32_t mp3DurationSeconds(SdFs &sd, const char *path) {
  FsFile file = sd.open(path, O_RDONLY);
  if (!file) return 0;

  uint32_t size = file.fileSize();
  uint32_t pos = 0;

  // Skip an optional ID3v2 tag at the beginning.
  uint8_t tag[10];
  if (file.read(tag, sizeof(tag)) == sizeof(tag) &&
      tag[0] == 'I' && tag[1] == 'D' && tag[2] == '3') {
    uint32_t tagSize =
        ((tag[6] & 0x7F) << 21) |
        ((tag[7] & 0x7F) << 14) |
        ((tag[8] & 0x7F) << 7)  |
         (tag[9] & 0x7F);

    pos = 10 + tagSize;
    if (tag[5] & 0x10) pos += 10;  // ID3 footer, if present
  }

  uint64_t totalSamples = 0;
  uint32_t sampleRate = 0;

  while (pos + 4 <= size) {
    if (!file.seekSet(pos)) break;

    int b0 = file.read();
    int b1 = file.read();
    int b2 = file.read();
    int b3 = file.read();

    if (b0 < 0 || b1 < 0 || b2 < 0 || b3 < 0) break;

    uint32_t h = ((uint32_t)b0 << 24) | ((uint32_t)b1 << 16) |
                 ((uint32_t)b2 << 8)  | (uint32_t)b3;

    // MP3 sync word, MPEG version, Layer III
    if ((h & 0xFFE00000) != 0xFFE00000) {
      pos++;
      continue;
    }

    uint8_t version = (h >> 19) & 0x03;  // 3=MPEG1, 2=MPEG2, 0=MPEG2.5
    uint8_t layer   = (h >> 17) & 0x03;  // 1=Layer III
    uint8_t brIndex = (h >> 12) & 0x0F;
    uint8_t srIndex = (h >> 10) & 0x03;
    uint8_t padding = (h >> 9) & 0x01;

    if (version == 1 || layer != 1 || brIndex == 0 ||
        brIndex == 15 || srIndex == 3) {
      pos++;
      continue;
    }

    static const uint16_t mpeg1Bitrate[] = {
      0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320
    };
    static const uint16_t mpeg2Bitrate[] = {
      0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160
    };
    static const uint16_t rates[] = {44100, 48000, 32000};

    uint32_t sr = rates[srIndex];
    if (version == 2) sr /= 2;       // MPEG-2
    else if (version == 0) sr /= 4;  // MPEG-2.5

    uint32_t bitrate = (version == 3 ? mpeg1Bitrate[brIndex]
                                      : mpeg2Bitrate[brIndex]) * 1000UL;

    uint32_t samplesPerFrame = (version == 3) ? 1152 : 576;
    uint32_t frameBytes = ((version == 3 ? 144UL : 72UL) * bitrate) / sr
                          + padding;

    if (frameBytes < 4 || pos + frameBytes > size) {
      pos++;
      continue;
    }

    sampleRate = sr;
    totalSamples += samplesPerFrame;
    pos += frameBytes;
  }

  file.close();

  return sampleRate ? (uint32_t)(totalSamples / sampleRate) : 0;
}