# Mothpad Field Report: Southbank Bus Test

Source label: `mon 7-jun-2026`

Status: field notes triaged

## Context

First real attempt to use Mothpad in the field on the PicoCalc while traveling
to and from Southbank. The note was written live on the device across an
afternoon and evening meetup trip.

Battery observations:

- 14:41: 100%
- 15:33: 100%
- 16:41: 100%, device had been off for a while
- 18:16: 99%
- 18:32: 98%
- 20:15: 98%
- 21:39: 98%
- 21:51: 97%

The battery result is excellent for writing use. The more interesting failure
mode is not power. It is field ergonomics: saving, scrolling, key confidence,
and avoiding accidental edits while carrying the device.

## Bugs / Sharp Edges

- Backspace reportedly jumped back one character too far during writing.
- Holding Left or Right sometimes selected one character erroneously on a
  regular timing.
- Back should alias to Esc in non-text-entry popups and menus.
- Shift alone might need a defined behavior: either select one character like
  Shift+Right, or switch the cursor to a vertical bar to show insertion point.
- Long files are slow to navigate to the end. Shift+Tab / Shift+Del were noted
  as Home/End-like, but selected to line start/end rather than document-level
  movement.
- The physical keyboard is loud enough to matter for covert notetaking.
- Long typing causes left-hand cramping.

## Feature Requests

- Create directory from the file UI.
- Toggle soft wrap for long lines.
- Autosave.
- Send clipboard or files to iPad when networking is available.
- Scrollbar for long files.
- Use `*` instead of `MOD` in the header for dirty state.
- Reduce or vary static white top/bottom bars if burn-in is a plausible LCD
  concern.
- Calculator tool inside Mothpad, possibly on F5.
- Open Recent:
  - highlight the most recent item in the open menu, or
  - show a top section with the five most recent files across directories,
    including paths.
- Toggle key input / write lock for bag carry, maybe Alt+F5.
- Consider a read-only scroll mode so the device can be left on briefly without
  accidental edits.
- Faster scrolling / page movement.

## Triage

High priority:

- Verify safe save in field conditions: save-as, reopen, edit, save again,
  `.bak`.
- Investigate the Backspace off-by-one report.
- Investigate accidental Shift/Left/Right selection while holding arrows.
- Add Back-as-Esc behavior where Back does not mean text deletion.
- Add autosave or a lower-friction save prompt before more decorative features.

Medium priority:

- Open Recent.
- Create directory.
- Write lock / scroll-only mode.
- Soft wrap toggle.
- Faster long-file navigation and scrollbar.

Low priority / later:

- Clipboard/file transfer to iPad.
- Calculator tool.
- Burn-in mitigation, unless hardware evidence says the white bars are risky.

## Immediate Code Response

- Dirty marker changed from `MOD` to `*`.
- Backspace now aliases Esc in menu, file list, and dirty-confirm modes. Save
  and find prompts keep Backspace as text deletion.
- Autosave is implemented as one global recovery file,
  `/.mothpad-recovery.txt`, after 8 seconds of quiet dirty editing. On boot,
  Mothpad asks whether to open it. Opening recovery loads an unnamed dirty draft
  rather than overwriting the original file.
- File menu now has a `[x] Keep Backups` boolean item controlling whether manual
  save keeps `file.bak`.
- View menu now has `[ ] Wrap` and `[ ] Write Lock` boolean items. F4 opens View;
  F5 toggles Write Lock directly.

## Next Honest Move

Build and test the current dirty tree, then run a hardware acceptance pass that
specifically tries:

- Backspace in a normal document around line boundaries.
- Backspace in menus, file list, save-as, find, and dirty-confirm.
- Holding Left/Right with and without Shift.
- Save-as, reopen, edit, save, and verify `.bak`.
- Edit a file, pause for 8 seconds, verify `Recovery saved`, reboot/relaunch,
  and confirm the boot prompt can open the recovered draft.
- Toggle File > Keep Backups off, save twice, and verify no `.bak` remains.
- Toggle View > Wrap and inspect a long line. Toggle View > Write Lock and
  verify typing/delete/paste/cut/undo are blocked while movement/copy still work.
- Long-file navigation from top to end.
