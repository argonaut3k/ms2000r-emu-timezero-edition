# ms2000r-emu TimeZero Edition v0.9.9_02

## What's Changed

### Preset Manager:
- **Fixed accidental preset renaming on double-click. Renaming is now available via the context menu.**
- **Added keyboard arrow navigation through the preset list.**
- **Added scrolling support for large preset collections and imported banks.**

[![KORG MS2000R](https://img.shields.io/badge/KORG-MS2000R-black?logo=audio&logoColor=white&style=flat-square)](https://github.com/paulhuman/ms2000r-emu)
[![Windows](https://img.shields.io/badge/platform-Windows%20x64-informational?logo=windows&style=flat-square)](https://github.com/paulhuman/ms2000r-emu/releases)
[![CI](https://img.shields.io/github/actions/workflow/status/paulhuman/ms2000r-emu/ci.yml?label=ci&style=flat-square)](https://github.com/paulhuman/ms2000r-emu/actions)
[![Release](https://img.shields.io/github/v/release/argonaut3k/ms2000r-emu-timezero-edition?include_prereleases&label=release&color=orange&style=flat-square)](https://github.com/argonaut3k/ms2000r-emu-timezero-edition/releases)
[![Downloads](https://img.shields.io/github/downloads/argonaut3k/ms2000r-emu-timezero-edition/total?color=yellow&style=flat-square)](https://github.com/argonaut3k/ms2000r-emu-timezero-edition/releases)
[![License](https://img.shields.io/github/license/paulhuman/ms2000r-emu?color=lightgray&style=flat-square)](https://github.com/paulhuman/ms2000r-emu/blob/main/LICENSE.md)

A hardware-level emulator of the **KORG MS2000R**, the rack version of KORG's analog-modeling synthesizer.

The original H8S/2350 firmware runs instruction by instruction, while the DSP56362 executes the original DSP code through the dsp56300 emulator library. The synthesis engine is not reimplemented — the sound comes directly from the original machine code.

Available as a standalone application (Windows/macOS, ImGui) and a VST3 instrument plugin (JUCE; Audio Unit on macOS).

## TimeZero Edition

This fork tracks upstream `Tuth/ms2000r-emu` v0.9.9 and includes selected improvements.

- **Upstream v0.9.9:** Profile-guided optimization (PGO), approximately 14% faster standalone and 7% lower plugin CPU usage, with bit-identical audio. Includes a crisp dot-matrix LCD at any window size.
- **Program-aware knobs:** Controls automatically reflect the loaded program's values. Can be disabled via `Knobs show the program`.
- **Latency reporting:** Optional host reporting of approximately 21 ms firmware latency for plugin delay compensation.
- **Editor resizing:** Preserves the original panel aspect ratio instead of distorting the interface.
- **Project state recall:** Restores the edit buffer, program slot, GLOBAL buffer, AudioInThru setting and LCD navigation state when reopening a DAW project.
- **Preset Manager:** Extended bank management, clipboard operations, drag-and-drop reordering and .syx/.mid import and export.
- **v0.9.9_02:** Improved preset navigation, fixed accidental renaming on double-click and added scrolling for large preset collections.

## Features & Status

### Hardware emulation
- Original firmware, programs, vocoder, arpeggiator, motion/mod sequencer and demo songs.
- MIDI IN/OUT, including knob movements transmitting CC messages.
- Global and program WRITE with persistent emulated flash memory.
- Full front panel: 32 knobs, switch matrix, multiplexed LED matrix and 16×2 LCD with custom characters.
- Shift+click holds buttons, supporting key combinations and service-mode shortcuts.
- AK4522 DAC digital filtering, output coupling and modeled 10 kΩ linear Master Volume control.
- POWER/VOLUME switch behavior, including shutdown and cold boot. Can be disabled in plugin Settings.
- Adjustable output boost: 0 to +12 dB in 1 dB steps.
- Audio Inputs 1/2 with modeled input stage and AK4522 codec for vocoder and OSC1 AUDIO IN.

### Plugin integration
- 59 host parameters representing physical controls: 32 knobs, POWER/VOLUME, AUDIO IN controls and 23 performance keys.
- Full DAW automation with Write/Touch support.
- MIDI CC remains firmware-controlled; automation and MIDI CC tracks do not conflict.
- Host tempo via MIDI Clock and transport via Start/Stop.
- Multiple independent plugin instances in the same host.
- Complete machine state saved in DAW projects.
- VST3 on Windows/macOS and Audio Unit on macOS.

### Preset Manager

The plugin includes a dedicated Patches page with banks on the left and programs on the right.

**User Banks**
- Create and manage custom banks containing up to 128 programs each.
- Stored in `Documents/UserName/ms2k/patchmanager/` as `.ms2kbank` files.
- Add, rename, remove, delete, refresh and export banks as .syx/.mid.
- Add the current patch, paste from clipboard or reveal files in the browser.
- Rename, delete, overwrite, copy and paste individual programs.
- Drag and drop programs to reorder them.
- Keyboard Up/Down navigation through the preset list.
- Double-click no longer triggers accidental renaming; use the context menu to rename.
- Automatic scrolling for large bank collections.

**Data Sources**
- Import .syx/.mid files or recursively scan folders.
- Supports banks containing any number of programs.
- Imported data is cached in `patchmanager/cache`.
- Refresh, remove or reveal sources in the file browser.
- Copy programs to the clipboard or add them to User Banks.

**Clipboard**
- Copy the currently playing patch directly from the front panel.
- Paste patches into User Banks or transfer them between plugin instances.
- Uses the `MS2000R-PATCH-1:` format with 508 hexadecimal digits.

**Export**
- Exports complete 128-program banks in .syx format.
- Unused slots retain the synthesizer's existing programs where available; otherwise, empty slots are filled with copies of the first entry.
- The synthesizer's memory is only changed through its own WRITE operation or by loading an exported bank.
- The original `MS2000R/userbank.ms2kbank` is migrated to `User Bank` during initial setup.

### MIDI & Program Management
- Load .syx banks and individual programs through emulated MIDI IN.
- Save all 128 programs as .syx.
- Firmware handles data loading and reports DATA LOAD COMPLETED / ERROR.
- Host tempo and transport synchronization.
- Optional `Knobs show the program` mode keeps physical controls aligned with the current edit buffer.

### Flash Tool
The upstream Windows release includes MS2000 Flash Tool for dumping the complete 1 MB flash from your own MS2000/MS2000R over MIDI.

Requires the original unit's update mode and KORG v1.07 updater file. **Do not use with MS2000B/MS2000BR.**

See [Flash Tool documentation](source/tools/flashdump/README.md) and [IPL protocol](docs/ipl_protocol.md).

## Known Limitations

- GUI and plugins support Windows and macOS. Linux supports headless standalone builds only.
- Requires a recent AVX2-capable CPU.
- Performance is approximately 2× real time on a modern desktop.
- Plugin benchmark: 96 kHz / 128-sample buffer, approximately 0.62 ms median and 0.96 ms worst-case processing time per 1.33 ms block after boot.
- VSTHost may stop processing plugins that send SysEx to the host. Disable `MIDI OUT SysEx -> host` in plugin Settings if necessary.

## Download & Installation

[**Latest Release**](https://github.com/argonaut3k/ms2000r-emu-timezero-edition/releases/latest)

TimeZero Edition releases provide the **Windows x64 VST3 plugin bundle** (`MS2000R-v<version>-win64.zip`).

1. Extract the archive.
2. Copy `MS2000R.vst3` into your VST3 directory (usually `C:\Program Files\Common Files\VST3`).
3. On first launch, open Settings → `Choose the MS2000 folder...` and select your firmware directory.

The selected path is saved in `%APPDATA%\MS2000R\home.txt`.

Requires Microsoft Visual C++ 2015–2022 x64 runtime.

The standalone application is not included in this fork's releases. Build it from source or download official Windows/macOS builds from [upstream](https://github.com/Tuth/ms2000r-emu).

## Firmware & ROM Requirements

**Firmware, ROMs and manuals are not included.** You must obtain dumps from your own hardware.

| File | Description | Required |
|---|---|---|
| `flash.bin` | Main MS2000R flash, 1 MB (MBM29LV800BA) | Yes |
| `boot-362.bin` | DSP56362 boot ROM, 768 bytes (192 × 4, little-endian). May be placed beside `flash.bin` or in `full FW`. Known bad words are corrected automatically. | Yes |
| `hd44780_a00.bin` | LCD character generator ROM, 4096 bytes | No |

A corrected `boot-362.ms2000.bin` takes priority when present.

The working directory is detected automatically: `MS2K_HOME`, current directory, executable/plugin directory or parent directories containing the required ROMs.

Settings (`thin_gui.ini`), machine memory and recordings are stored in this directory.

The original `flash.bin` is never modified. Firmware WRITE operations are saved separately in `ms2000_flash_state.bin` / `.sectors` or inside the DAW project for the plugin.

Missing files are reported in the console or plugin interface.

## Build

### Windows

Requirements: Visual Studio 2022, CMake 3.16+ and [vcpkg](https://github.com/microsoft/vcpkg) with `VCPKG_ROOT` configured.

```bash
cmake -S . -B build -G "Visual Studio 17 2022"
cmake --build build --config Release --target ms2000_emulator
build\Release\ms2000_emulator.exe --gui
```

For the VST3 plugin:

```bash
cmake -S . -B build_vst -G "Visual Studio 17 2022" -DMS2K_BUILD_VST3=ON
cmake --build build_vst --config Release --target MS2000R_Shim
```

The plugin bundle is generated in `build_vst\VST3_multi\MS2000R.vst3`.

`dist\MS2000R.bat` and `dist\MS2000R (log).bat` are the standalone launchers.

### Linux (Headless)

Requires GCC 13 or Clang, CMake 3.16+, Ninja, ALSA development headers (`libasound2-dev`) and AVX2.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target ms2000_emulator
cd <ROM folder> && <repo>/build/ms2000_emulator --headless --selftest-full
```

Render audio using scheduled MIDI input:

```bash
MS2K_MIDIIN="2:C008;2.3:903C64;3.8:803C00" <repo>/build/ms2000_emulator --headless --quiet-boot --duration 6 --render-test out.wav
```

`MS2K_MIDIIN` schedules MIDI bytes at MCU timestamps. `--render-test` exports DSP output as 32-bit WAV. Run `ctest` in the build directory for the test suite.

### macOS

Supports native Apple Silicon builds using the ARM64 DSP JIT with bit-identical audio to x64.

Requirements: Xcode Command Line Tools, CMake 3.16+ and Ninja.

```bash
cmake -S . -B build.nosync -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build.nosync --target MS2000R_app
```

The standalone application is generated at `build.nosync/MS2000R.app`. It can locate ROMs automatically or prompt for their folder on first launch.

For plugin builds (VST3 and AU):

```bash
cmake -S . -B build_vst.nosync -G Ninja -DCMAKE_BUILD_TYPE=Release -DMS2K_BUILD_VST3=ON
cmake --build build_vst.nosync --target MS2000R_Shim MS2000R_AUShim
```

Install the bundles:

```bash
cp -R build_vst.nosync/VST3_multi/MS2000R.vst3 ~/Library/Audio/Plug-Ins/VST3/
cp -R build_vst.nosync/AU_multi/MS2000R.component ~/Library/Audio/Plug-Ins/Components/
```

Multiple instances run independently. Builds are ad-hoc signed; distribution to other Macs requires Developer ID signing and notarization.

For x64 builds under Rosetta 2, add:

```bash
-DCMAKE_OSX_ARCHITECTURES=x86_64
```

**Note:** Use a `.nosync` build directory when working inside iCloud-synchronized folders to avoid build failures caused by duplicated files.

### Profile-Guided Optimization (PGO)

Release builds use PGO for improved performance without changing audio output.

Windows:

```powershell
powershell -File tools\pgo_build.ps1 -RomDir <ROM folder>
```

The script instruments both standalone and plugin builds, trains them using the emulator and host tests, then rebuilds with the collected profile.

Requires firmware files and takes approximately 15 minutes. The profile is specific to the exact source revision; disable PGO for development builds using `-DMS2K_PGO=OFF`.

## Credits

- **Claude (Anthropic)** — original implementation in collaboration with **Tamás Bársony (Tuth)**, who provided the hardware, testing, measurements, photos, manuals and firmware dumps.
- **The Usual Suspects (TUS)** — DSP56300 emulation through the [gearmulator](https://github.com/dsp56300/gearmulator) project.
- **Unknown Technologies** — technical assistance, discussions and bug reports.
- **JUCE** — plugin framework.
- **Dear ImGui** — standalone interface.

## License

GPL-3.0. See `LICENSE.md`.

KORG and MS2000 are trademarks of KORG Inc. This project is not affiliated with or endorsed by KORG.
