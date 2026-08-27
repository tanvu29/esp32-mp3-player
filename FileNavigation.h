# pragma once

#include <Arduino.h>
#include "AudioTools.h"
#include "AudioTools/Disk/AudioSourceSDFAT.h"

namespace memConfig {
  constexpr size_t MAX_NAME_LENGTH = 128;
  constexpr size_t MAX_PATH_LENGTH = 256;
}

struct CurrentBrowserEntry {
  char name[memConfig::MAX_NAME_LENGTH];
  char path[memConfig::MAX_PATH_LENGTH];
  bool isDirectory = false;
  uint16_t parentDirectoryIndex = 0;
};

struct SelectedSongInfo {
  char path[memConfig::MAX_PATH_LENGTH];
  char title[memConfig::MAX_NAME_LENGTH];
  char artist[memConfig::MAX_NAME_LENGTH];
  uint32_t durationSeconds = 0;
  uint16_t parentDirectoryIndex = 0;
};

class FileNavigation {
public:
  FileNavigation(AudioSourceSDFAT<SdFs, FsFile> &source);

  /*
   * Open the root directory and select its first entry
   * 
   * Returns:
   *  true = successful 
   *  false = root directory could not be opened or contains no entries
   */
  bool begin();

  /*
   * Move to the next/previous browser entry (+1 = next, -1 = previous)
   *
   * Directories are NOT skipped
   * - Called in main program during browsing mode
   * 
   * Returns:
   *  true = successful
   *  false = no entry in that direction
   */
  bool move(int direction);

  /*
   * Enter highlighted directory or select highlighted MP3
   *
   * Returns:
   *  true = successful
   *  false = not a directory or MP3 (some other file type)
   */
  bool select();

  /*
   * Leave current directory and return to parent directory
   * 
   * Returns:
   *  true = successful 
   *  false = already at root
   */
  bool back();

  /*
   * Move to next MP3 in the current directory and select it
   *
   * Directories ARE skipped
   * - Called in main program during playback mode
   * 
   * Returns:
   *  true = successful 
   *  false = path/name error, or entry does not exist
   */
  bool nextSong();

  /*
   * Move to previous MP3 in the current directory and select it
   *
   * Directories ARE skipped
   * - Called in main program during playback mode
   * 
   * Returns:
   *  true = successful 
   *  false = path/name error, or entry does not exist
   */
  bool previousSong();

  /*
   * GETTERS
   */

  const CurrentBrowserEntry& currentBrowserEntry() const;
  const SelectedSongInfo& selectedSongInfo() const;
  const char* currentBrowserDirectory() const;


private:
  AudioSourceSDFAT<SdFs, FsFile> &_source;

  // Saves position in parent directories for navigating back up
  struct ParentDirectory {
    uint16_t selectedIndex = 0; // Child directory's index within parent directory
  };
  
  uint16_t _depth = 0; // Directory depth
  static constexpr uint16_t MAX_DIRECTORY_DEPTH = 128;
  ParentDirectory _parentDirectory[MAX_DIRECTORY_DEPTH];

  // Current browser entry
  FsFile _entry; // This is the browser cursor object
  CurrentBrowserEntry _currentBrowserEntry;

  // Selected song info
  SelectedSongInfo _selectedSongInfo;

  // Current browser directory
  FsFile _directory; // This is the browser directory object
  char _currentBrowserDirectory[memConfig::MAX_PATH_LENGTH] = "/";

  /* 
   * FILESYSTEM HELPERS
   */ 

  /*
   * Check if currently opened entry should be hidden from user
   */
  bool isHiddenEntry();

  /*
   * Open first entry in current directory
   */
  bool openFirstEntry();

  /*
   * Open entry immediately after current highlighted entry
   */
  bool openNextEntry();

  /*
   * Open entry immediately before the current highlighted entry
   */
  bool openPreviousEntry();

  /*
   * Update CurrentBrowserEntry state
   */
  bool updateCurrentEntry();

  /*
   * Checks if current directory is root
   */
  bool atRoot() const;

  /*
   * Constructs a path from current directory + filename
   */
  bool buildPath(const char *name, char *pathBuffer, size_t bufferSize) const;

  /*
   * Get parent path of current path
   */
  bool buildParentPath(const char *path, char *pathBuffer, size_t bufferSize) const;

  /*
   * Open directory by path
   */
  bool openDirectory(const char *path);

  /*
   * Check if filename ends with ".mp3"
   */
  bool isMP3(const char *fileName) const;

  /* 
   * SONG HELPERS
   */ 

  /*
   * Read title and artist
   */
  bool readMetadata(const char *path);

  static constexpr size_t METADATA_BUFFER_SIZE = 512;

  /*
   * Estimate song duration based on encoding
   */
  uint32_t calculateDurationSeconds(const char *path);

  /*
   * Selects currently highlighted MP3 and updates SelectedSongInfo
   */
  bool loadSong();

  /* 
   * METADATA CALLBACK
   */ 
  static FileNavigation *_metadataInstance;
  static void metadataCallback(MetaDataType type, const char *str, int len);
};