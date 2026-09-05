# ESP32 MP3 Player

A breadboard MP3 player, complete with a file system and a multi-function playback controller. 

![Complete breadboard build](./images/hero_image.jpeg)

## Demo
[Check it out on Youtube!](https://youtu.be/1Ke2b3DL_yw)

## Features
- High capacity storage support (64GB+)
- MP3 file system for singles/albums
- Play/pause, next song, return, and volume control

## How it works

### File system
The Arduino SdFat library was used to read MP3 files stored in a microSD card. The file system  is exFAT formatted to handle large microSD sizes, since <64GB microSD cards use the older FAT32 standard.

### Audio decoding
The Arduino Audio Tools library was used to decode MP3 PCM signals into raw bitstreams sent over I2S. The bitsreams are fed into a hardware DAC/AMP module that sends the audio to stereo speakers.

### Playback
All controls are handled through a single rotary encoder that includes a built-in button. State machines are used to debounce encoder turns and detect different button presses.

## Hardware
| Component | Part |
|:-----|:-------|
| MCU    |   ESP32-DEVKITC-VE    |
| Control    | KY-040      |
| DAC/AMP    | MAX98357a    |
| Display    | LCD1602 (w/ I2C backpack)    |
| Storage    | MicroSD Module  (SPI)   |
| Output    | 4/8 Ohm Speakers      |

## Wiring Tables
### KY-040
| Part Pin | ESP32-DEVKITC-VE Pin |
|:------|:------|
| CLK   | 36    |
| DT    | 39    |
| SW    | 34    |
| +     | 3V3   |
| GND   | GND   |

### MicroSD Module
| Part Pin | ESP32-DEVKITC-VE Pin |
|:-------|:------|
| VCC    | 3V3   |
| CS     | 5     |
| MOSI   | 23    |
| SCK    | 18    |
| MISO   | 19    |
| GND    | GND   |

### MAX98357a
| Part Pin | ESP32-DEVKITC-VE Pin | Notes |
|:-------|:------|:------|
| WS/LRC   | 15    |
| BCK      | 14    |
| DATA/DIN | 22    |
| GAIN     | N/A      | Leave disconnected
| SD       | See datasheet      | Left channel: Connect SD to 5V <br><br> Right channel: Connect SD to 5V via 500k pull-up resistor
| GND      | GND   |
| VIN      | 5V    |

### LCD1602 (I2C)
| Part Pin | ESP32-DEVKITC-VE Pin |
|:-------|:------|
| GND    | GND   |
| VCC    | 5V    |
| SDA    | 32    |
| SCL    | 33    |

## Flashing Guide
Before flashing, double check hardware connections and connect the ESP32 via USB.

### Manual Setup: Arduino IDE
1. Install [Arduino IDE](https://www.arduino.cc/en/software) 

2. Clone or download this repository, and open the .ino file

4. Install the board package: **esp32 by Espressif Systems**

5. Select the board: **ESP32 Dev Module**

6. Under Tools, select the following configurations:
    - Flash Size: 8 MB
    - Partition Scheme: 8M with SPIFFS

7. Install the following Arduino libraries:
    - [SdFat by Bill Greiman](https://github.com/greiman/sdfat)
    - [Audio Tools by Phil Schatzmann](https://github.com/pschatzmann/arduino-audio-tools/tree/main)
    - [LibHelix by Phil Schatzmann](https://github.com/pschatzmann/arduino-libhelix) 
    - [LiquidCrystal_I2C by Martin Kubovcik, Frank de Brabander](https://github.com/markub3327/LiquidCrystal_I2C)



### Quick Setup: Pre-compiled Binary
1. Install [esptool](https://docs.espressif.com/projects/esptool/en/latest/esp32/):
    ```
    pip install esptool
    ```

2. Download the .bin files from the latest Github Release

3. Inside the directory containing the downloaded .bin files, run:
    ```
    esptool --chip esp32 write-flash 0x1000 esp32_mp3_player.ino.bootloader.bin 0x8000 esp32_mp3_player.ino.partitions.bin 0x10000 esp32_mp3_player.ino.bin
    ```