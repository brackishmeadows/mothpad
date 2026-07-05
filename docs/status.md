# Mothpad Status

Status: active spike

## What This Is

A new PicoCalc/Picoware text editor project. The current artifact is a C-first
desktop-testable editing core plus a Pico SDK UF2 shell that boots on PicoCalc,
draws through a clean LCD/keyboard shim, and opens/saves files on the SD card.

## Why

The risky part is not only drawing characters. The little monster under the
floor is state: cursor motion, line splitting, deletion at boundaries,
scrolling, dirty tracking, safe save behavior, and clean return-to-caller
semantics. Build those where they can be tested.

## Current Decision

Treat `docs/mothpad-spec.md` as canonical. Keep the editor core runtime-agnostic,
use `mothpad_pico.uf2` as the clean hardware path, and keep the legacy UF2 as a
fallback only. The Python spike is reference only.

Architecture spine:

```text
TEXT BUFFER
-> LINE TABLE
-> VIEWPORT
-> CELL BUFFER
-> DISPLAY
```

## Candidate Runtime Shells

- Picoware/MicroPython app: likely best if the existing SD-card setup is the
  user's active environment.
- PicoMite BASIC editor: possible, but less pleasant for reusable core logic.
- C/C++ firmware app: strongest control, highest setup cost.

## Pico Build State

`c/pico/` contains Pico SDK CMake targets. The clean target is `mothpad_pico`,
which renders cells to the PicoCalc LCD through `mothpad_picocalc_platform.c`,
polls the PicoCalc keyboard through that same shim, and mounts the SD card
through `pico-vfs`.

This is an SD-backed edit build, not the full PicoCalc editor layer.

Build result on 2026-07-05: Pico SDK and MSYS2 ARM toolchain installed. The
`pico2_w` target builds successfully and emits:

```text
c/pico/build-pico2_w/mothpad_pico_legacy.uf2
c/pico/build-pico2_w/mothpad_pico.uf2
```

Host C tests also pass via `c/build.ps1`.

The current PicoCalc grid is `40x26`, based on 8x12 cells on the 320x320 LCD.
Rows 0 and 25 are status bars; rows 1 through 24 are document viewport rows.

Pelrun UF2 Loader note: this path can be tested by copying `mothpad_pico.uf2`
into the SD card's `pico2-apps` folder and launching it from the loader menu.
The official ClockworkPi multibooter `.bin` offset path is a different boot
route and is not needed for the Pelrun UF2-loader test.

Confirmed on 2026-07-04: launched from Pelrun's loader on the user's PicoCalc
and drew the Mothpad cell grid on the LCD. The next build added basic keyboard
editing: printable characters, Enter, Tab, Backspace, Delete, arrows, Home, and
End.

Current file behavior: boots into a blank document, uses Ctrl+S for save/save-as,
uses Ctrl+O for a simple current-directory file list, and keeps editing in memory
if SD mount fails. The SD stack uses `pico-vfs` with FatFs over SPI0 pins
18/19/16/17 plus detect pin 22.

Hardware posture as of 2026-07-05: `mothpad_pico.uf2` is a clean-build PicoCalc
hardware success through Pelrun's UF2 Loader. It edits live, uses SD-backed
open/save, and renders without the previous cyan shimmer after reasserting LCD
pixel format `0x3A = 0x66` after the final display mode/MADCTL writes.

Licensing posture as of 2026-07-05: the clean target no longer compiles
ClockworkPi hello-world LCD/keyboard source. It still depends on Pico SDK,
`pico-vfs`, and FatFs. `mothpad_pico_legacy.uf2` still exists and compiles
ClockworkPi hello-world LCD/keyboard sources through a small adapter; treat it
as fallback, not the public-release posture. See `../THIRD_PARTY.md`.

## What Not To Do Next

- Do not mutate the factory SD-card dump while experimenting.
- Do not write a full UI before confirming keyboard/display APIs.
- Do not bury the core logic inside hardware-specific event code.
- Do not let the Python seed become accidental architecture.
- Do not add `stb` to the core just because it is handy. Use it later only if a
  specific platform/display/font task earns it.

## Next Honest Moves

1. Run the C host tests.
2. Harden safe-save behavior for the target filesystem.
3. Run the hardware acceptance pass: save-as, reopen, edit, verify `.bak`.
4. Add dirty quit handling.
5. Add new/quit command UI.

## Source References

- `docs/mothpad-spec.md`
- `docs/refterm-reading.md`
- `docs/build-handoff.md`
