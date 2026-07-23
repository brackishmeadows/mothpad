# Mothpad Pico Build

This folder is a Pico SDK build target for Mothpad's C core. It is not yet the
full PicoCalc app shell, but it now boots into a blank editable document, draws
to the PicoCalc LCD, accepts keyboard input, and can open/save files on the SD
card.

For the full verified build handoff, read `../../docs/build-handoff.md`.
For the Duolith hardware proof ledger, read
`../../../duolith/docs/proof-report.md`.

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

For Pico 2 W / RP2350 Wi-Fi style targets:

```powershell
cmake -S picocalc\mothpad\c\pico -B picocalc\mothpad\c\pico\build-pico2w -DPICO_BOARD=pico2_w
```

Then build:

```powershell
cmake --build picocalc\mothpad\c\pico\build
```

Expected output includes `mothpad_pico.uf2`, `mothpad_pico_legacy.uf2`, and
diagnostic UF2s. Experimental/reference builds may also produce
`foldarium.uf2`, `mothnote-experimental.uf2`, and the older
`mothpad-experimental.uf2` two-core service sketch.

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

The same build directory may also contain the app-split/reference experiment:

```text
picocalc\mothpad\c\pico\build-pico2_w_mothpad\foldarium.uf2
picocalc\mothpad\c\pico\build-pico2_w_mothpad\mothnote-experimental.uf2
```

## Current Behavior

The clean target is `mothpad_pico`. It initializes the Mothpad core, renders
`MothCell` cells to the PicoCalc LCD through `mothpad_picocalc_platform.c`, and
polls the PicoCalc I2C keyboard through the same platform shim. The PicoCalc LCD
path currently uses a `40x26` grid from 8x12 cells, and keypress redraws are
diffed at the cell level instead of clearing the whole screen. It mounts the SD
card at `/` through `pico-vfs` and does not format the card on mount failure.
The top status row also shows the keyboard controller's battery percent when
available. The normal editor footer is removed, and the title row uses a dark
background plus smeared bold title text to reduce visible LCD persistence from
the old static white bands.
After 60 seconds without input, Mothpad starts a full-screen LCD scrub
screensaver with larger moving black/white fields. The pattern changes between
vertical, horizontal, and diagonal sweeps over time without whole-screen
strobing. Any key wakes the editor and is not passed through as input.
On boot, the clean target shows the embedded `mothpad-splash.png` artwork for
about half a second before clearing into the editor or recovery prompt.

The `mothpad-experimental.uf2` target is a two-core app/service sketch. Core 0
still owns the editor, LCD, keyboard, and normal Mothpad UI. Core 1 launches a
copied-out file-browser service (`mothpad_file_browser_app.c`) that performs a
read-only root directory scan and then keeps a heartbeat/status alive. This is a
prototype for external app surfaces; it is not the known-good hardware path and
does not yet let both cores browse or mutate the filesystem concurrently.

`foldarium.uf2` is now a benched standalone file browser reference target. It
mounts the SD card, browses directories, and writes the selected file path to
`/.foldarium-open`. `mothnote-experimental.uf2` is the editor-side companion:
on boot it checks for that handoff file, opens the queued path if possible, and
then deletes the handoff. Recovery prompt handling still wins over Foldarium, so
an autosave recovery file is not silently skipped.

Mothpad's embedded files surface provides Open, New Folder, Copy, Cut, Paste,
Rename, Delete, and Cancel from its F1 file-action menu. `N/O/C/X/V/R/D` are
menu-local accelerators. Ctrl+C copies the selected file item, Ctrl+X cuts the
selected file or directory for move, and Ctrl+V pastes into the current folder.
Copying directories and deleting non-empty directories are intentionally blocked
in this first pass.

This is deliberately a file handoff contract, not simultaneous app execution,
and it is no longer the active product direction. Mothpad's embedded file
surface is the canonical files app for now. Standalone Foldarium remains useful
as reference code for file operations, handoff, and app-split experiments.

This older launch flow is: boot Foldarium, select a file, boot Mothnote
Experimental. It is a step toward separate UF2 apps that communicate through
small SD-card contracts, not the current product path.

The local Duolith UF2 Bootloader checkout at `picocalc/duolith-bootloader` is
currently benched after hardware testing; use Pelrun's upstream `BOOT2350.uf2`
for normal testing. The intended launch-file architecture note is
`../../../duolith/docs/architecture.md`.

`portmanteau-demo.uf2` is the first resident runtime experiment. It is a normal
app launched by the loader, not part of the GPL loader fork. It keeps Mothnote
resident, switches fullscreen into a Foldarium-like browser without rebooting,
uses a small Duolith mailbox for slot state, and returns to Mothnote with a
selected file handoff.

`mothpad-duolith.uf2` is the named Mothpad-suite lab build. It still links the
payloads into one UF2, but Mothpad, Foldarium, and the calculator now register
through a small Duolith app context/suite module instead of being only ad hoc
local modes. F5 foregrounds the calculator through the Duolith path, Ctrl+O
foregrounds the Foldarium-style picker through the same path, and Esc/F5 returns
to the Mothpad editor payload. Core 1 now runs a Duolith payload loop that
tracks the active app id and publishes per-app payload heartbeats. The
Calculator now owns the foreground more directly: while it is active, core 0
parks the Mothpad UI loop and core 1 polls the keyboard, updates calculator
state, and draws the calculator screen itself. Esc/F5 in the core-1 calculator
releases the foreground back to Mothpad. This is cooperative LCD/keyboard bus
ownership, not a protected arbiter. Foldarium still uses core 0 for its real
file-browser logic, with core 1 only tracking its payload identity/heartbeat.
This is not yet the final independent SD-loaded payload bundle, and Calculator
is still linked into the same UF2 rather than loaded as a separate binary.

`duolith-ram-slot-proof.uf2` is the first isolated Duolith hardware proof. Core
0 owns the LCD/keyboard foreground and keeps a visible heartbeat. Core 1 runs a
tiny RAM-resident payload A or B. Pressing any key resets core 1 and relaunches
it at the other RAM-resident payload while core 0 keeps running. This proves
core-1 relaunch with a surviving foreground core; it does not yet load an
external app image from SD into RAM.

`duolith-ram-image-proof.uf2` is the next Duolith hardware proof. Core 0 owns
the LCD/keyboard foreground, copies an embedded assembler payload byte range
into a reserved RAM slot, and launches core 1 at the copied RAM address. Each
keypress resets core 1, overwrites the same RAM slot with the other payload
image, and relaunches it. This proves a replaceable RAM image slot using
embedded blobs; it still does not load payloads from SD.

`duolith-slot0-replace-proof.uf2` tests the opposite direction. Core 1 owns the
visible monitor and keyboard after launch. Core 0 runs from a copied RAM image
in a slot-0 RAM buffer. Each keypress on core 1 requests the other slot-0 image;
the running slot-0 payload cooperatively jumps through a loader trampoline,
which overwrites the slot-0 RAM buffer and jumps into the replacement image.
This proves cooperative slot-0 self-replacement while slot 1 remains alive; it
does not prove hardware reset of core 0.

The Duolith proof UF2s are intentionally asymmetric harnesses so the behavior is
visible and debuggable. They are not the final ownership model. The target model
is coequal slots: any compatible payload may run in either slot, and whichever
slot remains alive should be able to act as the temporary host/root long enough
to load, replace, or recover the other slot.

`duolith-coequal-proof.uf2` is the first proof aimed at that coequal target. It
uses one slot-neutral assembler payload ABI that reads its current core/slot id
and indexes shared slot state. The proof starts with slot 0 as the visible host
replacing slot 1. After the third keypress, the monitor is handed to slot 1 and
slot 0 becomes a replaceable RAM payload; further keypresses let slot 1 host and
replace slot 0. This proves both slots can run the same payload ABI and either
alive slot can act as the temporary host in the test harness. It still uses
embedded payload blobs, not SD-loaded app binaries.

`duolith-sd-payload-proof.uf2` is the first SD-loaded payload proof. On first
boot it mounts the SD card and creates `DUO_A.BIN` and `DUO_B.BIN` in the SD
root if they are missing, using tiny embedded reference payloads. It then reads
one of those files back from SD into a reserved RAM slot and launches core 1 at
that loaded RAM image. Each keypress resets core 1, reads the other payload file
from SD, overwrites the RAM slot, and relaunches core 1. This proves the
SD-file -> RAM-slot -> execute path. It is still a core0-host/core1-payload
harness, not the coequal bundle loader.

`duolith-sd-slot0-proof.uf2` tests the same SD-file -> RAM-slot -> execute path
for core 0. Core 1 owns the monitor/keyboard. Core 0 runs a RAM payload loaded
from `DUO0_A.BIN` or `DUO0_B.BIN` at the SD root. Each keypress asks core 0 to
swap; the running core-0 payload cooperatively jumps through a trampoline, which
reads the requested file from SD, overwrites the core-0 RAM slot, and jumps into
the replacement image. This proves cooperative SD-loaded core-0 replacement
while core 1 remains alive. It does not prove forced hardware reset/relaunch of
core 0.

`duolith-sd-coequal-ui-proof.uf2` is the first proof of the intended foreground
handoff shape. It creates `DUOC_A.BIN` and `DUOC_B.BIN` if missing, loads one
payload from SD into each slot, and starts both slots running. The same payload
image shape can run in either slot: it reads its current core id at runtime and
uses shared slot tables rather than hard-coding slot 0 or slot 1. Only the
foreground payload draws and reads the keyboard. Pressing any key requests the
peer slot be replaced from SD, then transfers foreground/display/keyboard
ownership to that peer. Repeated keypresses replace both slots in alternation
while the foreground moves between them. This is still a harness: the SD payloads
are tiny ABI stubs that call resident service functions, not full independent
apps.

`duolith-app-catalog-proof.uf2` is the first app-catalog-shaped Duolith proof.
It creates Idle, Menu, Mothpad, Calculator, and Foldarium payload files on SD
from separate source folders under `duolith/apps/`, starts Mothpad in slot 0
with slot 1 empty, and opens the Menu payload into slot 1 when F5 needs a peer
app. The menu can replace its own slot with one of the named app payloads. Q
closes either foreground app into the Idle payload; dirty simulated apps require
Y/N confirmation first. This still uses tiny ABI stubs plus resident C services;
it is not yet full product app binaries, and it avoids truly dead cores by
keeping closed slots alive as Idle.

`portmanteau-pma-proof.uf2` is the first proof of the Portmanteau app-binary
container. It seeds `.PMA` files on SD, validates a small header
(magic/version/app id/entry offset/image size), copies the image portion into a
RAM slot, and lets the payload call back through a tiny ABI table. It boots
Mothpad as a PMA image, can open Menu into the peer slot with `M`, and Menu can
replace its slot with Mothpad, Calculator, or Foldarium. The Menu PMA now owns
its own menu UI and requests self-replacement through the ABI. Calculator is
now built as a separate ARM C payload at
`b-duolith/pma-calculator/calculator.pma`; copy it to `/apps/calculator.pma` on
the SD card before selecting Calculator. It is linked for fixed RAM at
`0x20080000`, clears its own `.bss`, receives the ABI pointer in `r0`, draws its
own UI, and evaluates small integer expressions. Mothpad and Foldarium remain
toy payloads assembled with the host UF2.

`duolith-riscv-core1-proof.uf2` is the first RISC-V slot proof and has passed on
hardware. It is still an ARM UF2 running the PicoCalc monitor on core 0, but it
sets core 1's `ARCHSEL` bit to RISC-V, copies a tiny hand-encoded RV32I payload
into RAM, launches core 1 at that RISC-V RAM image, and watches the payload
update shared heartbeat state. Each keypress resets core 1 and relaunches the
other RISC-V payload variant. This proof does not use a local RISC-V compiler;
the payload words are encoded by the ARM monitor at runtime. It proves mixed
ARM/RISC-V launch and RISC-V shared-memory heartbeat. It does not yet prove two
RISC-V slots or SD-loaded RISC-V app binaries.

Build outputs:

```text
c/pico/build-pico2_w/mothpad_pico.uf2
c/pico/build-pico2_w/mothpad_pico_legacy.uf2
c/pico/build-pico2_w/foldarium.uf2
c/pico/build-pico2_w/mothnote-experimental.uf2
c/pico/build-pico2_w/portmanteau-demo.uf2
c/pico/build-pico2_w/mothpad-duolith.uf2
c/pico/build-pico2_w/duolith-ram-slot-proof.uf2
c/pico/build-pico2_w/duolith-ram-image-proof.uf2
c/pico/build-pico2_w/duolith-slot0-replace-proof.uf2
c/pico/build-pico2_w/duolith-coequal-proof.uf2
c/pico/build-pico2_w/duolith-sd-payload-proof.uf2
c/pico/build-pico2_w/duolith-sd-slot0-proof.uf2
c/pico/build-pico2_w/duolith-sd-coequal-ui-proof.uf2
c/pico/build-pico2_w/duolith-app-catalog-proof.uf2
c/pico/build-pico2_w/portmanteau-pma-proof.uf2
c/pico/build-pico2_w/duolith-riscv-core1-proof.uf2
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
- Tab, inserted as a literal tab character and displayed at two columns by default
- Backspace
- Delete
- arrow keys
- Home and End
- F1 File menu
- F2 Edit menu, or F1 then Right
- F3 Select menu, or F1 then Right twice
- F4 View menu
- F5 fullscreen calculator
- Ctrl+N new
- Ctrl+S save
- Ctrl+O open
- Ctrl+Q reboot
- Ctrl+F find, then find next
- Ctrl+A select all
- Ctrl+D select none
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
and Select None. F4 opens the View menu, which offers Wrap plus Edit Mode and
Read Mode radio items. F5 opens a fullscreen calculator with boxed input/result
areas, live evaluation while typing, and per-session expression history.
Enter or `=` commits an evaluation to history, Left/Right move within the input,
Up/Down recall history, `C` clears the current input without clearing history,
and Esc or F5 returns to the editor. Whole-number results render as integers;
unclear or invalid results render as `?`. It supports basic `+`, `-`, `*`, `/`,
`x`, decimal, and parenthesized expressions. In Read Mode there is no cursor, Left/Right
scroll one line, Up/Down scroll one page, and returning to Edit Mode places the
cursor at the top of the visible text. Clipboard contents live only inside the running Mothpad
session. If text is selected, Copy and Cut use the selection; otherwise they
fall back to the current line. Paste, selection deletion, and cut-line deletion
undo as one grouped edit. Find uses a centered prompt, selects the found match,
and repeats the last query on later Ctrl+F presses. Dirty New, Open, and
Reboot actions open an `Unsaved changes`
popup with Cancel, Discard, and action-specific Save choices. On a clean buffer
Reboot uses the Pico SDK watchdog reboot path. The Open screen sorts `..` first, directories
before files, and names in case-insensitive ASCII order. It includes a
right-side peek pane only when the selected file has text content to preview.

Persistent settings are stored as plain text at `/.mothpad-settings.txt`.
Currently persisted keys are `settings_version`, `keep_backups`, `soft_wrap`,
`tab_insert_spaces`, and `tab_width`; default tabs are literal tab characters with width 2. Recovery autosave writes `/.mothpad-recovery.txt` plus
`/.mothpad-recovery.meta`, which records the original file path. Opening,
ignoring, or saving clears stale recovery files.

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
