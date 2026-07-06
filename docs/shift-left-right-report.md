# Shift+Left/Right Selection Report

Status: resolved on hardware.

This tracks the PicoCalc-specific bug where `Shift+Left` and `Shift+Right` did
not extend selection in Mothpad, even though `Shift+Up` and `Shift+Down` worked.

## Final Behavior

- Quick `Shift+Left` / `Shift+Right` presses select one character per press.
- Holding `Shift+Left` / `Shift+Right` repeats quickly enough to feel good on
  hardware.
- `Shift+Up` / `Shift+Down` still extend selection.
- Plain Left/Right movement still works.
- Selection rendering and selection-aware Copy/Cut behavior work.

Final hardware-confirmed tuning:

```text
initial repeat delay: 180ms
held repeat:          22ms
release debounce:     90ms
keyboard I2C:         100kHz
register settle:      250us
```

## Root Cause

ClockworkPi keyboard firmware maps shifted vertical arrows to alternate keycodes:

```text
Up    -> KEY_UP, KEY_PAGE_UP
Down  -> KEY_DOWN, KEY_PAGE_DOWN
Right -> KEY_RIGHT
Left  -> KEY_LEFT
```

So `Shift+Up/Down` can work even if modifier state is awkward, because the
keyboard reports `PageUp/PageDown`. `Shift+Left/Right` do not have alternate
shifted keycodes.

Hardware testing also showed that when Shift is held, regular Left/Right FIFO
events can be suppressed. The arrows are still visible through the keyboard's
joystick register:

```text
REG_ID_C64_JS / 0x0d
right: bit 0, active-low
left:  bit 3, active-low
```

The working solution is therefore:

- sample physical Shift from the C64 matrix once per frame;
- while Shift is physically down, ignore FIFO Left/Right movement;
- synthesize horizontal selection from joystick register `0x0d`;
- use release debounce so one quick press cannot re-arm as several fresh presses;
- use faster keyboard I2C polling so repeat movement is continuous instead of
  bursty.

## Active Implementation

Files:

- `c/pico/mothpad_picocalc_platform.c`
- `c/pico/mothpad_picocalc_platform.h`
- `c/pico/mothpad_pico_main.c`

Platform layer:

- `picocalc_kbd_read_event()` reads FIFO events and tracks modifier FIFO events
  if they appear.
- `picocalc_kbd_shift_down()` reads the C64 matrix and detects Shift using
  matrix indices 2 and 3 with mask `0x80`.
- `picocalc_kbd_read_joystick()` reads joystick register `0x0d`.
- Keyboard I2C now runs at `100kHz`.
- Keyboard register reads use `250us` settle delay instead of the old `16ms`
  sleeps.

Pico shell:

- Samples physical Shift once per frame.
- Uses the current physical Shift sample for regular arrow selection.
- Ignores FIFO Left/Right while physical Shift is down.
- Synthesizes `Shift+Left/Right` selection from joystick bits.
- Uses `180ms` initial repeat delay, `22ms` held repeat, and `90ms` release
  debounce.

## Why The Timing Matters

The earlier keyboard path used `10kHz` I2C and slept `16ms` after every keyboard
register select. The main loop could read FIFO, matrix, and joystick registers
in one pass, so keyboard polling alone could impose roughly 50ms of deliberate
delay.

That produced the observed "select several characters, pause, select several
characters" wave. Raising I2C to `100kHz` and reducing register-settle delay to
`250us` fixed the pulse.

Quick taps then selected multiple characters because the joystick/Shift sample
could flicker across frames and re-arm the first-press path. The `90ms` release
debounce fixed that.

## Evidence Sources

Relevant upstream files inspected:

- `toolchains/PicoCalc-upstream/Code/picocalc_keyboard/keyboard.ino`
- `toolchains/PicoCalc-upstream/Code/picocalc_keyboard/keyboard.h`
- `toolchains/PicoCalc-upstream/Code/picocalc_keyboard/reg.h`
- `toolchains/PicoCalc-upstream/Code/picocalc_keyboard/picocalc_keyboard.ino`

Relevant community reference inspected:

- ClockworkPi forum Picoware thread, which points to BlairLeduc's
  `picocalc-text-starter` as a C/C++ PicoCalc keyboard/display starter.
- `BlairLeduc/picocalc-text-starter` keyboard and southbridge drivers.

Hardware diagnostics:

- `mothpad_diag.uf2` showed Shift changes in the C64 matrix bytes.
- Holding Shift changed displayed matrix bytes from `FF` to `7F`.
- Correct interpretation: active-low bit 7, matrix indices 2 and 3, mask
  `0x80`.

## Process Log

This section preserves the path taken, including wrong turns. The final answer
is above; this is the trail of footprints.

1. Added modifier tracking in the clean PicoCalc keyboard shim.

   File: `c/pico/mothpad_picocalc_platform.c`

   The shim watched FIFO key events for:

   ```text
   KEY_MOD_SHL 0xA2
   KEY_MOD_SHR 0xA3
   ```

   Result: did not fix `Shift+Left/Right` on hardware.

2. Explicitly enabled keyboard modifier reporting.

   File: `c/pico/mothpad_picocalc_platform.c`

   The init path wrote the documented keyboard config bits:

   ```text
   CFG_OVERFLOW_INT
   CFG_KEY_INT
   CFG_REPORT_MODS
   CFG_USE_MODS
   ```

   Result: did not fix `Shift+Left/Right` on hardware.

3. Added `PageUp/PageDown` fallback for shifted vertical arrows.

   File: `c/pico/mothpad_pico_main.c`

   The editor treats `KEY_PAGE_UP` and `KEY_PAGE_DOWN` as selection movement
   upward/downward.

   Result: `Shift+Up/Down` worked. That confirmed editor-side selection was
   functional, but did not help Left/Right because the firmware does not emit
   alternate shifted left/right codes.

4. Added C64 matrix polling for physical Shift state.

   Files:

   - `c/pico/mothpad_picocalc_platform.c`
   - `c/pico/mothpad_picocalc_platform.h`

   The shim read `REG_ID_C64_MTX` / `0x0c` and checked matrix slots 2 and 3,
   based on upstream button order:

   ```text
   button 2 -> MOD_SHL
   button 3 -> MOD_SHR
   ```

   Result: the first attempt still did not fix `Shift+Left/Right`.

5. Added diagnostic matrix output.

   File: `c/pico/mothpad_pico_diag.c`

   `mothpad_diag.uf2` showed:

   ```text
   MTX: .. .. .. .. .. .. .. .. .. SH:0/1
   ```

   Hardware observation: holding Shift changed displayed matrix bytes from
   `FF` to `7F`. This ruled out a low-nibble `0F -> 07` interpretation and
   confirmed active-low bit 7. The app shim was corrected to zero-based matrix
   indices 2 and 3 with mask `0x80`.

6. Compared BlairLeduc's starter keyboard driver.

   Reference clone: `C:\tmp\picocalc-text-starter-ref`

   Findings:

   - It reads keyboard FIFO through southbridge register `0x09`.
   - It tracks Shift from FIFO `KEY_MOD_SHL` / `KEY_MOD_SHR` press and release
     events.
   - It polls in a repeating timer and keeps modifier state persistent outside
     any one key event.
   - It does not appear to use the C64 matrix for Shift.

   Mothpad experiment based on this: refresh physical Shift state from the
   matrix around FIFO reads, so modifier state is maintained more like a
   persistent modifier model.

7. Added direct movement-time Shift query.

   Files:

   - `c/pico/mothpad_picocalc_platform.c`
   - `c/pico/mothpad_picocalc_platform.h`
   - `c/pico/mothpad_pico_main.c`

   Added `picocalc_kbd_shift_down()` and made arrow/Home/End movement combine
   the event's Shift value with a live matrix read at the moment movement was
   handled.

   Result: still did not reliably fix `Shift+Left/Right`.

8. Added frame-level Shift latch.

   File: `c/pico/mothpad_pico_main.c`

   The Pico shell remembered whether Shift had been down recently. Each
   main-loop frame updated a short latch from `picocalc_kbd_shift_down()` and
   the event's Shift flag. Arrow/Home/End movement used that latched state.

   Result: `Shift+Left/Right` began to work intermittently, especially after
   Shift release, but the latch could become sticky. False or stale Shift state
   could hold selection mode until Shift was pressed again.

9. Added direct Shift check inside Left/Right handling.

   File: `c/pico/mothpad_pico_main.c`

   Left and Right called `picocalc_kbd_shift_down()` inside their own switch
   cases before deciding whether movement should extend selection.

   Result: still not enough, because with Shift held the normal Left/Right FIFO
   events were often suppressed.

10. Added joystick-based Shift+Left/Right synthesis.

    Files:

    - `c/pico/mothpad_picocalc_platform.c`
    - `c/pico/mothpad_picocalc_platform.h`
    - `c/pico/mothpad_pico_main.c`

    Hardware observation: when Shift is held, regular Left/Right FIFO events
    can be suppressed. Upstream firmware also exposes arrow buttons through
    `REG_ID_C64_JS` / `0x0d`, where right is bit 0 and left is bit 3,
    active-low.

    Mothpad began reading joystick register `0x0d` and synthesizing horizontal
    selection while physical Shift was down.

    Result: `Shift+Left/Right` worked, but repeat behavior had timing problems.

11. Removed the Shift latch.

    The latch was no longer needed once joystick synthesis owned horizontal
    selection. The editor switched to sampling physical Shift once per frame.

    Result: the sticky-Shift failure improved. Selection stopped depending on
    stale cached Shift state.

12. Ignored FIFO Left/Right while physical Shift is down.

    Before this, FIFO Left/Right and joystick Left/Right could both affect
    movement. The result was a burst/pause wave: several characters selected,
    then a pause, then several more.

    Result: joystick synthesis became the single owner for horizontal selection
    during Shift hold.

13. Fixed keyboard polling speed.

    The platform had initialized keyboard I2C at `10kHz` and slept `16ms` after
    each keyboard register select. A frame could read FIFO, matrix, and joystick
    registers, producing roughly 50ms of deliberate keyboard delay.

    Change:

    ```text
    keyboard I2C:    10kHz -> 100kHz
    register settle: 16ms  -> 250us
    ```

    Result: the burst/pause wave went away and held selection became fast
    enough to tune.

14. Tuned repeat behavior, then separated tap behavior from hold speed.

    Early repeat tuning tried desktop-like values:

    ```text
    initial delay: 210ms, repeat: 55ms
    initial delay: 360ms, repeat: 75ms
    ```

    Hardware result: slowing repeat could protect taps somewhat, but hold
    behavior felt wrong. Quick taps could still select several characters.

    The correct fix was release debounce, not slow repeat.

15. Added release debounce.

    Mothpad now requires a short released window before the same Shift+Left or
    Shift+Right direction can become a new first press.

    Tuning tested:

    ```text
    release debounce: 90ms
    ```

    Result: quick taps became one character per press.

16. Restored fast held repeat.

    Final tuning after hardware feedback:

    ```text
    initial repeat delay: 180ms
    held repeat:          22ms
    release debounce:     90ms
    ```

    Result: user confirmed tap behavior works and hold behavior feels great.

## Failed Paths Preserved For Context

These were tried and should not be repeated as fixes:

- Only tracking FIFO Shift modifier events.
- Only enabling keyboard modifier reporting.
- Guessing alternate shifted Left/Right keycodes.
- Treating `Shift+Up/Down` success as proof that normal Shift modifier events
  are reliable.
- Polling matrix Shift inside ordinary FIFO reads.
- Adding a short Shift latch. It helped briefly, but false Shift readings could
  make selection mode stick until Shift was pressed again.
- Using FIFO Left/Right and joystick Left/Right together while Shift was down.
  They fought each other and produced burst/pause repeat behavior.
- Slowing held repeat to protect quick taps. Release debounce is the correct
  tap guard; hold speed should stay fast.

## What Not To Break

- Keep `Shift+Left/Right` owned by joystick synthesis while Shift is down.
- Keep the release debounce unless a better edge model replaces it.
- Do not restore `10kHz` keyboard I2C or `16ms` register sleeps without a
  hardware reason.
- Do not hide another Shift latch inside the editor layer.
- Do not add more core-editor selection logic for this bug. The issue was in
  hardware input interpretation, not Mothpad's text selection model.

## Retest Checklist

Use the current `mothpad_pico.uf2` on PicoCalc hardware:

- Tap `Shift+Left`: selection extends one character left.
- Tap `Shift+Right`: selection extends one character right.
- Hold `Shift+Left`: selection repeats quickly and continuously.
- Hold `Shift+Right`: selection repeats quickly and continuously.
- Release Shift: selection mode stops; plain arrows move without extending.
- `Shift+Up/Down`: still extend selection.
- Copy/Cut with a selection still acts on the selected range.
