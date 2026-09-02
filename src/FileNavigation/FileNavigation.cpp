#include "FileNavigation.h"

// Static metadata instance
FileNavigation* FileNavigation::_metadataInstance = nullptr;

namespace {

constexpr size_t MP3_SCAN_BUFFER_SIZE = 4096;

// Parse an MP3 frame header
//
// Returns true if the header is valid and fills in:
//   sampleRate
//   bitrate
//   samplesPerFrame
//   frameBytes
bool parseMP3FrameHeader(
    uint32_t header,
    uint32_t &sampleRate,
    uint32_t &bitrate,
    uint32_t &samplesPerFrame,
    uint32_t &frameBytes
  ) {

  // MP3 sync word
  if ((header & 0xFFE00000) != 0xFFE00000) {
    return false;
  }

  uint8_t version  = (header >> 19) & 0x03;
  uint8_t layer = (header >> 17) & 0x03;
  uint8_t bitrateIndex = (header >> 12) & 0x0F;
  uint8_t sampleRateIndex = (header >> 10) & 0x03;
  uint8_t padding = (header >> 9) & 0x01;

  // MPEG version 1 = reserved value.
  // Layer must be III.
  // Bitrate/sample-rate indices 0x0 and 0xF/0x3 are invalid.
  if (version == 1 ||
      layer != 1 ||
      bitrateIndex == 0 ||
      bitrateIndex == 15 ||
      sampleRateIndex == 3) {
    return false;
  }

  static const uint16_t MPEG1_BITRATES[] = {
    0, 32, 40, 48, 56, 64, 80, 96,
    112, 128, 160, 192, 224, 256, 320
  };

  static const uint16_t MPEG2_BITRATES[] = {
    0, 8, 16, 24, 32, 40, 48, 56,
    64, 80, 96, 112, 128, 144, 160
  };

  static const uint16_t SAMPLE_RATES[] = {
    44100, 48000, 32000
  };

  sampleRate = SAMPLE_RATES[sampleRateIndex];

  if (version == 2) {
    // MPEG-2
    sampleRate /= 2;
  }
  else if (version == 0) {
    // MPEG-2.5
    sampleRate /= 4;
  }

  bitrate =
      (version == 3
          ? MPEG1_BITRATES[bitrateIndex]
          : MPEG2_BITRATES[bitrateIndex])
      * 1000UL;

  samplesPerFrame = (version == 3) ? 1152 : 576;

  frameBytes =
      ((version == 3 ? 144UL : 72UL) * bitrate) / sampleRate
      + padding;

  if (frameBytes < 4) {
    return false;
  }

  return true;
}


// Determine where the actual MP3 audio begins.
//
// If an ID3v2 tag exists, returns the position immediately after it.
// Otherwise returns 0.
//
// The caller is responsible for checking that the returned position is
// within the file.
bool findAudioStart(
    FsFile &file,
    uint32_t fileSize,
    uint32_t &audioStart
  ) {
  audioStart = 0;

  if (fileSize < 10) {
    return true;
  }

  if (!file.seekSet(0)) {
    return false;
  }

  uint8_t tag[10];

  if (file.read(tag, sizeof(tag)) != sizeof(tag)) {
    return false;
  }

  // Not an ID3v2 file.
  if (tag[0] != 'I' ||
      tag[1] != 'D' ||
      tag[2] != '3') {
    return true;
  }

  // ID3v2 uses a 7-bit "synchsafe" integer.
  uint32_t tagSize =
      ((uint32_t)(tag[6] & 0x7F) << 21) |
      ((uint32_t)(tag[7] & 0x7F) << 14) |
      ((uint32_t)(tag[8] & 0x7F) << 7)  |
      ((uint32_t)(tag[9] & 0x7F));

  audioStart = 10 + tagSize;

  // ID3v2 footer is another 10 bytes.
  if (tag[5] & 0x10) {
    audioStart += 10;
  }

  if (audioStart > fileSize) {
    return false;
  }

  return true;
}

} // anonymous namespace end


FileNavigation::FileNavigation(AudioSourceSDFAT<SdFs, FsFile> &source)
  : _source(source) {}

bool FileNavigation::begin() {
  // Reset states
  _depth = 0;
  memset(&_currentBrowserEntry, 0, sizeof(_currentBrowserEntry));
  memset(&_selectedSongInfo, 0, sizeof(_selectedSongInfo));
  strcpy(_currentBrowserDirectory, "/");

  // Close anything left open from previous begin()
  _entry.close();
  _directory.close();

  // Open root directory
  if (!_directory.openRoot(&_source.getAudioFs())) {
    Serial.println("[E] FileNavigation::begin: could not open root directory");
    return false;
  }

  // Set cursor to first entry in root
  if (!openFirstEntry()) {
    Serial.println("[E] FileNavigation::begin: root directory is empty");
    return false;
  }
  
  Serial.println("FileNavigation::begin: initialized");
  return true;
}

bool FileNavigation::openDirectory(const char *path) {
  if (path == nullptr) {
    Serial.println("[E] FileNavigation::openDirectory: path is null");
    return false;
  }

  // Close current directory
  _directory.close();

  // Open requested directory
  if (!_directory.open(&_source.getAudioFs(), path)) {
    Serial.println("[E] FileNavigation::openDirectory: failed to open directory");
    return false;
  }

  // REVISED - replaced strncpy with snprintf
  //strncpy(_currentBrowserDirectory, path, sizeof(_currentBrowserDirectory) - 1);
  //_currentBrowserDirectory[sizeof(_currentBrowserDirectory) - 1] = '\0';

  // Store current cursor path
  int written = snprintf(_currentBrowserDirectory, sizeof(_currentBrowserDirectory), "%s", path);

  if (written < 0 || static_cast<size_t>(written) >= sizeof(_currentBrowserDirectory)) {
    Serial.println("[E] FileNavigation::openDirectory: failed to write path");
    return false;
  }

  return true;
}

bool FileNavigation::move(int direction) {
  if (direction > 0) {
    return openNextEntry();
  }

  if (direction < 0) {
    return openPreviousEntry();
  }

  return false;
}

SelectResult FileNavigation::select() {
  // Case 1: selecting a directory
  if (_currentBrowserEntry.isDirectory) {
    if (_depth >= MAX_DIRECTORY_DEPTH) {
      Serial.println("[E] FileNavigation::select: maximum directory depth reached");
      return SelectResult::NONE;
    }

    // Save where we are in the parent directory before entering child directory
    _parentDirectory[_depth].selectedIndex = _currentBrowserEntry.parentDirectoryIndex;

    // Build child directory path
    char newPath[memConfig::MAX_PATH_LENGTH];

    if (!buildPath(_currentBrowserEntry.name, newPath, sizeof(newPath))) {
      Serial.println("[E] FileNavigation::select: could not build directory path");
      return SelectResult::NONE;
    }

    // Enter child directory
    if (!openDirectory(newPath)) {
      Serial.print("[E] FileNavigation::select: could not open directory: ");
      Serial.println(newPath);
      return SelectResult::NONE;
    }
    
    _depth++;

    // Check that child directory is not empty
    if (!openFirstEntry()) {
      Serial.println("[E] FileNavigation::select: directory is empty");

      // Return to parent directory, revert depth
      back();
      return SelectResult::NONE;
    }

    return SelectResult::DIRECTORY_ENTERED;
  }
  
  // Case 2: selecting an MP3
  if (isMP3(_currentBrowserEntry.name)) {
    if (loadSong()) {
      return SelectResult::SONG_SELECTED;
    }
  }

  return SelectResult::NONE;
}

bool FileNavigation::back() {
  // Root has no parent
  if (atRoot()) {
    return false;
  }

  // Retrieve the index of the child directory we previously entered
  uint16_t selectedIndex = _parentDirectory[_depth - 1].selectedIndex;

  // Rebuild parent path
  char parentDirectory[memConfig::MAX_PATH_LENGTH];
  if (!buildParentPath(_currentBrowserDirectory, parentDirectory, sizeof(parentDirectory))) {
    Serial.println("[E] FileNavigation::back: could not build parent directory path");
    return false;
  }

  // Close child directory
  _entry.close();
  _directory.close();

  // Open parent directory
  if (!openDirectory(parentDirectory)) {
    Serial.println("[E] FileNavigation::back: failed to reopen parent directory");
    return false;
  }

  _depth--;

  // Restore the cursor to the child directory entry in the parent directory
  _entry.close();
  if (!_entry.open(&_directory, selectedIndex, O_RDONLY)) {
    Serial.println("[E] FileNavigation::back: failed to restore cursor in parent directory");
    return false;
  }

  return updateCurrentEntry();
}

bool FileNavigation::nextSong() {
  uint16_t originalIndex = _selectedSongInfo.parentDirectoryIndex;
  
  while (true) {
    // We've reached the end of the directory without finding an MP3
    if (!openNextEntry()) {
      // Restore cursor position to loaded song
      if (_entry.open(&_directory, originalIndex, O_RDONLY)) {
        updateCurrentEntry();
      }
      return false;
    }
    if (!_currentBrowserEntry.isDirectory && isMP3(_currentBrowserEntry.name)) {
      return loadSong();
    }
  }
}

bool FileNavigation::previousSong() {
  uint16_t originalIndex = _selectedSongInfo.parentDirectoryIndex;
  
  while (true) {
    // We've reached the end of the directory without finding an MP3
    if (!openPreviousEntry()) {
      // Restore cursor position to loaded song
      if (_entry.open(&_directory, originalIndex, O_RDONLY)) {
        updateCurrentEntry();
      }
      return false;
    }
    if (!_currentBrowserEntry.isDirectory && isMP3(_currentBrowserEntry.name)) {
      return loadSong();
    }
  }
}

bool FileNavigation::buildPath(const char *name, char* pathBuffer, size_t bufferSize) const {
  if (name == nullptr || pathBuffer == nullptr || bufferSize == 0) {
    return false;
  }
  
  int written;
  
  // If starting from root, simply format as /name
  if (atRoot()) {
    written = snprintf(pathBuffer, bufferSize, "/%s", name);
  }

  // Otherwise format as .../name
  else {
    written = snprintf(pathBuffer, bufferSize, "%s/%s", _currentBrowserDirectory, name);
  }

  return written >= 0 && static_cast<size_t>(written) < bufferSize;
}

bool FileNavigation::buildParentPath(const char *path, char *pathBuffer, size_t bufferSize) const {
  if (path == nullptr || pathBuffer == nullptr || bufferSize == 0) {
    return false;
  }
  
  // REVISED - replaced strncpy with snprintf
  //strncpy(pathBuffer, path, bufferSize - 1);
  //pathBuffer[bufferSize - 1] = '\0';

  int written = snprintf(pathBuffer, bufferSize, "%s", path);

  if (written < 0 || static_cast<size_t>(written) >= bufferSize) {
    return false;
  }

  // Find the last '/' in the path
  char* slash = strrchr(pathBuffer, '/');

  // Both slash and pathBuffer point to the same slash
  // Since pathBuffer points to the root slash, the last directory in path was root
  if (slash == pathBuffer) {
    pathBuffer[0] = '/'; // return to root
    pathBuffer[1] = '\0';
  }

  // Remove everything after the last slash
  else {
    *slash = '\0';
  }

  return true;
}

bool FileNavigation::isHiddenEntry() {
  char name[memConfig::MAX_NAME_LENGTH];

  if (_entry.getName(name, sizeof(name)) == 0) {
    return true;
  }
  
  // Hide folder auto generated by Windows
  return strcmp(name, "System Volume Information") == 0;
}

bool FileNavigation::openFirstEntry() {
  _entry.close();
  _directory.rewind(); // Go to start of directory

  while (_entry.openNext(&_directory, O_RDONLY)) {
    if (!isHiddenEntry()) {
      return updateCurrentEntry();
    }
    _entry.close();
  }

  return false;
}

bool FileNavigation::openNextEntry() {
  _entry.close();

  uint16_t originalIndex = _currentBrowserEntry.parentDirectoryIndex;

  while (_entry.openNext(&_directory, O_RDONLY)) {
    if (!isHiddenEntry()) {
      return updateCurrentEntry();
    }
    _entry.close();
  }

  // No non-hidden next entry
  // Restore original cursor position
  if (_entry.open(&_directory, originalIndex, O_RDONLY)) {
    updateCurrentEntry();
  }

  return false;
}

bool FileNavigation::openPreviousEntry() {
  _entry.close();
  
  uint16_t originalIndex = _currentBrowserEntry.parentDirectoryIndex;

  // There is no entry before index 0
  if (originalIndex == 0) {
    return false;
  }

  // Search backwards, starting with the entry immediately before
  // Keep going until we find an entry
  uint16_t searchIndex = originalIndex - 1;
  
  while (true) {
    if (_entry.open(&_directory, searchIndex, O_RDONLY)) {
      if (!isHiddenEntry()) {
        return updateCurrentEntry();
      }
      _entry.close();
    }

    if (searchIndex == 0) {
      break;
    }

    searchIndex--;
  }

  // No non-hidden previous entry
  // Restore original cursor position
  if (_entry.open(&_directory, originalIndex, O_RDONLY)) {
    updateCurrentEntry();
  }

  return false;
}

bool FileNavigation::updateCurrentEntry() {
  if (!_entry.isOpen()) {
    return false;
  }

  // Read filename
  char name[memConfig::MAX_NAME_LENGTH];
  if (_entry.getName(name, sizeof(name)) == 0) {
    return false;
  }

  // Clear old entry information
  memset(&_currentBrowserEntry, 0, sizeof(_currentBrowserEntry));
  
  // REVISED - replaced strncpy with snprintf
  //strncpy(_currentBrowserEntry.name, name, sizeof(_currentBrowserEntry.name) - 1);
  //_currentBrowserEntry.name[sizeof(_currentBrowserEntry.name) - 1] = '\0';
  
  // Store name
  int written = snprintf(_currentBrowserEntry.name, sizeof(_currentBrowserEntry.name), "%s", name);

  if (written < 0 || static_cast<size_t>(written) >= sizeof(_currentBrowserEntry.name)) {
    Serial.println("[E] FileNavigation::updateCurrentEntry: failed to write name");
    return false;
  }

  // Build complete path
  if (!buildPath(name, _currentBrowserEntry.path, sizeof(_currentBrowserEntry.path))) {
    Serial.println("[E] FileNavigation::updateCurrentEntry: failed to build path");
    return false;
  }

  _currentBrowserEntry.isDirectory = _entry.isDir();
  _currentBrowserEntry.parentDirectoryIndex = _entry.dirIndex();

  return true;
}

bool FileNavigation::atRoot() const {
  return _depth == 0;
}

bool FileNavigation::loadSong() {
  SelectedSongInfo& song = _selectedSongInfo;
  CurrentBrowserEntry& entry = _currentBrowserEntry;
  
  // Must be MP3
  if (entry.isDirectory || !isMP3(entry.name)) {
    return false;
  }

  // Clear selected song info
  memset(&song, 0, sizeof(song));

  /*
   * Copy fields from current browser entry into selected song info
   */

  // Parent directory index
  song.parentDirectoryIndex = entry.parentDirectoryIndex;

  // Copy song path
  int written = snprintf(song.path, sizeof(song.path), "%s", entry.path);
  
  if (written < 0 || static_cast<size_t>(written) >= sizeof(song.path)) {
    Serial.println("[E] FileNavigation::loadSong: failed to write path");
    return false;
  }

  // Copy title (default song title = filename)
  // If ID3 metadata contains a title, readMetadata() will replace this
  written = snprintf(song.title, sizeof(song.title), "%s", entry.name);

  if (written < 0 || static_cast<size_t>(written) >= sizeof(song.title)) {
    Serial.println("[E] FileNavigation::loadSong: failed to write name");
    return false;
  }

  // Read ID3 metadata, update title & artist
  // If no title found, default to filename
  // Artist is a blank array by default
  readMetadata(song.path);

  // Calculate duration
  song.durationSeconds = calculateDurationSeconds(song.path);

  // Debug output
  Serial.println();

  Serial.println("FileNavigation: selected song");

  Serial.print("Path: ");
  Serial.println(song.path);

  Serial.print("Title: ");
  Serial.println(song.title);

  Serial.print("Artist: ");
  Serial.println(song.artist);

  Serial.printf("Duration: %lu:%02lu\n",
    song.durationSeconds / 60,
    song.durationSeconds % 60
  );

  return true;
}


bool FileNavigation::isMP3(const char *filename) const {
  if (filename == nullptr) {
    Serial.println("[E] FileNavigation::isMP3: filename is null");
    return false;
  }

  const char *extension = strrchr(filename, '.');

  if (extension == nullptr) {
    return false;
  }

  // Case insensitive comparison
  return strcasecmp(extension, ".mp3") == 0;
}

uint32_t FileNavigation::calculateDurationSeconds(const char *path) {
  if (path == nullptr) {
    Serial.println(
        "[E] FileNavigation::calculateDurationSeconds: "
        "path is null"
    );
    return 0;
  }

  FsFile file;

  if (!file.open(&_source.getAudioFs(), path, O_RDONLY)) {
    Serial.print(
        "[E] FileNavigation::calculateDurationSeconds: "
        "could not open file: "
    );
    Serial.println(path);
    return 0;
  }

  uint32_t fileSize = file.fileSize();

  // Find the beginning of the actual MP3 audio.
  uint32_t audioStart = 0;

  if (!findAudioStart(file, fileSize, audioStart)) {
    Serial.println(
        "[E] FileNavigation::calculateDurationSeconds: "
        "could not determine audio start"
    );
    file.close();
    return 0;
  }

  if (audioStart >= fileSize) {
    file.close();
    return 0;
  }

  /*
   * First, read a small portion of the beginning of the audio.
   *
   * This lets us look for a Xing/Info VBR header. Files produced by
   * LAME/yt-dlp commonly contain one.
   */
  constexpr size_t HEADER_SCAN_SIZE = 512;

  uint8_t headerBuffer[HEADER_SCAN_SIZE];

  if (!file.seekSet(audioStart)) {
    file.close();
    return 0;
  }

  size_t bytesRead =
      file.read(
          headerBuffer,
          min(
              static_cast<uint32_t>(HEADER_SCAN_SIZE),
              fileSize - audioStart
          )
      );

  if (bytesRead < 4) {
    file.close();
    return 0;
  }

  /*
   * Find the first valid MP3 frame.
   */
  uint32_t firstSampleRate = 0;
  uint32_t firstBitrate = 0;
  uint32_t firstSamplesPerFrame = 0;
  uint32_t firstFrameBytes = 0;

  size_t firstFrameOffset = 0;
  bool foundFirstFrame = false;

  for (size_t i = 0; i + 4 <= bytesRead; i++) {
    uint32_t header =
        ((uint32_t)headerBuffer[i] << 24) |
        ((uint32_t)headerBuffer[i + 1] << 16) |
        ((uint32_t)headerBuffer[i + 2] << 8) |
        (uint32_t)headerBuffer[i + 3];

    uint32_t sampleRate;
    uint32_t bitrate;
    uint32_t samplesPerFrame;
    uint32_t frameBytes;

    if (parseMP3FrameHeader(
            header,
            sampleRate,
            bitrate,
            samplesPerFrame,
            frameBytes)) {

      // Make sure the complete frame is actually inside the file.
      uint32_t framePosition =
          audioStart + i;

      if (framePosition + frameBytes <= fileSize) {
        firstSampleRate = sampleRate;
        firstBitrate = bitrate;
        firstSamplesPerFrame = samplesPerFrame;
        firstFrameBytes = frameBytes;
        firstFrameOffset = i;
        foundFirstFrame = true;
        break;
      }
    }
  }

  if (!foundFirstFrame) {
    Serial.println(
        "[E] FileNavigation::calculateDurationSeconds: "
        "could not find MP3 frame"
    );
    file.close();
    return 0;
  }

  /*
   * Look for a Xing or Info header.
   *
   * We search rather than relying on a hard-coded offset because the
   * exact location depends on MPEG version, channel mode, and CRC.
   */
  for (size_t i = firstFrameOffset;
       i + 8 <= bytesRead &&
       i < firstFrameOffset + 256;
       i++) {

    bool isXing =
        headerBuffer[i] == 'X' &&
        headerBuffer[i + 1] == 'i' &&
        headerBuffer[i + 2] == 'n' &&
        headerBuffer[i + 3] == 'g';

    bool isInfo =
        headerBuffer[i] == 'I' &&
        headerBuffer[i + 1] == 'n' &&
        headerBuffer[i + 2] == 'f' &&
        headerBuffer[i + 3] == 'o';

    if (!isXing && !isInfo) {
      continue;
    }

    /*
     * Xing/Info structure:
     *
     *   4 bytes  "Xing"/"Info"
     *   4 bytes  flags
     *   4 bytes  frame count   <-- if flag 0x0001 is set
     */
    uint32_t flags =
        ((uint32_t)headerBuffer[i + 4] << 24) |
        ((uint32_t)headerBuffer[i + 5] << 16) |
        ((uint32_t)headerBuffer[i + 6] << 8) |
        (uint32_t)headerBuffer[i + 7];

    if (flags & 0x00000001) {
      if (i + 12 <= bytesRead) {
        uint32_t frameCount =
            ((uint32_t)headerBuffer[i + 8] << 24) |
            ((uint32_t)headerBuffer[i + 9] << 16) |
            ((uint32_t)headerBuffer[i + 10] << 8) |
            (uint32_t)headerBuffer[i + 11];

        if (frameCount > 0 && firstSampleRate > 0) {
          uint64_t totalSamples =
              (uint64_t)frameCount * firstSamplesPerFrame;

          uint32_t duration =
              (uint32_t)(totalSamples / firstSampleRate);

          file.close();
          return duration;
        }
      }
    }

    // We found Xing/Info, but it did not contain a usable frame count.
    break;
  }

  /*
   * No usable Xing/Info header.
   *
   * Fall back to scanning the MP3 frames sequentially.
   */
  uint64_t totalSamples = 0;
  uint32_t sampleRate = firstSampleRate;

  uint32_t position =
      audioStart + firstFrameOffset;

  uint8_t buffer[MP3_SCAN_BUFFER_SIZE];

  while (position + 4 <= fileSize) {

    /*
     * Read a large sequential block starting at the current frame.
     */
    if (!file.seekSet(position)) {
      break;
    }

    size_t chunkSize =
        min(
            static_cast<uint32_t>(sizeof(buffer)),
            fileSize - position
        );

    int chunkBytes =
        file.read(buffer, chunkSize);

    if (chunkBytes < 4) {
      break;
    }

    size_t offset = 0;

    while (offset + 4 <= (size_t)chunkBytes) {

      uint32_t header =
          ((uint32_t)buffer[offset] << 24) |
          ((uint32_t)buffer[offset + 1] << 16) |
          ((uint32_t)buffer[offset + 2] << 8) |
          (uint32_t)buffer[offset + 3];

      uint32_t currentSampleRate;
      uint32_t bitrate;
      uint32_t samplesPerFrame;
      uint32_t frameBytes;

      if (!parseMP3FrameHeader(
              header,
              currentSampleRate,
              bitrate,
              samplesPerFrame,
              frameBytes)) {

        // Not an MP3 frame header. Move forward one byte.
        offset++;
        position++;
        continue;
      }

      /*
       * A valid frame was found.
       */
      if (position + frameBytes > fileSize) {
        break;
      }

      sampleRate = currentSampleRate;

      totalSamples += samplesPerFrame;

      /*
       * If the complete frame is inside our buffer, we can jump directly
       * to the next frame without another SD operation.
       */
      if (offset + frameBytes <= (size_t)chunkBytes) {
        offset += frameBytes;
        position += frameBytes;
      }
      else {
        /*
         * Frame crosses the end of this buffer.
         *
         * We don't need to read the frame body. We can simply start the
         * next buffer at the beginning of the next frame.
         */
        position += frameBytes;
        break;
      }
    }
  }

  file.close();

  if (sampleRate == 0 || totalSamples == 0) {
    return 0;
  }

  return (uint32_t)(totalSamples / sampleRate);
}



/*
 * METADATA
 */

bool FileNavigation::readMetadata(const char *path) {
  if (path == nullptr) {
    Serial.println(
        "[E] FileNavigation::readMetadata: path is null"
    );
    return false;
  }

  FsFile file;

  if (!file.open(&_source.getAudioFs(), path, O_RDONLY)) {
    Serial.print(
        "[E] FileNavigation::readMetadata: "
        "could not open MP3: "
    );
    Serial.println(path);
    return false;
  }

  uint32_t fileSize = file.fileSize();

  if (fileSize < 10) {
    file.close();
    return true;
  }

  // Read the ID3v2 header.
  if (!file.seekSet(0)) {
    file.close();
    return false;
  }

  uint8_t header[10];

  if (file.read(header, sizeof(header)) != sizeof(header)) {
    file.close();
    return false;
  }

  // We only currently handle ID3v2 metadata here.
  //
  // If there is no ID3v2 tag, title remains the filename and
  // artist remains empty.
  if (header[0] != 'I' ||
      header[1] != 'D' ||
      header[2] != '3') {
    file.close();
    return true;
  }

  uint32_t tagSize =
      ((uint32_t)(header[6] & 0x7F) << 21) |
      ((uint32_t)(header[7] & 0x7F) << 14) |
      ((uint32_t)(header[8] & 0x7F) << 7)  |
      ((uint32_t)(header[9] & 0x7F));

  uint32_t totalTagBytes = 10 + tagSize;

  // ID3v2 footer, if present.
  if (header[5] & 0x10) {
    totalTagBytes += 10;
  }

  // Protect against a corrupt ID3 header claiming a tag larger
  // than the actual file.
  if (totalTagBytes > fileSize) {
    Serial.println(
        "[E] FileNavigation::readMetadata: "
        "invalid ID3 tag size"
    );
    file.close();
    return false;
  }

  // Feed only the ID3 tag to MetaDataID3.
  _metadataInstance = this;

  MetaDataID3 metadata;

  metadata.setCallback(FileNavigation::metadataCallback);
  metadata.setFilter(SELECT_ID3);
  metadata.resize(METADATA_BUFFER_SIZE);
  metadata.begin();

  // Start again at the beginning so the parser receives the ID3 header.
  file.seekSet(0);

  uint8_t buffer[METADATA_BUFFER_SIZE];

  uint32_t remaining = totalTagBytes;

  while (remaining > 0) {
    size_t bytesToRead =
        min(
            static_cast<uint32_t>(sizeof(buffer)),
            remaining
        );

    int bytesRead = file.read(buffer, bytesToRead);

    if (bytesRead <= 0) {
      break;
    }

    metadata.write(buffer, bytesRead);

    remaining -= bytesRead;
  }

  metadata.end();

  file.close();
  _metadataInstance = nullptr;

  return remaining == 0;
}


void FileNavigation::metadataCallback(MetaDataType type, const char *str, int len) {
  if (_metadataInstance == nullptr || str == nullptr || len <= 0) {
    return;
  }

  FileNavigation &navigation = *_metadataInstance;
  SelectedSongInfo &song = navigation._selectedSongInfo;

  if (type == Title) {
    size_t copyLength = min(static_cast<size_t>(len), sizeof(song.title) - 1);
    memcpy(song.title, str, copyLength);
    song.title[copyLength] = '\0'; 
  }

  else if (type == Artist) {
    size_t copyLength = min(static_cast<size_t>(len), sizeof(song.artist) - 1);
    memcpy(song.artist, str, copyLength);
    song.artist[copyLength] = '\0'; 
  }
}


/*
 * Getters
 */

const CurrentBrowserEntry& FileNavigation::currentBrowserEntry() const {
  return _currentBrowserEntry;
}

const SelectedSongInfo& FileNavigation::selectedSongInfo() const {
  return _selectedSongInfo;
}

const char* FileNavigation::currentBrowserDirectory() const {
  return _currentBrowserDirectory;
}