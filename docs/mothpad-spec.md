# Mothpad PicoCalc Text Editor Spec

## Core Thesis

Mothpad should be a Notepad-shaped editor for PicoCalc first: simple, obvious,
and plain-text-first. Not vi. Not emacs. Not an editor lifestyle cult. A clean
little knife in the drawer.

Implementation note as of 2026-07-05: the current build is a Pico SDK UF2 for
PicoCalc.

## Deployment Compatibility

The clean `mothpad_pico.uf2` target is known to run under Pelrun's UF2 Loader
on PicoCalc with a Pico 2 or Pico 2W. Put the UF2 in the SD card's
`pico2-apps` directory and launch it from Pelrun's menu. This route was
hardware-verified on 2026-07-04.

This is a deployment fact, not a claim that Mothpad depends on Pelrun. The
editor does not need the older ClockworkPi multibooter `.bin` offset path, and
the core should remain portable to another confirmed display/keyboard shell.

## Primary Goal

Open text. Change text. Save text. Leave cleanly.

The editor should make PicoCalc feel like a trustworthy pocket writing machine:
a field notebook, config editor, script scratchpad, and small-program organelle.

## Non-Goals

- No modal editing.
- No vim/emacs key cult.
- No proprietary note database.
- No hidden workspace requirement.
- No markdown preview in v0.1.
- No plugin architecture yet.
- No syntax highlighting yet.
- No split panes or tabs yet.
- No callable editor service for separate Pelrun UF2 applications.
- No AI assistant dangling from the ceiling like a damp bat.

## Design Principle

Plain text is infrastructure.

Mothpad should edit config files, notes, scripts, logs, todo lists, generated
files, and small game data tables.

The editor should not own the world. It should open text, change text, and save
it without inventing a runtime around itself.

## Usage Shape

Standalone app:

```text
Mothpad
> new
> open
> recent
```

## Basic Keyboard Behavior

- Arrow keys move the cursor.
- Typing inserts text.
- Backspace deletes before cursor.
- Delete deletes after cursor.
- Enter inserts newline.
- Ctrl+S saves.
- Ctrl+O opens.
- Ctrl+N creates new.
- Ctrl+F finds.
- Ctrl+Q requests a reboot; a dirty document gets a confirmation prompt first.
- Escape dismisses active menus or prompts and returns from the calculator. It
  has no quit action while normal editing is active.

No normal mode. No insert mode. No state where typing letters mysteriously does
not type letters.

## Current Clean Target Behavior

The following is implemented in the clean `mothpad_pico.uf2` target. It is the
current product behavior, not a proposal.

- Documents are bounded to 65,536 bytes and 4,096 logical lines. The editor
  keeps 128 undo records; inserts, paste, selection replacement, cut, and
  auto-indent group their related changes where practical.
- The editor has physical-key editing, Home/End, soft wrap, selection with
  Shift+Arrow, Select All/None, find-next with a persistent query, and an
  internal session-only clipboard. Tabs insert literal tab characters at two
  columns by default; the settings file can select two or four spaces instead.
  Enter copies the current line's leading spaces and tabs onto the new line.
- F1-F4 open File, Edit, Select, and View menus. File includes New, Open,
  Save, Save As, a persistent Keep Backups toggle, and Reboot. Edit includes
  Undo, Cut, Copy, and Paste. Select includes Find, Select All, and Select
  None. View includes persistent Wrap plus Edit Mode and Read Mode.
- Read Mode hides the cursor and blocks edits. Left/Right scroll one line,
  Up/Down scroll one page, and Home/End jump to the document bounds. Returning
  to Edit Mode places the cursor at the top of the visible text.
- The Open surface is a file browser: directories sort before files, `..` is
  first, and names use case-insensitive ASCII sorting. It can create folders,
  copy or move files, paste a file into the current folder, rename, and delete
  files or empty directories after confirmation. Directory copy and deletion of
  non-empty directories are intentionally blocked. A selected text file gets a
  right-side preview. At the SD root, up to five recent files can appear as
  shortcuts; the persistent Recent Root toggle controls that shelf.
- File-browser commands have direct accelerators: `O` Open, `N` New Folder,
  `C` Copy, `X` Cut, `V` Paste, `R` Rename, `D` Delete, and `E` Recent Root.
  Recent-root shortcuts can be opened or previewed, but are not renamed, moved,
  or deleted there.
- New, Open, and Reboot protect dirty work with Cancel, Discard, and Save First
  choices. Missing SD storage blocks file actions without discarding the
  in-memory document, so the editor remains usable as a scratchpad.
- F5 opens a session calculator. It evaluates basic `+`, `-`, `*`, `/`, `x`,
  decimal, and parenthesized expressions; updates the visible result while
  typing; retains eight expression/result entries for Up/Down recall; and
  returns to the editor with Esc or F5. This calculator is part of Mothpad's
  UF2, not a separate Pelrun application.
- The hardware shell shows a short splash at boot, polls the PicoCalc battery
  and charging state, uses a 40x26 cell display, and starts a full-screen LCD
  scrub screensaver after 60 seconds without input. The wake key only wakes the
  display; it is not inserted into the document.

## Safe Save Behavior

The editor's first sacred duty is: do not eat Morgan's words.

Recommended save pattern:

1. Write to `file.txt.tmp`.
2. Verify write succeeded.
3. Preserve previous file as `file.txt.bak`.
4. Replace original with temp.
5. Clear dirty flag only after success.

The `.bak` step should be user-controllable. When `Keep Backups` is off, manual
save still writes through a temp file but does not preserve the previous version.

If power dies or the SD card gets weird, the text should have a way to crawl out
wearing a little helmet.

Autosave must not silently overwrite the primary file. For v0.1, the editor
writes one global recovery file:

```text
/.mothpad-recovery.txt
```

On boot, if that file exists, Mothpad asks whether to open it. After eight
seconds of quiet dirty editing, recovery writes use a temporary file and rename,
and never replace the primary document. Opening recovery loads it as a dirty
draft, restores the original path from `/.mothpad-recovery.meta` when available,
and leaves manual save explicit. Opening, selecting **Ignore**, or successfully
saving clears stale recovery text and metadata; dismissing the prompt leaves it
available at the next boot.

The current settings and recent-file records are also plain text:

```text
/.mothpad-settings.txt
/.mothpad-recent.txt
```

Settings persist Keep Backups, soft wrap, tab insertion mode, tab width, and
whether the recent-file shelf appears at the SD root. Recent entries are pruned
when their files no longer exist.

## File Format Rules

- Plain files only.
- Preserve line endings where reasonable.
- Manual Save writes the current buffer; Mothpad does not automatically rewrite
  the primary document.
- Do not store editor-specific metadata inside user files unless explicitly
  requested.

## Example File Paths

```text
/logs/today.txt
/scripts/test.bas
/doglands/forts.txt
/config/theme.txt
/apps/bramblevale/rooms.txt
```

## Status Bar

The editor uses the 40-column top row for the current path and battery state:

```text
/notes/doglands.txt  *             84% [battery]
```

- An unnamed buffer appears as `[new]`.
- `*` follows the path only when the buffer is dirty.
- The battery percentage and charge glyphs live at the right edge; unavailable
  battery data is shown as `--`.
- Read Mode does not add an `RO` indicator, and the current build shows no line
  or column counter.
- The bottom row is normally empty. It becomes a transient message strip for
  save results, errors, and similar short feedback; menus and prompts replace
  it with their own instructions while open.

## Runtime Boundary

Pelrun launches one UF2 application at a time. Mothpad therefore has no
cross-application caller/return contract and does not promise to open a path on
behalf of another application.

The C editor core may still be embedded by code linked into the same UF2. That
is ordinary code reuse, not an inter-application service ABI.

## First Implementation Shape

Do not start with a fancy editor data structure unless the hardware or file size
demands it.

A simple flat buffer is acceptable for v0.1:

```c
#define MAX_FILE_SIZE 65536
char buffer[MAX_FILE_SIZE];
int len;
int cursor;
int scroll_line;
bool dirty;
```

Core functions:

```text
load_file()
save_file()
draw_editor()
cursor_left()
cursor_right()
cursor_up()
cursor_down()
insert_char()
backspace()
delete_char()
line_col_from_cursor()
cursor_from_line_col()
```

Flat-buffer cursor up/down is annoying, but contained.

## Display And Interaction Assumptions

PicoCalc's screen is small and square, so the UI should be calm:

- One file at a time.
- No split panes in v0.1.
- Text wrapping should be predictable.
- Horizontal scrolling may be acceptable for code/config, but soft wrap should
  be available for notes.
- Menus should be shallow.
- Error messages should be short and useful.

## Plain Text Conventions To Tolerate

These are ordinary text. Mothpad does not parse, style, activate, preview, or
toggle them; it merely lets a file contain them without getting clever:

```text
# heading
- bullet
[] todo
[x] done
@tag
[[link to another note]]
```

## Vibe Rule

Every feature should answer:

Does this make the PicoCalc feel more like a trustworthy pocket writing machine?

If yes, consider it. If no, throw it into the feature bog.

## Slogan

A text editor for plain files.

Not an empire. Not a religion. Just plain text, saved safely, on a tiny square
bench.

## Implementation Reference

The cell-grid renderer reading belongs in [Refterm Reading Notes](refterm-reading.md),
not in this product spec.
