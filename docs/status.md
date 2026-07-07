# Mothpad Status

Status: active spike

## What This Is

A new PicoCalc text editor project. The current artifact is a C-first
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

- Picoware/MicroPython app: possible future port target, not supported by the
  current build.
- PicoMite BASIC editor: possible, but less pleasant for reusable core logic.
- C/C++ firmware app: strongest control, highest setup cost.

## Pico Build State

`c/pico/` contains Pico SDK CMake targets. The clean target is `mothpad_pico`,
which renders cells to the PicoCalc LCD through `mothpad_picocalc_platform.c`,
polls the PicoCalc keyboard through that same shim, and mounts the SD card
through `pico-vfs`.

This is an SD-backed PicoCalc editor shell with menus, find, undo, selection,
clipboard, battery display, and safe-save behavior.

Build result on 2026-07-06: Pico SDK and MSYS2 ARM toolchain installed. The
`pico2_w` target builds successfully. The default wrapper output is:

```text
c/pico/build-pico2_w/mothpad_pico_legacy.uf2
c/pico/build-pico2_w/mothpad_pico.uf2
```

Current hardware tuning builds have used:

```text
c/pico/build-pico2_w_mothpad/mothpad_pico_legacy.uf2
c/pico/build-pico2_w_mothpad/mothpad_pico.uf2
```

Host C tests also pass via `c/build.ps1`.
Boot now flashes the embedded `mothpad-splash.png` artwork for about half a
second before the editor or recovery prompt appears.

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

Current file behavior: boots into a blank document, uses Ctrl+N for new, Ctrl+S
for save/save-as, Ctrl+O for a current-directory file list, Ctrl+Q for reboot,
uses Ctrl+Z for bounded
single-character undo, and keeps editing in memory if SD mount fails. F1 opens a
small File menu for New, Open, Save, Save As, and Reboot. F2, or F1 then Right,
opens an Edit menu for Undo, Cut Line, Copy Line, and Paste. F3, or F1 then
Right twice, opens a Select menu for Find, Select All, and Select None; Ctrl+A
selects all and Ctrl+D clears selection. F4 opens View. F5 opens a fullscreen
calculator placeholder; Esc or F5 returns to the editor. The
first clipboard is internal to the running app session. Shift+Arrow selection is
wired through the clean keyboard shim; the clean target also explicitly enables
keyboard modifier reporting with the documented default keyboard config and
treats shifted Up/Down's PageUp/PageDown events as selection movement. Because
some keyboard firmware does not report Shift modifier FIFO events reliably, the
clean shim samples the C64 matrix for physical Shift and uses the joystick
register to synthesize Shift+Left/Right selection while Shift is down. Selected
text is highlighted and Copy/Cut use the selection when present, falling back to
the current line otherwise; menu labels shorten to Copy/Cut while a selection
exists. Paste, selection deletion, and cut-line deletion undo as grouped edits.
Find uses a centered prompt, selects the found match, and repeats the previous
query on Ctrl+F. Dirty New, Open, and Reboot actions open an Unsaved Changes
popup with Cancel, Discard, and action-specific Save choices. Dirty editing writes a single
global recovery file, `/.mothpad-recovery.txt`, after 8 seconds of quiet input.
On boot, Mothpad asks whether to open that recovery file. Opening it loads an
unnamed dirty draft with the original path restored from
`/.mothpad-recovery.meta` when available, then clears the stale recovery files;
manual save remains explicit. Ignoring recovery or saving also clears recovery
text and metadata.
File has a checkbox-glyph Keep Backups boolean item controlling `.bak` creation.
View has checkbox-glyph Wrap plus radio-glyph Edit Mode and Read Mode entries.
Read Mode hides the cursor:
Left/Right scroll one line, Up/Down scroll one page, and returning to Edit Mode
places the cursor at the top of the visible text. Wrap and Keep Backups persist through `/.mothpad-settings.txt`.
Tabs currently default to literal tab characters displayed at two columns; the same settings file supports
`settings_version`, `tab_insert_spaces`, and `tab_width` until a settings dialog exists.
Read Mode blocks mutating edit actions while preserving viewing and copy.
The Open screen shows a right-side peek pane only when the selected file has
text content to preview, with `..` first, directories before files, and
case-insensitive ASCII name sorting. The top status row shows a private
two-cell, 25%-step battery glyph and percent when the keyboard controller
reports it; the bottom-right status corner shows a two-cell moth logo. Private
UI glyphs now render full-cell while text glyphs keep their padded 5x7
placement, so menu box pieces and logo halves can connect. The SD stack uses
`pico-vfs` with FatFs over SPI0 pins 18/19/16/17 plus detect pin 22.

Hardware posture as of 2026-07-06: `mothpad_pico.uf2` is a clean-build PicoCalc
hardware success through Pelrun's UF2 Loader. It edits live, uses SD-backed
open/save, and renders without the previous cyan shimmer after reasserting LCD
pixel format `0x3A = 0x66` after the final display mode/MADCTL writes.
Shift+Left/Right selection is hardware-tuned through joystick-register
synthesis; see `docs/shift-left-right-report.md`.

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
4. Run hardware acceptance on the menu, selection, find, and clipboard surface.
5. Expand the File menu only after the current shallow version is proven on
   hardware.

Field report note: the first real bus/meetup writing test is preserved in
`docs/field-report-2026-06-07-southbank.md`. Its highest-value follow-ups are
Backspace off-by-one investigation, accidental selection while holding
Left/Right, Open Recent, create-directory, soft wrap, and a write-lock or
scroll-only carry mode.

## Source References

- `docs/mothpad-spec.md`
- `docs/refterm-reading.md`
- `docs/build-handoff.md`
- `docs/shift-left-right-report.md`
- `docs/field-report-2026-06-07-southbank.md`
- `docs/picocalc-software-ideas.md`
