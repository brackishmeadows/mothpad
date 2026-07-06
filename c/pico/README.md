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

From the Field Office root, the currently verified explicit build command is:

```powershell
$env:PICO_SDK_PATH = (Resolve-Path 'picocalc\toolchains\pico-sdk').Path
powershell -ExecutionPolicy Bypass -File .\picocalc\mothpad\c\pico\build-pico.ps1 -Board pico2_w -BuildDir .\picocalc\mothpad\c\pico\build-pico2_w_mothpad
```

That writes the tested UF2 to:

```text
picocalc\mothpad\c\pico\build-pico2_w_mothpad\mothpad_pico.uf2
```

## Current Behavior

The clean target is `mothpad_pico`. It initializes the Mothpad core, renders
`MothCell` cells to the PicoCalc LCD through `mothpad_picocalc_platform.c`, and
polls the PicoCalc I2C keyboard through the same platform shim. The PicoCalc LCD
path currently uses a `40x26` grid from 8x12 cells, and keypress redraws are
diffed at the cell level instead of clearing the whole screen. It mounts the SD
card at `/` through `pico-vfs` and does not format the card on mount failure.
The top status row also shows the keyboard controller's battery percent when
available.
On boot, the clean target shows the embedded `mothpad-splash.png` artwork for
about half a second before clearing into the editor or recovery prompt.

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
F1 open the File menu
F2 open the Edit menu
F3 open the Select menu
F1 then Right cycles through Edit and Select
Ctrl+Z undo the last edit
Ctrl+F open Find, then repeat the last find
Shift+Arrows select text on the clean target
```

Expected working keys:

- printable ASCII characters
- Enter
- Tab, inserted as a tab character
- Backspace
- Delete
- arrow keys
- Home and End
- F1 File menu
- F2 Edit menu, or F1 then Right
- F3 Select menu, or F1 then Right twice
- Ctrl+S save
- Ctrl+O open
- Ctrl+F find, then find next
- Ctrl+Z undo
- Ctrl+C copy current line
- Ctrl+X cut current line
- Ctrl+V paste internal clipboard
- Shift+Arrow text selection on the clean target

The clean keyboard shim enables modifier reporting, samples the PicoCalc C64
matrix for physical Shift, and reads the joystick register for horizontal arrow
state. This is needed because Shift+Up/Down are reported as PageUp/PageDown by
the keyboard firmware, while Shift+Left/Right do not have alternate shifted
keycodes and regular Left/Right FIFO events can be suppressed while Shift is
held.

The File menu currently offers New, Open, Save, Save As, and Reboot. The Edit
menu offers Undo, Cut Line, Copy Line, and Paste; Cut Line and Copy Line shorten
to Cut and Copy when text is selected. The Select menu offers Find, Select All,
and Select None. Clipboard contents live only inside the running Mothpad
session. If text is selected, Copy and Cut use the selection; otherwise they
fall back to the current line. Paste, selection deletion, and cut-line deletion
undo as one grouped edit. Find uses a centered prompt, selects the found match,
and repeats the last query on later Ctrl+F presses. Dirty New, Open, and
Reboot actions open an `Unsaved changes`
popup with Cancel, Quit, and Save+Quit choices. On a clean buffer Reboot uses
the Pico SDK watchdog reboot path. The Open screen sorts `..` first, directories
before files, and names in case-insensitive ASCII order. It includes a
right-side peek pane only when the selected file has text content to preview.

The top-right status uses private LCD glyph codes for a two-cell, 25%-step
battery icon. The bottom-right status corner uses a two-cell moth logo. Boolean
menu toggles use private checkbox glyphs from the same atlas. Those glyphs are
defined in `mothpad_picocalc_platform.h` and
rendered only by the PicoCalc platform shim; they are not file text and are not
part of the core editor model. Normal text glyphs render as padded 5x7 shapes
inside an 8x12 cell; private UI glyphs render stretched across the full 8x12
cell so menu borders, the moth logo, battery halves, and checkbox marks can use
the whole cell.
The editable source atlas for those private UI glyphs is
`../../docs/glyph-previews/mothpad-ui-glyph-atlas-fullcell-raw.bmp`.
The battery state is polled four times per second so the charging glyph can
appear or disappear shortly after USB power is connected or removed, subject to
whatever delay the keyboard controller applies to its charging bit.

## Hardware Notes

- LCD SPI1 pins: SCK 10, MOSI 11, MISO 12, CS 13, DC 14, RST 15.
- Keyboard I2C1 pins: SDA 6, SCL 7, address `0x1f`, key register `0x09`,
  battery register `0x0b`.
- SD SPI0 pins: SCK 18, MOSI 19, MISO 16, CS 17, detect 22.
- The clean LCD driver uses 18-bit RGB888 writes. If colors look inverted,
  cyan-fringed, or single-pixel-corrupted, check that `0x3A = 0x66` is sent
  after the final `0x36`/MADCTL write.

## What Still Needs The PicoCalc Shell

- app lifecycle: launch and return result contract
- public-release notice bundle
