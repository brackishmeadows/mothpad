# Mothpad PicoCalc Text Editor Spec

## Core Thesis

Mothpad should be a Notepad-shaped editor for PicoCalc first: simple, obvious,
plain-text-first, and friendly to other programs. Not vi. Not emacs. Not an
editor lifestyle cult. A clean little shared knife in the drawer.

Implementation note as of 2026-07-05: the current build is a Pico SDK UF2 for
PicoCalc. It does not currently run inside Picoware. Picoware references below
are future integration goals, not current support claims.

## Primary Goal

Open text. Change text. Save text. Leave cleanly.

The editor should make PicoCalc feel like a trustworthy pocket writing machine:
a field notebook, config editor, script scratchpad, daily log, and small-program
organelle.

## Non-Goals

- No modal editing.
- No vim/emacs key cult.
- No proprietary note database.
- No hidden workspace requirement.
- No markdown preview in v0.1.
- No plugin architecture yet.
- No syntax highlighting yet.
- No split panes or tabs yet.
- No AI assistant dangling from the ceiling like a damp bat.

## Design Principle

Plain text is infrastructure.

Mothpad should play nicely with config files, notes, scripts, logs, todo lists,
generated files, small game data tables, and other Picoware apps that need to
edit text.

The editor should not own the world. It should be a borrowed bench.

## Usage Shape

Standalone app:

```text
Mothpad
> new
> open
> recent
```

Borrowed by other apps:

```text
edit /apps/doglands/forts.txt
edit /config/wifi_notes.txt
edit /logs/2026-07-03.txt
```

Other programs should be able to open Mothpad with a path, let the user edit,
then receive a clean return result: saved, cancelled, dirty quit refused, or
error.

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
- Ctrl+Q or Escape opens quit/back behavior.

No normal mode. No insert mode. No state where typing letters mysteriously does
not type letters.

## Version 0.1 Feature Set

Required:

- Open plain text file.
- Create new file.
- Edit text.
- Save safely.
- Save as.
- Show filename and dirty flag.
- Cursor movement.
- Scrolling.
- Basic find.
- Confirm quit if unsaved.
- Return cleanly to caller.
- Bounded undo for ordinary text edits.

Soon after:

- Recent files.
- Daily quick note.
- Basic word/line/character count.
- File associations from Picoware file manager.

Later, maybe:

- Plain-text link navigation using `[[link]]` syntax.
- Align table columns.
- Roll from simple dice tables.
- Syntax highlighting.
- Multiple buffers.
- Rich undo/redo grouping.

## Safe Save Behavior

The editor's first sacred duty is: do not eat Morgan's words.

Recommended save pattern:

1. Write to `file.txt.tmp`.
2. Verify write succeeded.
3. Preserve previous file as `file.txt.bak`.
4. Replace original with temp.
5. Clear dirty flag only after success.

If power dies or the SD card gets weird, the text should have a way to crawl out
wearing a little helmet.

## File Format Rules

- Plain files only.
- Preserve line endings where reasonable.
- Save only when changed.
- Avoid rewriting files unnecessarily.
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

A single useful status bar is enough:

```text
notes/doglands.txt     Ln 12 Col 08     MOD
```

Possible indicators:

- filename/path
- line and column
- dirty flag: `MOD`
- read-only flag: `RO`
- insert/overwrite only if overwrite ever exists, which it probably should not
  in v0.1

## Good Citizen API Shape

Mothpad should exist as both:

1. A standalone app.
2. A reusable editing component or callable service for other Picoware programs.

Possible C-ish shape:

```c
mothpad_main(path_optional);

TextEditResult edit_text_buffer(TextBuffer *buffer, TextEditOptions options);
```

Possible return states:

```c
TEXT_EDIT_SAVED
TEXT_EDIT_CANCELLED
TEXT_EDIT_UNCHANGED
TEXT_EDIT_ERROR
```

This lets other small tools borrow the editor instead of each building their own
cursed little text box.

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

Flat-buffer cursor up/down is annoying, but contained. Later upgrades can use a
gap buffer, line index cache, or piece table if the editor grows fangs.

## Display And Interaction Assumptions

PicoCalc's screen is small and square, so the UI should be calm:

- One file at a time.
- No split panes in v0.1.
- Text wrapping should be predictable.
- Horizontal scrolling may be acceptable for code/config, but soft wrap should
  be available for notes.
- Menus should be shallow.
- Error messages should be short and useful.

## Command Palette

Instead of deep menus, use a small command palette later:

```text
> save
> open
> find
> goto line
> new
> word count
```

This fits a keyboard-first pocket machine better than nested UI sludge.

## Quick Capture Mode

A killer feature for PicoCalc:

From launcher: quick note.

It opens today's log file, cursor at bottom:

```text
/log/2026-07-03.txt
```

Type thought. Save. Leave.

This turns PicoCalc into a thought trap with manners.

## Plain Text Conventions To Tolerate

Do not render these at first. Just allow them to exist as useful text:

```text
# heading
- bullet
[] todo
[x] done
@tag
[[link to another note]]
```

Later Mothpad can notice them. In v0.1 they are just text.

## File Associations

Eventually Picoware should open these in Mothpad:

```text
.txt
.md
.bas
.py
.cfg
.log
.ini
.csv maybe, as plain text
```

Other apps should be able to specify a file and ask Mothpad to open it directly.

## Possible Morgan-Native Extensions

Daily log mode:

```text
2026-07-03
- tested gameboy emulator. bad.
- maybe make text editor.
- house dragon ended season 2. everyone is trapped in inheritance goo.
```

Deck/wiki mode:

```text
[[Doglands/Forts]]
[[PicoCalc/Ideas]]
[[Marnor/NPCs]]
```

Cursor on link, press Enter, open or create the target file.

Table align mode:

```text
item        cost    note
rent        300     paid
coffee      6       bad idea?
bus         .50     mercy fare
```

Dice table mode:

```text
1d6 fort problem
1 disputed heir
2 missing well
3 unpaid mercenaries
4 saint bones
5 false map
6 wet horse
```

Highlight/select table, roll one result. This can wait. The frog council may
assess it later.

## Vibe Rule

Every feature should answer:

Does this make the PicoCalc feel more like a trustworthy pocket writing machine?

If yes, consider it. If no, throw it into the feature bog.

## Slogan

A text editor for other programs to borrow.

Not an empire. Not a religion. Just plain text, saved safely, on a tiny square
bench.

## Refterm Source Notes

Repository examined: `cmuratori/refterm`

Detailed reading note: `docs/refterm-reading.md`

Refterm is not a text editor. It is a reference renderer for monospace terminal
displays. The useful part for Mothpad is not terminal behavior itself, but the
architecture of rendering text through an explicit cell grid.

Important warning:

The refterm README says the main reference renderer code lives in three simple
files:

- `refterm.hlsl`
- `refterm_glyph_cache.h`
- `refterm_glyph_cache.c`

The `refterm_example_*` files are scaffolding for testing the API and should be
treated as vague reference, not production design to copy. For Mothpad: loot
patterns, do not inherit the shrine.

## Core Architecture Observed

Refterm roughly flows like this:

```text
bytes / output stream
-> source / scrollback buffer
-> line table
-> screen cell buffer
-> renderer draws cells as glyph tiles
```

The Mothpad equivalent should be:

```text
text buffer
-> line table
-> viewport
-> cell buffer
-> display
```

This is the useful spine.

## Cell-Buffer Idea

Refterm represents visible output as terminal cells. Each cell stores a glyph
index, foreground color, and background color.

For Mothpad, use the same conceptual pattern, but smaller and simpler:

```c
typedef struct {
    uint16_t glyph;
    uint8_t fg;
    uint8_t bg;
    uint8_t flags;
} MothCell;

MothCell cells[SCREEN_COLS * SCREEN_ROWS];
```

Each frame:

```text
clear cells
layout visible document into cells
draw status bars into cells
draw prompts / overlays into cells
flush cells to PicoCalc display
```

This means the document, status bars, menus, prompts, cursor, and selection all
use the same simple rendering language: cells with meaning.

## Separate Text From View

Do not let cursor movement, file storage, wrapping, and drawing all chew on the
same rope.

Use separate layers:

- text buffer: actual document contents
- line table: logical and/or visual line starts
- viewport: which portion of the document is visible
- cell buffer: the actual screen image about to be drawn

Possible v0.1 structure:

```c
char text[MAX_FILE_SIZE];
int text_len;
int cursor;

MothLine lines[MAX_LINES];
int line_count;

MothCell cells[COLS * ROWS];
```

Possible line record:

```c
typedef struct {
    int start;
    int end;
    int flags;
} MothLine;
```

Later this can grow visual wrapping information, cached width, dirty spans, or
syntax flags. Do not start there unless needed.

## Layout Derived From Rectangle

Refterm derives terminal dimensions from window size, margin, and font cell
size.

Mothpad should do the same conceptually, but fixed for PicoCalc:

```text
row 0: top status bar
rows 1 through screen_rows - 2: document viewport
row screen_rows - 1: bottom status / prompt
```

Everything flows from the rectangle.

Text owns the center. System speaks at the edges.

## State Model

Use explicit UI modes:

```c
typedef enum {
    MOTH_EDITING,
    MOTH_MENU,
    MOTH_OPEN_FILE,
    MOTH_SAVE_AS,
    MOTH_FIND,
    MOTH_GOTO_LINE,
    MOTH_CONFIRM_QUIT,
    MOTH_ERROR,
} MothMode;
```

Suggested frame loop:

```c
void mothpad_frame(Mothpad *m) {
    read_input(m);
    update_state(m);

    clear_cells(m);
    draw_top_bar(m);
    draw_document_view(m);
    draw_bottom_bar_or_prompt(m);

    if (m->mode == MOTH_MENU) draw_menu_overlay(m);
    if (m->mode == MOTH_CONFIRM_QUIT) draw_confirm_overlay(m);
    if (m->mode == MOTH_ERROR) draw_error_overlay(m);

    flush_cells_to_display(m);
}
```

This keeps the editor understandable. Input mutates state. Layout writes cells.
Display flushes cells. No wet spaghetti.

## Renderer Lesson

Refterm's shader maps:

```text
screen pixel -> cell -> glyph -> foreground/background blend
```

Mothpad should map:

```text
cell -> font bitmap -> display pixels
```

Same abstraction, smaller backpack. No GPU cathedral required.

## What Not To Steal

Do not copy these into Mothpad v0.1:

- DirectWrite glyph generation
- Uniscribe complex shaping
- D3D11 renderer / shaders
- Windows pipes and subprocess terminal behavior
- VT escape parsing
- huge scrollback ring buffer
- glyph cache LRU, unless rendering becomes slow enough to justify it

For PicoCalc, start with:

- fixed bitmap font
- ASCII-first or simple UTF-8 tolerance
- fixed grid
- plain screen cell buffer
- simple colors or attributes

`stb` libraries are acceptable later for a specific earned job, such as bitmap
font or image handling. They should not become a default dependency of the text
buffer, editor state, or caller API.

Let Unicode dragons sleep behind the laundromat for now.

## Practical Mothpad Rendering Plan

```c
#define SCREEN_COLS 40
#define SCREEN_ROWS 26
#define TOP_ROW 0
#define BOTTOM_ROW (SCREEN_ROWS - 1)
#define TEXT_FIRST_ROW 1
#define TEXT_ROWS (SCREEN_ROWS - 2)
```

```c
typedef struct {
    uint16_t ch;
    uint8_t fg;
    uint8_t bg;
    uint8_t flags;
} MothCell;
```

```c
typedef struct {
    char text[MAX_FILE_SIZE];
    int text_len;
    int cursor;
    int scroll_line;
    bool dirty;
    MothMode mode;
    MothCell cells[SCREEN_COLS * SCREEN_ROWS];
} Mothpad;
```

Draw in passes:

```text
clear_cells()
draw_top_bar()
draw_text_view()
draw_cursor_overlay()
draw_bottom_bar()
draw_active_overlay()
flush_cells()
```

## Why This Matters

A PicoCalc text editor should not draw text directly from scattered editor
logic. The cell buffer acts as a truce line between the document and the
display.

It makes status bars boring. It makes prompts boring. It makes menus boring. It
makes cursor drawing boring.

Boring here is good. Boring is the floor not collapsing.

## Final Takeaway

Refterm confirms that Mothpad should be cell-grid first.

Not because Mothpad is a terminal, but because a small-screen text editor wants
terminal-like discipline:

- fixed cells
- explicit rows
- visible buffer separate from document state
- layout pass before draw pass
- status bars as reserved rows
- cursor and selection as overlays
- simple cache only if needed
- no smearing UI logic into file logic

The Mothpad spine:

```text
TEXT BUFFER
-> LINE TABLE
-> VIEWPORT
-> CELL BUFFER
-> DISPLAY
```

That is the little door. Crooked hinge, still opens.
