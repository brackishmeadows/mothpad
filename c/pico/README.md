# Mothpad Pico Build

This folder is a Pico SDK build target for Mothpad's C core. It is not yet the
full PicoCalc app shell, but it now boots into a blank editable document, draws
to the PicoCalc LCD, accepts keyboard input, and can open/save files on the SD
card.

For the full verified build handoff, read `../../docs/build-handoff.md`.

## Requirements

- Raspberry Pi Pico SDK installed somewhere local.
- `PICO_SDK_PATH` set to that SDK folder.
- CMake and an ARM GCC toolchain available to CMake.

Field Office setup note: MSYS2 packages provide CMake, Ninja, and
`arm-none-eabi-gcc` from `C:\msys64\mingw64\bin`. The Pico SDK is cloned at
`picocalc\toolchains\pico-sdk`.

The clean PicoCalc hardware path uses Mothpad-owned LCD/keyboard code in this
folder, plus the `pico-vfs` dependency from:

```text
picocalc\toolchains\PicoCalc-upstream\Code\pico_multi_booter\sd_boot\lib\pico-vfs
```

For licensing notes, read `../../THIRD_PARTY.md`.

Important: `mothpad_pico.uf2` is now the known-good clean hardware build on
PicoCalc through Pelrun's UF2 Loader. The clean LCD path needs two details:
wait for SPI idle before releasing LCD chip-select, and reassert LCD pixel
format command `0x3A = 0x66` after the final display mode/MADCTL writes.
Keep `mothpad_pico_legacy.uf2` as a fallback only.

## Configure

For RP2040-style Pico targets:

```powershell
cmake -S picocalc\mothpad\c\pico -B picocalc\mothpad\c\pico\build -DPICO_BOARD=pico
```

For Pico 2 / RP2350-style targets, assuming the installed SDK supports it:

```powershell
cmake -S picocalc\mothpad\c\pico -B picocalc\mothpad\c\pico\build-pico2 -DPICO_BOARD=pico2
```

For Pico 2 W / RP2350 Wi-Fi style targets, which matches the local
`Picoware-PicoCalcPico2W.uf2` naming:

```powershell
cmake -S picocalc\mothpad\c\pico -B picocalc\mothpad\c\pico\build-pico2w -DPICO_BOARD=pico2_w
```

Then build:

```powershell
cmake --build picocalc\mothpad\c\pico\build
```

Expected output includes `mothpad_pico.uf2`, `mothpad_pico_legacy.uf2`, and
diagnostic UF2s.

Or use the wrapper:

```powershell
powershell -ExecutionPolicy Bypass -File .\picocalc\mothpad\c\pico\build-pico.ps1 -Board pico2_w
```

## Current Behavior

The clean target is `mothpad_pico`. It initializes the Mothpad core, renders
`MothCell` cells to the PicoCalc LCD through `mothpad_picocalc_platform.c`, and
polls the PicoCalc I2C keyboard through the same platform shim. The PicoCalc LCD
path currently uses a `40x26` grid from 8x12 cells, and keypress redraws are
diffed at the cell level instead of clearing the whole screen. It mounts the SD
card at `/` through `pico-vfs` and does not format the card on mount failure.

Build outputs:

```text
c/pico/build-pico2_w/mothpad_pico.uf2
c/pico/build-pico2_w/mothpad_pico_legacy.uf2
```

`mothpad_pico.uf2` is verified on hardware with the clean LCD/keyboard path and
SD-backed editing. `mothpad_diag.uf2` remains useful for LCD/key diagnostics.
`mothpad_pico_legacy.uf2` is the old ClockworkPi-source-backed fallback.

For Pelrun's UF2 Loader, copy the clean UF2 to the SD card:

```text
pico2-apps\mothpad_pico.uf2
```

Then launch it from the loader menu. Expected behavior:

```text
blank editable document
Ctrl+S save / save-as
Ctrl+O open file list for the current directory
```

Expected working keys:

- printable ASCII characters
- Enter
- Tab, inserted as a tab character
- Backspace
- Delete
- arrow keys
- Home and End
- Ctrl+S save
- Ctrl+O open

## Hardware Notes

- LCD SPI1 pins: SCK 10, MOSI 11, MISO 12, CS 13, DC 14, RST 15.
- Keyboard I2C1 pins: SDA 6, SCL 7, address `0x1f`, key register `0x09`.
- SD SPI0 pins: SCK 18, MOSI 19, MISO 16, CS 17, detect 22.
- The clean LCD driver uses 18-bit RGB888 writes. If colors look inverted,
  cyan-fringed, or single-pixel-corrupted, check that `0x3A = 0x66` is sent
  after the final `0x36`/MADCTL write.

## What Still Needs The PicoCalc Shell

- new/quit command UI
- app lifecycle: launch, return result, dirty quit prompt
- public-release notice bundle
