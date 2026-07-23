<p>
  <img src="mothpad-logo.bmp" alt="Mothpad logo wordmark" width="420" class="center-image">
</p>

Mothpad is a notepad-shaped text editor for PicoCalc: simple, plain-text-first,
safe-saving, and built to stand on its own. Not an editor lifestyle cult.

Mothpad is written by Harper Weathervane, an LLM agent configured through OpenAI Codex
and running on GPT-5, and made in collaboration with Morgan Brackish Meadows.

This project is AI-generated / AI-assisted software. The code, docs, tests, and
font work in this repository were produced through an interactive human + AI
development process and should be reviewed accordingly.

##

<p>
  <img src="0ec087bf-c103-438e-86ce-975278652e4e.png" alt="Mothpad splash artwork: moth under a lamp" width="360" class="center-image">

  <img src="IMG_2133.jpg" alt="Mothpad running on a PicoCalc" width="420" class="center-image">
</p>

## Current Shape

Status: active spike

License status: no open-source license has been selected yet. See
`LICENSE.md` and `THIRD_PARTY.md`.

The implementation starts in C so the architecture matches PicoCalc-scale
constraints: flat buffers, explicit cells, boring file I/O, and a thin device
shell. The canonical product shape is in `docs/mothpad-spec.md`.

The known-good hardware route is Pelrun's UF2 Loader on a Pico 2 or Pico 2W:
copy the clean `mothpad_pico.uf2` into the SD card's `pico2-apps` folder and
launch it from Pelrun's menu. This has been hardware-verified on PicoCalc.
Mothpad does not need the older ClockworkPi multibooter `.bin` path for that
route, and the editor core remains independent of any one loader.

## Goals

- Open, edit, and save small plain-text files from the SD card.
- Keep the editor usable with physical keys and no mouse.
- Make cursor movement, insertion, deletion, scrolling, and safe saving reliable
  before adding menus or visual sugar.
- Show basic PicoCalc shell state such as battery percent and simple file
  commands without smearing that state into the editor core.
- Keep the core runtime-agnostic so the display and keyboard layer can change.

## Non-Goals

- Rich text, syntax highlighting, Unicode cleverness, markdown preview, or
  project-wide search in v0.1.
- Editing huge files. This is for notes and small scripts first.
- Locking into one PicoCalc firmware until the device API is confirmed.
- Acting as a callable editor service for separate Pelrun UF2 applications.

## Layout

- `c/src/mothpad.h` - C API, limits, state, cells, and editor operations.
- `c/src/mothpad.c` - C editor core, safe save, find, and cell rendering.
- `c/tests/mothpad_tests.c` - C behavior tests.
- `c/pico/` - Pico SDK UF2 targets. The clean `mothpad_pico.uf2` build is
  hardware-verified on PicoCalc via Pelrun's UF2 Loader; the legacy UF2 remains
  a fallback only.
- `LICENSE.md` - current Mothpad project license status.
- `THIRD_PARTY.md` - current third-party and licensing notes.
- `docs/mothpad-spec.md` - canonical product and architecture spec.
- `docs/build-handoff.md` - exact host/Pico build process for future agents.
- `docs/status.md` - continuity note and next honest moves.

## Run C Tests

```powershell
powershell -ExecutionPolicy Bypass -File .\picocalc\mothpad\c\build.ps1
```

## Build Pico Target

Install the Raspberry Pi Pico SDK, set `PICO_SDK_PATH`, then see
`c/pico/README.md`. The clean target is `mothpad_pico.uf2`; use
`mothpad_pico_legacy.uf2` only as a fallback while the clean driver keeps
working.
