# PCE-PS3 (v1.0.1)

Port of **Hu-Go!** PC Engine / TurboGrafx-16 / SuperGrafx / CD-ROM2 for the **PlayStation 3** (PS3) using the PSL1GHT SDK.

Ported by **theheroGAC** (2026).

---

## Authors & Credits

PCE-PS3 is powered by the legendary Hu-Go! PC Engine emulation core, created and improved by dedicated retro-computing engineers:

- **Zeograd (François-Xavier Delmotte)**: Original creator, architectural design, and primary maintainer of the Hu-Go! emulation core.
- **David Ries**: Sound engine development, PSG envelope generation, and HuC6280 CPU extensions.
- **Alex Krasivsky**: Core 6502 execution logic and base instruction set timing.
- **notaz**: CDDA Redbook audio streaming and high-speed multi-platform optimizations.
- **theheroGAC**: PlayStation 3 port (2026), PSL1GHT modern integration, low-latency 48 kHz Cell Audio ring buffer driver with hardware event queues, hardware-accelerated RSX video presentation, complete DualShock 3 active-high input mapping, custom multi-tab XMB frontend, 10-slot real-time state manager with live thumbnails, on-the-fly ZIP extractor, and CD-ROM2 CUE/ISO/ADPCM CDDA subsystem.

---

## Overview

PCE-PS3 is a high-performance, standalone homebrew emulator designed specifically for Sony PlayStation 3 consoles (compatible with all Custom Firmware such as Evilnat, Cobra CFW, and PS3HEN). 

It accurately emulates the full range of NEC 8-bit and 16-bit video game hardware:
- **PC Engine / TurboGrafx-16 (HuCard)**
- **SuperGrafx** (HuC6270 Dual-VDC graphics engine)
- **CD-ROM2 & Super CD-ROM2** (CDDA audio + ADPCM speech + System Card 3.0)
- **Arcade Card Enhanced CD Games**

Supported media file formats:
- `.pce` (PC Engine / TurboGrafx-16 cartridge ROMs)
- `.sgx` (SuperGrafx games)
- `.cue` + `.bin` (CD-ROM2 audio/data images)
- `.iso` (Standard 2048 bytes/sector CD-ROM images)
- `.zip` (Direct ZIP archive decompression on-the-fly)

---

## PS3 Port Features

- **Dedicated 48 kHz Cell Audio Engine**:
  - Direct hardware port streaming through Sony Cell Audio subsystem (`AUDIO_PORT_2CH`, 8 blocks of 256 samples).
  - Hardware event queue notification loop (`audioCreateNotifyEventQueue` + `audioSetNotifyEventQueue`) ensuring click-free, zero-stutter PSG, ADPCM, and CDDA audio synthesis.
  - Fully adjustable master audio volume slider directly inside the in-game Quick Menu and main settings.

- **Hardware-Accelerated RSX Video Presentation**:
  - Full HD output with automatic screen centering and aspect ratio control:
    - **4:3 Standard** (Classic CRT television ratio with crisp pillarboxes)
    - **Pixel-Perfect** (Integer scaling matching native HuC6270 display registers)
    - **16:9 Widescreen** (Full 1080p widescreen fill)
  - Video scaling filters: **Nearest Crisp** (sharp retro pixels), **Bilinear Smooth**, and **Anisotropic / Sharp**.
  - CRT Scanlines overlay simulation option.
  - Real-time on-screen FPS counter.
  - **In-Game Screenshot Capture**: Instant one-click screenshot capture via the In-Game Quick Menu, saving uncompressed 24-bit full-resolution BMP images to `/dev_hdd0/pce4ps3/screenshots/` with on-screen notification.

- **DualShock 3 Gamepad & Avenue Pad 6 (6-Button) Support**:
  - Full support for the official NEC **Avenue Pad 6** 6-button controller protocol with 4-scan state machine, auto-detected for all fighting games (*Street Fighter II'*, *Fatal Fury*, *Garou Densetsu*, *Art of Fighting*, *World Heroes*, *Flash Hiders*, *Advanced V.G.*, *Kabuki Ittouryoudan*, *Martial Champion*, *Strip Fighter II*, *Asuka 120%*, etc.) and toggleable in Settings and Quick Menu (`Auto (Fighting Games)`, `6-Button Arcade`, `2-Button Standard`).
  - Authentic 6-button arcade layout on DualShock 3:
    - **Square**: Button IV (Light Punch)
    - **Triangle**: Button V (Medium Punch)
    - **R1** (or L1): Button VI (Heavy Punch)
    - **Cross**: Button I (Light Kick)
    - **Circle**: Button II (Medium Kick)
    - **R2**: Button III (Heavy Kick)
  - Active-high polling logic ensuring smooth, continuous character movement without dropped frames or stuttering.
  - Configurable 2-button layout: swap Button I / Button II between Cross and Circle or Cross and Square.
  - Independent hardware Turbo Fire on L1 / R1 with selectable frequencies (15 Hz, 20 Hz, 30 Hz, 60 Hz).
  - Up to 5 simultaneous controllers supported for multiplayer games (Bomberman, Dungeon Explorer, etc.).
  - Seamless in-game Quick Menu triggered with **L2 + R2**, **L3 + R3**, or **SELECT + START**.

- **Street Fighter II' & Large HuCard Bankswitching Support**:
  - Full hardware emulation of Capcom's custom 20 Mbit (2.5 MB) bankswitching mapper (`0x1FF0`–`0x1FF3` bank registers), eliminating all character portrait tile corruptions and graphic errors in *Street Fighter II': Champion Edition*.
  - Balanced audio summing preventing digital multi-channel distortion and loud clipping.

- **Modern Multi-Tab User Interface**:
  - **Library Tab**: Automatic recursive scanner displaying games found across internal HDD and connected USB drives with clean categorization badges (`[PCE]`, `[SGX]`, `[CD]`, `[ZIP]`).
  - **File Browser Tab**: Full directory navigation across `/dev_hdd0` and all USB storage devices (`/dev_usb000` through `/dev_usb007`).
  - **Save States Tab**: 10 independent save state slots per game with real-time RGB thumbnail screenshot previews and single-button delete / restore.
  - **Settings Tab**: Display aspect, video filters, scanlines, master volume, button mapping, and turbo rate saved to persistent `hugo.ini`.
  - **BIOS & CD-ROM2 Tab**: System Card status checker and one-click USB BIOS auto-importer.
  - **Controls & Info Tab**: Full button diagram and port credits.

- **Robust Error Handling & Stability**:
  - Comprehensive ROM and ZIP verification preventing console freezes when non-PCE or corrupted files are selected.
  - Helpful graphical modal alert dialogs informing the user when a BIOS is missing or an invalid file format is loaded.
  - Clean PS Button XMB shutdown with proper SysUtil event handling and resource cleanup without freezes or double-beeps.

---

## Controls

### Frontend & Menu Navigation
| Button | Action |
|---|---|
| **D-Pad / Left Stick** | Navigate items / Change settings (smooth rapid scrolling when held) |
| **D-Pad Left / Right** | Page Jump (Scroll 10 items at a time in Library and Browser) |
| **Cross (X)** | Confirm / Select / Launch Game / Exit to XMB (on CREDITS & EXIT tab) |
| **Circle (O)** | Cancel / Return / Back |
| **Square** | Switch Library Media Filter (`ALL` / `HUCARD` / `SUPERGRAFX` / `CD-ROM2`) |
| **Triangle** | Rescan Library / Import BIOS / Delete Save State |
| **L1 / R1** | Switch Tabs (`LIBRARY` ↔ `BROWSER` ↔ `STATES` ↔ `SETTINGS` ↔ `BIOS` ↔ `CREDITS & EXIT`) |
| **SELECT** | Cycle Root Quick-Bookmarks in File Browser (`/dev_hdd0`, `/dev_usb000`, `/dev_usb001`) |

### In-Game Controls
| Button | PC Engine Function |
|---|---|
| **D-Pad / Left Analog Stick** | Directional Pad (Up, Down, Left, Right) |
| **Circle (or Cross)** | Button I (Primary Action / Confirm) |
| **Cross (or Square)** | Button II (Secondary Action / Jump) |
| **Triangle** | Turbo Button I (Rapid Fire) |
| **Square (or Circle)** | Turbo Button II (Rapid Fire) |
| **START** | RUN Button (Start / Pause) |
| **SELECT** | SELECT Button (PC Engine) |
| **R1** | Dedicated Turbo Fire Button I |
| **L1** | Dedicated Turbo Fire Button II |
| **L2 + R2** | In-Game Quick Menu (Save/Load State, Reset, Exit) |
| **L3 + R3** | Alternative In-Game Quick Menu Shortcut |
| **SELECT + START** | Classic In-Game Quick Menu Shortcut |
| **Clean Exit to XMB** | Navigate to the last tab **`CREDITS & EXIT`** and press **Cross (X)**, or select **`Exit PCE-PS3 (Return to XMB)`** from the In-Game Quick Menu or Settings tab. The `SELECT` button is never used for exiting, avoiding any accidental exit during gameplay or navigation. |

---

## Directory Structure on PS3

At first launch, the emulator creates the necessary directory tree on the internal hard drive:

```
/dev_hdd0/pce4ps3/
  ├── roms/        PC Engine ROMs (.pce, .sgx, .zip, .cue, .iso)
  ├── bios/        Super CD-ROM2 System Card (syscard3.pce)
  ├── saves/       10-slot save states (.s00-.s09) and thumbnail images (.thm)
  ├── screenshots/ Uncompressed 24-bit BMP screenshots (pce_snap_0001.bmp)
  ├── bram/        Battery-backed 8KB Backup RAM per game (.brm)
  ├── hugo.ini     Persistent configuration file
  └── bootlog.txt  Diagnostic log output
```

### ROM Locations
The Library and File Browser automatically detect games stored in any of the following locations:
- `/dev_hdd0/pce4ps3/roms/`
- `/dev_hdd0/ROMS/pce/` or `/dev_hdd0/roms/pce/`
- External USB drives: `/dev_usb000` through `/dev_usb007` (in `/roms/pce/`, `/pce/`, `/ROMS/`, or drive root)

### CD-ROM2 System Card (BIOS)
CD-ROM2 and Super CD-ROM2 games require the NEC Super CD-ROM2 System Card BIOS:
- Place `syscard3.pce` into `/dev_hdd0/pce4ps3/bios/syscard3.pce`, OR
- Place `syscard3.pce` on the root of your USB flash drive and press **Triangle** in the **BIOS & CD-ROM2** tab to automatically import it with a single click.

---

## Building from Source

Requires the **PSL1GHT v2** SDK (or `ps3dev-sdk`) environment.

```bash
export PS3DEV=/path/to/ps3dev-sdk
export PSL1GHT=$PS3DEV/psl1ght
export PATH=$PS3DEV/bin:$PS3DEV/ppu/bin:$PSL1GHT/host/bin:$PATH

./build.sh
./package.sh
```

The build scripts generate:
- `dist/PCE4PS3.pkg`: Standard unsigned package for Custom Firmware (CFW) and PS3HEN.
- `dist/PCE4PS3.gnpdrm.pkg`: Signed retail NPDRM package.
- `pce_ps3.elf`: Compiled Cell PPU executable.

---

## License & Disclaimer

PCE-PS3 is released under the GNU General Public License (GPL).  
PC Engine and TurboGrafx-16 are trademarks of NEC Home Electronics and Hudson Soft. This software is an independent open-source project and is not affiliated with or endorsed by Sony Interactive Entertainment, NEC, or Hudson Soft.
