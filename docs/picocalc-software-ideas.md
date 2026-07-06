# PicoCalc Software Ideas

Status: active idea note.

This note collects software shapes that seem native to PicoCalc after building
Mothpad: keyboard-first, small-screen, plain-state programs that do not pretend
the device is a desktop workstation.

## Constraint Read

PicoCalc can probably support fairly complicated software if the complexity is
inside the data model and state machine, not inside mouse-like spatial UI.

Good constraints:

- physical keyboard
- small square screen
- built-in speaker
- SD card storage
- Pico-class CPU
- no mouse
- no GPU
- redraw and input loops must stay honest

The machine likes workflows built from:

- lines
- rows
- fields
- cells
- tiles
- commands
- menus
- text selection
- small lists

It dislikes workflows that depend on:

- drag/drop precision
- hover
- freeform canvas navigation
- smooth zoom and pan
- dense multi-pane layouts
- GPU preview as the main reward

## Programs That Fit Well

- text editors
- file managers
- config editors
- logbooks
- outliners
- todo systems
- BASIC/script editors
- small database browsers
- roguelikes
- tile/map editors
- packet/card/table editors
- calculators
- recipe/notebook/reference tools
- terminal-like apps
- turn-based games
- interactive fiction tools
- little compilers/transpilers
- data-entry apps for small structured records
- tracker-like music tools

The common shape: move a cursor, edit an item, save safely, leave cleanly.

## Programs That Fit Badly

Likely bad fits:

- desktop-style node graphs
- vector drawing
- DAW timelines
- shader graph editors
- freeform mind maps
- spline/animation tools
- dense spreadsheets
- large visual IDEs
- complex multi-pane dashboards

These are not impossible, but they fight the hardware. They usually want a
mouse, a big canvas, or a GPU preview.

## Node Editor Thought

A normal node editor is probably a poor fit.

The bad version:

- drag boxes around a canvas
- connect ports with pointer gestures
- pan and zoom constantly
- require visual layout management before the program does anything useful

A PicoCalc-native graph tool might still work if it is secretly an outline,
table, or command editor with graph semantics.

Example shape:

```text
[01] noise      -> 02.value
[02] threshold  -> 03.mask
[03] tile-fill  -> output
```

Focused node edit shape:

```text
NODE 02 threshold
input:  01.value
amount: 0.42
out:    03.mask
```

Possible graph uses that fit better than shader graphs:

- procedural tile/map generation
- dialogue trees
- quest/state machines
- room logic for text adventures
- dice table combinators
- rule systems for tiny games
- simple image/tile filters for PicoCalc-scale graphics
- file/log automation rules

Guiding rule: cockpit, not canvas. Named things you can tab through, not boxes
you have to chase.

## Live Music Tool

This seems like one of the strongest non-Mothpad directions.

The PicoCalc kit has built-in speakers, and a live-coded / tracker-like music
tool matches the hardware better than a visual graph editor:

- keyboard-first
- grid/text based
- small screen is acceptable
- immediate feedback matters
- constraints become taste
- patterns save naturally as plain text
- the device becomes self-contained

Do not build Ableton-in-a-lunchbox. Build a small ritual rhythm machine.

### Drum Circle Sketch

Plain-text pattern idea:

```text
tempo 120
kit pico

kick  x... x... x..x ....
snare .... x... .... x...
hat   x.x. x.x. x.x. x.x.
blip  ..3. .... ..5. ....
```

Denser tracker-ish shape:

```text
BPM 132        BAR 03/08
01 K x...x...x..x....
02 S ....x.......x...
03 H x.x.x.x.x.x.x.x.
04 M ..c...e...g...b.
```

Version 0.1 could be:

- play/stop
- tempo
- 4 tracks
- 16 steps
- edit while playing
- save/load plain text pattern files
- current step highlight
- mute track
- rotate row
- randomize row

Sound generation should start brutally simple:

- kick: pitch-drop square/sine-ish voice with envelope
- snare: noise burst plus short tone
- hat: very short noise burst
- blip: square wave note with fast decay

Even primitive sound can have character. PicoCalc does not need hi-fi; it needs
a tiny, reliable, strange band in a box.

Possible names:

- Mothbeat
- Tin Circle
- Pico Drum Circle
- Clickmoth
- Pocket Rattle
- Bramblebeat
- Mothbox

## Implementation Lessons To Reuse From Mothpad

- Use a cell buffer between app state and display.
- Reserve top/bottom status rows.
- Keep menus shallow.
- Save plain files safely.
- Keep hardware platform code separate from app core.
- Make diagnostics early when hardware behavior gets weird.
- Tune input on hardware, not from vibes.

## Next Honest Prototype

If we pursue the music tool:

1. Make a tiny C PicoCalc app that plays one square-wave click on a timer.
2. Add noise burst and simple envelope.
3. Add a 16-step, 4-track pattern grid.
4. Highlight current step while playing.
5. Allow live edits without stopping playback.
6. Save/load pattern text from SD only after the sound loop feels real.

Do audio first. The UI can borrow Mothpad later. A silent music editor is a
painted door.

## Screen Mirror / Demo Bridge

This would be a browser app that mirrors the PicoCalc screen while the device is
plugged in over USB. The goal is live demos: run real software on the PicoCalc,
show a clean enlarged version in a browser for talks, videos, or debugging.

This is a good fit because it does not ask the PicoCalc to become a desktop. It
lets the PicoCalc remain the real machine while a host browser acts as a
projector.

### Likely Architecture

Use USB CDC serial from the Pico app to the browser through the Web Serial API.

Pico side:

- app renders normally to the LCD;
- the display/platform layer also emits mirror packets over USB;
- packets describe either cells, pixels, dirty rectangles, or LCD operations;
- mirror output must be optional so normal hardware behavior does not depend on
  a connected browser.

Browser side:

- user clicks Connect;
- browser opens the PicoCalc USB serial port;
- JavaScript parses packets;
- a canvas draws the mirrored screen;
- UI can scale the canvas cleanly for presentation;
- optional capture/recording can come later.

### Protocol Options

Cell-grid mirror:

- best for Mothpad-style apps;
- send rows/columns/glyph/fg/bg/flags;
- very low bandwidth;
- browser must share the font/glyph mapping;
- not exact for arbitrary graphics.

LCD operation mirror:

- wrap platform drawing calls and stream operations like set-window plus pixel
  data;
- closer to the actual LCD path;
- works for more graphical apps if they use the shared display layer;
- more protocol complexity.

Framebuffer mirror:

- send raw 320x240 RGB565 or RGB888 frames;
- simplest conceptually;
- expensive: 320x240 RGB565 is about 150KB per full frame;
- probably acceptable at low frame rates over USB, but wasteful for text apps.

Dirty-rectangle / RLE mirror:

- good middle ground for graphical apps;
- send only changed rectangles;
- simple RLE could shrink large flat UI regions;
- more moving parts, but still tractable.

### Best First Prototype

Start with a Mothpad-only cell mirror:

1. Add an optional USB mirror mode to the Pico platform/app.
2. On each Mothpad render, send a frame header plus the cell grid.
3. Make a local browser page using Web Serial.
4. Draw the cells to a scaled canvas using the same 5x7 font.
5. Add a "fit to window" presentation mode.

If that works, generalize into a shared PicoCalc display mirror layer.

### Things To Avoid

- Do not require a server for v0.1; a static browser page plus Web Serial is
  enough.
- Do not start with full raw video unless the cell mirror proves too narrow.
- Do not make the browser the source of truth. The PicoCalc app must keep
  working with no host attached.
- Do not block the render loop on USB writes. Dropped mirror frames are better
  than sluggish software on the real device.

### Why It Matters

This would make hardware demos much less cursed. Instead of pointing a webcam at
a reflective tiny screen, the presenter can show the actual PicoCalc output in a
browser while still using the real keyboard and device.
