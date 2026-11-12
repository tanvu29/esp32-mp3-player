# ESP32 MP3 Player

A simple breadboard MP3 player!

![final build](./images/hero_image.jpeg)

## Demo
[Check it out on youtube :)](https://youtu.be/1Ke2b3DL_yw)

## Some highlights
- High capacity storage support (64GB+)
- File system that lets you select songs/folders
- Play/pause, next song, return to song browser, and volume controls

## How it works

This is meant to be a simple music player that has as little barrier to entry as possible - it's built with relatively cheap hobbyist electronic modules, an ESP32 which is quite popular and well documented, and a breadboard.

The firmware is split into four main components which are implemented via object-oriented programming:

1. FileNavigation: 
<br> Lets you nagivate the contents of your storage device (microSD) using functions that calculates file paths and directory depths. It also reads MP3 metadata in order to calculate song duration.. since this is surprisingly not already stored in metadata apparently.

2. Controls:
<br> This is responsible for reading user input from the rotary encoder + the built-in button. The encoder logic is implemented using a hardware interrupts and a state transition table to keep track of its rotation. For the button, polling and debounce timers are used instead since it needs to differentiate between three different button presses (single press, double press, long press).

3. Display:
<br> Contains functions to update and refresh the LCD display. Since the LCD only shows 16 characters max in one line, long text/filenames scroll across and wrap around the screen.

4. PlaybackTime:
<br> Keeps track of the song timer essentially by counting the PCM (pulse-code modulation) bytes outputted from the MP3 decoder at a known rate. 

## Hardware components

These are the parts you'll need if you want to build it! This is also listed in the BOM.

| Component | Part #/Name| Qty |
|:-----|:-------|:-------|
| MCU    |   ESP32-DEVKITC-VE    | 1
| Control    | KY-040      | 1
| DAC/AMP    | MAX98357a    | 2
| Display    | LCD1602 (w/ I2C backpack)    | 1
| Storage    | microSD module  (SPI)   | 1
| Output    | 4/8 ohm speakers      | 2

## Wiring diagram

Note: some pin labels may look slightly different on the physical module (you might see a pin called LRC on the MAX98357a - I called it LRCLK since that makes the pin function clearer to me).

![wiring diagram](./images/wiring_diagram.png)

## How to flash

A little setup is required if you want to flash through Arduino IDE, but the gist of it is downloading this firmware, selecting the correct settings for the ESP32 board and flash partition, and installing the dependencies.

You can also skip all of that and flash directly to the ESP32 with a pre-compiled binary provided via the GitHub Release. This does require installing esptool in order to flash the binary.


### Option 1. Manual setup w/ Arduino IDE
1. Install [Arduino IDE](https://www.arduino.cc/en/software) 

2. Clone or download this repository, and open the .ino file

4. Install the board package: **esp32 by Espressif Systems**

5. Select the board: **ESP32 Dev Module**

6. Under Tools, select these configurations:
    - Flash Size: 8 MB
    - Partition Scheme: 8M with SPIFFS

7. Install these Arduino libraries:
    - [SdFat by Bill Greiman](https://github.com/greiman/sdfat)
    - [Audio Tools by Phil Schatzmann](https://github.com/pschatzmann/arduino-audio-tools/tree/main)
    - [LibHelix by Phil Schatzmann](https://github.com/pschatzmann/arduino-libhelix) 
    - [LiquidCrystal_I2C by Martin Kubovcik, Frank de Brabander](https://github.com/markub3327/LiquidCrystal_I2C)



### Option 2. Using the pre-compiled binary
1. Install [esptool](https://docs.espressif.com/projects/esptool/en/latest/esp32/):
    ```
    pip install esptool
    ```

2. Download the .bin files from the latest Github Release

3. Inside the directory containing the downloaded .bin files, run:
    ```
    esptool --chip esp32 write-flash 0x1000 esp32_mp3_player.ino.bootloader.bin 0x8000 esp32_mp3_player.ino.partitions.bin 0x10000 esp32_mp3_player.ino.bin
    ```