# PTPE-RP2350 — Wi-Fi Skylanders Portal

An experimental RP2350 adaptation of **PTPE**, originally developed by [NicoAICP](https://github.com/NicoAICP/PTPE).

This version explores using a Raspberry Pi Pico 2 W as a virtual Skylanders Portal, with Wi-Fi access for uploading Skylanders dump files and USB HID emulation.

> **Development status:** The firmware builds successfully, the Wi-Fi access point and web upload have been tested on the Pico 2 W, and Windows recognizes the device as USB HID. Communication with an Xbox 360 has not yet been validated.

## Credits and original project

This project is based on [NicoAICP/PTPE](https://github.com/NicoAICP/PTPE).

The original project provides the foundation for the portal emulation code and related functionality. This repository contains experimental adaptations for the RP2350, including Wi-Fi access, a web interface, and RAM-based dump loading.

- **Original project:** [NicoAICP/PTPE](https://github.com/NicoAICP/PTPE)
- **Original developer:** [NicoAICP](https://github.com/NicoAICP)
- **Original project support:** [Ko-Fi](https://ko-fi.com/nicoaicp)
- **Original project Discord:** [Join the Discord](https://discord.gg/zx6RAAw7YJ)

This is an independent development repository, not an official release of the original project.

Please refer to the included `LICENSE` file for the applicable license terms.

## Features

- Raspberry Pi Pico 2 W / RP2350 target.
- Wi-Fi access point hosted by the Pico.
- Browser-based interface for uploading Skylanders dump files.
- RAM-based storage for the currently uploaded dump.
- USB HID device enumeration.
- Experimental integration with the existing PTPE portal emulation code.

## Current development status

| Feature | Status |
|---|---|
| Firmware compilation | Confirmed |
| Wi-Fi access point | Tested on hardware |
| Web interface | Tested from a phone |
| Skylanders dump upload | Tested with `Spyro.bin` (1024 bytes) |
| Dump stored in RAM | Confirmed by web interface |
| USB HID enumeration on Windows | Confirmed |
| USB report protocol | Not yet fully validated |
| Xbox 360 compatibility | Not yet tested |
| Full Skylanders read/write support | Not confirmed |

## Using the Wi-Fi interface

1. Power the Pico 2 W with the firmware running.
2. Connect your phone or computer to the following Wi-Fi network:

   - **SSID:** `PTPE-Skylanders`
   - **Password:** `SkyPortal26`

3. Open the following address in a browser:

   `http://192.168.4.1`

4. Upload a Skylanders dump file (`.bin`).

The current firmware accepts one dump at a time. The dump must be between 16 and 2048 bytes, inclusive, and its size must be a multiple of 16 bytes.

The dump is held in RAM, so it must be uploaded again after a restart or power loss.

These are the current development defaults and may change in future versions.

## Building the firmware

### Requirements

- Raspberry Pi Pico SDK
- CMake
- Ninja or another supported CMake build generator
- ARM GNU Toolchain for `arm-none-eabi`
- A compatible C/C++ build environment
- The repository's required Git submodules

### Getting the source

Clone this repository and initialize its submodules:

```bash
git clone https://github.com/RobertoWaide/PTPE-RP2350.git
cd PTPE-RP2350
git submodule update --init --recursive
```

Set `PICO_SDK_PATH` to the location of your Raspberry Pi Pico SDK installation.

Make sure the TinyUSB configuration required by this project is installed as described in the original PTPE build instructions.

### Building with CMake and Ninja

From the repository root, configure the build for the Pico 2 W:

```bash
cmake -S . -B build-ninja -G Ninja -DPICO_BOARD=pico2_w
```

Then build:

```bash
cmake --build build-ninja -j 4
```

The generated UF2 firmware should be located at:

`build-ninja/ptpe.uf2`

The exact build requirements may vary depending on your operating system and SDK/toolchain setup. The commands above describe the current CMake/Ninja workflow; a clean build on a separate machine should be verified.

## Hardware connections

The following connections are documented by the original PTPE project. Their functionality has not necessarily been verified with the current Wi-Fi development firmware.

### LCD

| Signal | Pico GPIO / Pin |
|---|---|
| SCK | GP14 / Pin 19 |
| SDA | GP15 / Pin 20 |
| A0 | GP10 / Pin 14 |
| RESET | GP11 / Pin 15 |
| CS | GP9 / Pin 12 |
| LED | 3.3V / Pin 36 |
| GND | GND / Pin 38 |
| VCC | 5V / Pin 40 |

### SD card reader

| Signal | Pico GPIO / Pin |
|---|---|
| SD_CS | GP5 / Pin 7 |
| SD_MOSI | GP3 / Pin 5 |
| SD_MISO | GP4 / Pin 6 |
| SD_SCK | GP2 / Pin 4 |

### Buttons

Five buttons are documented by the original project:

| Button | Function | Pico GPIO / Pin |
|---|---|---|
| Button 1 | Misc 1 | GP16 / Pin 21 |
| Button 2 | Left / Down | GP17 / Pin 22 |
| Button 3 | Select | GP18 / Pin 24 |
| Button 4 | Right / Up | GP19 / Pin 25 |
| Button 5 | Misc 2 | GP20 / Pin 26 |

## Known limitations

- The current development firmware starts the Wi-Fi portal interface directly.
- The current upload implementation keeps one dump in RAM; it is not a collection manager.
- Uploading a dump and detecting the Pico as a USB HID device do not, by themselves, prove that the Skylanders protocol works correctly.
- Xbox 360 communication and actual in-game Skylanders detection remain to be tested.
- Full portal functionality, including all original LCD, SD card, button, and game interaction features, has not been validated in this configuration.

## Contributing and