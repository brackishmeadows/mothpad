# Mothpad Build Handoff

Status: SD-backed edit build

Date verified: 2026-07-05

## What Builds

Mothpad currently has two verified local build paths:

- Host C tests on Windows using MSYS2 GCC.
- Pico SDK firmware build for `pico2_w`, producing clean and legacy UF2s.

The verified Pico UF2 is not the full PicoCalc editor UI yet. It initializes
the Mothpad C core, renders the `MothCell` grid to the PicoCalc LCD, accepts
basic keyboard editing, mounts the SD card through `pico-vfs`, and supports
minimal open/save.

Important hardware note: `mothpad_pico.uf2` is the known-good clean PicoCalc
build as of 2026-07-05. The clean LCD path needs two fixes: wait for SPI idle
before releasing LCD chip-select, and reassert LCD pixel format `0x3A = 0x66`
after the final display mode/MADCTL writes. Keep `mothpad_pico_legacy.uf2` as
fallback only.

## Important Paths

Project:

```text
C:\Users\iamru\OneDrive\Desktop\field-office\picocalc\mothpad
```

Pico SDK:

```text
C:\Users\iamru\OneDrive\Desktop\field-office\picocalc\toolchains\pico-sdk
```

MSYS2:

```text
C:\msys64
```

Generated UF2:

```text
picocalc\mothpad\c\pico\build-pico2_w\mothpad_pico_legacy.uf2
picocalc\mothpad\c\pico\build-pico2_w\mothpad_pico.uf2
```

Pico VFS dependency:

```text
picocalc\toolchains\PicoCalc-upstream\Code\pico_multi_booter\sd_boot\lib\pico-vfs
```

## Installed Toolchain

Installed via MSYS2 pacman:

```text
mingw-w64-x86_64-cmake
mingw-w64-x86_64-ninja
mingw-w64-x86_64-arm-none-eabi-binutils
mingw-w64-x86_64-arm-none-eabi-gcc
mingw-w64-x86_64-arm-none-eabi-newlib
```

Observed versions:

```text
cmake 4.2.1
ninja 1.13.2
arm-none-eabi-gcc 13.3.0
host gcc 15.2.0
```

Pico SDK commit:

```text
98a542c
```

If Git complains about dubious ownership for the SDK, use a one-shot safe
directory flag rather than changing global config:

```powershell
git -c safe.directory=C:/Users/iamru/OneDrive/Desktop/field-office/picocalc/toolchains/pico-sdk -C picocalc\toolchains\pico-sdk rev-parse --short HEAD
```

## Host C Test Build

Run from Field Office root:

```powershell
powershell -ExecutionPolicy Bypass -File .\picocalc\mothpad\c\build.ps1
```

Expected output:

```text
Compiler: C:\msys64\mingw64\bin\gcc.exe
mothpad C tests passed
```

The script prepends `C:\msys64\mingw64\bin` to `PATH`. Do not remove that. MSYS2
GCC may find `gcc.exe` but silently fail when child tools cannot find DLLs.
That was the little trapdoor.

## Pico 2 W UF2 Build

Run from Field Office root:

```powershell
$env:PICO_SDK_PATH = (Resolve-Path 'picocalc\toolchains\pico-sdk').Path
powershell -ExecutionPolicy Bypass -File .\picocalc\mothpad\c\pico\build-pico.ps1 -Board pico2_w
```

Expected artifact:

```text
picocalc\mothpad\c\pico\build-pico2_w\mothpad_pico_legacy.uf2
picocalc\mothpad\c\pico\build-pico2_w\mothpad_pico.uf2
```

Last verified size:

```text
mothpad_pico.uf2        336896 bytes
mothpad_pico_legacy.uf2 342528 bytes
```

Current PicoCalc cell geometry:

```text
40 columns x 26 rows
8x12 cells on a 320x320 LCD
```

Rows 0 and 25 are status bars. Rows 1 through 24 are the document viewport.

Other possible board values:

```text
pico
pico2
pico2_w
```

Use `pico2_w` for the current PicoCalc clue, because the SD-card dump contains:

```text
picocalc\sdcard\pico2-apps\Picoware-PicoCalcPico2W.uf2
```

## Testing On Device

With Pelrun's UF2 Loader, copy the UF2 to the Pico 2 app folder on the SD card:

```text
pico2-apps\mothpad_pico.uf2
```

Then launch it from the loader menu. The Pelrun loader runs standard pico-sdk
UF2 files directly from SD when the app does not write to flash, so Mothpad does
not need the official ClockworkPi multibooter `.bin` offset path for this test.

Expected behavior on this build:

- PicoCalc screen should clear and start in a blank editable document.
- Printable keys should insert text.
- Arrows should move the cursor.
- Enter, Tab, Backspace, Delete, Home, and End should work.
- Ctrl+S should save the current file, or open a save-as prompt if unnamed.
- Ctrl+O should open a file list for the current directory.
- Ctrl+Z should undo the last edit while undo history is available.
- Save uses `.tmp` and `.bak` rename behavior from the core safe-save path.
- If SD mount fails, the editor remains usable in memory and shows a short error.

```text
Ctrl+S -> centered Save As popup for unnamed files
Ctrl+O -> file list, with a text peek pane when useful
Ctrl+Z -> undo
```

If `mothpad_pico.uf2` is blank, retest `mothpad_diag.uf2` and
`mothpad_pico_legacy.uf2`. If only the clean editor is blank, suspect SD mount
or editor startup. If clean diag is blank too, suspect the clean LCD driver. If
legacy is blank too, check board target (`pico2_w` vs `pico`), loader
selection, and whether another known-good PicoCalc LCD app works on the same
hardware.

If the device needs Picoware restored afterward, relaunch or reflash the known
Picoware UF2 from the SD-card dump.

Confirmed hardware result on 2026-07-04: the UF2 launched from Pelrun's loader
on PicoCalc and displayed the Mothpad cell grid, including the `[new] MOD` top
status bar and `Ln 03 Col 21` bottom status bar. The first visible smoke build
had the final line inserted before the title because the smoke test used
`moth_insert_text()` after `moth_set_text()`; the code now uses one deterministic
test document string.

Live-edit build note: hardware testing on 2026-07-05 showed the clean
`mothpad_pico.uf2` works after two LCD-driver repairs. First,
`lcd_spi_finish()` must run before raising LCD CS, or the app can blank-screen
because SPI transactions end before the peripheral finishes shifting pixel
data. Second, the driver must reassert LCD pixel format `0x3A = 0x66` after the
final `0x36`/MADCTL write. Without that second write, colors can appear as
cyan/magenta/yellow substitutions, text can shimmer cyan, and first pixels of
filled blocks can corrupt. `mothpad_pico_legacy.uf2` also works and remains the
fallback path over the ClockworkPi hello-world LCD/keyboard sources.

SD-backed build note: this build populated ClockworkPi's declared
`pico-vfs` submodule from `https://github.com/oyama/pico-vfs.git`, links
`blockdevice_sd`, `filesystem_fat`, and `filesystem_vfs`, and mounts the SD card
at `/` using SPI0 pins SCK 18, MOSI 19, MISO 16, CS 17, detect 22. It does not
format on mount failure.

## Common Failure Modes

`PICO_SDK_PATH is not set`

Set it:

```powershell
$env:PICO_SDK_PATH = (Resolve-Path 'picocalc\toolchains\pico-sdk').Path
```

`cmake` or `arm-none-eabi-gcc` not found

Make sure `C:\msys64\mingw64\bin` is on `PATH`. The build wrapper does this.

Silent host GCC failure after `cc1.exe`

Same fix: prepend `C:\msys64\mingw64\bin` to `PATH`.

Picotool fetch failure

The first Pico build may fetch `raspberrypi/picotool` through the Pico SDK. If
network is sandboxed, rerun with escalation.

## Next Build Work

The next useful engineering step is the SD acceptance pass, then the rest of
the editor shell:

- verify save-as, reopen, edit, save-again, and `.bak` behavior on SD.
- save result / return-to-caller contract.
- new/quit command UI.
- better file list sorting/filtering.
- a public-release notice bundle for Pico SDK, `pico-vfs`, and FatFs.

Do not bury those inside `mothpad.c`. The core is clean. Keep it clean.
