# Refterm Reading Notes

Source read: `cmuratori/refterm`, main branch, downloaded for inspection on
2026-07-04.

License warning: refterm is GPL-2.0. Treat this as architectural reference
only unless Mothpad intentionally adopts compatible licensing. Loot patterns,
not code.

## Files Read

- `README.md`
- `refterm.hlsl`
- `refterm_glyph_cache.h`
- `refterm_glyph_cache.c`
- `refterm_example_terminal.h`
- `refterm_example_terminal.c`
- `refterm_example_d3d11.h`
- `refterm_example_d3d11.c`
- `refterm_example_source_buffer.h`
- `refterm_example_source_buffer.c`

## README Claims Confirmed

The repository presents refterm as a reference monospace terminal renderer, not
a complete terminal and not a text editor. It explicitly names the important
renderer code as:

- `refterm.hlsl`
- `refterm_glyph_cache.h`
- `refterm_glyph_cache.c`

It also explicitly warns that the `refterm_example_*` files are scaffolding for
API verification and only vague reference material. That warning is earned.
The example terminal is useful, but it is a shaggy machine full of Windows,
pipes, VT parsing, Uniscribe, D3D, and subprocess handling. Mothpad should not
inherit that furniture.

## Useful Spine

Refterm's hot shape is:

```text
input stream
-> source / scrollback buffer
-> line metadata
-> terminal cell buffer
-> renderer cell upload
-> glyph texture lookup
-> pixels
```

Mothpad's equivalent should be:

```text
file text
-> line table
-> viewport
-> MothCell buffer
-> PicoCalc display flush
```

That is the real treasure. The rest is mostly weather.

## Cell Model

`refterm.hlsl` defines a cell as:

```c
GlyphIndex
Foreground
Background
```

The shader maps a screen pixel to:

```text
screen position
-> cell index
-> position inside cell
-> glyph texture position
-> foreground/background blend
```

For Mothpad, use the same discipline with smaller machinery:

```c
typedef struct {
    uint16_t ch;
    uint8_t fg;
    uint8_t bg;
    uint8_t flags;
} MothCell;
```

No glyph cache is needed for v0.1 if Mothpad uses a fixed bitmap font or a
firmware text primitive. The key is not GPU rendering. The key is that document
layout writes cells, and display code consumes cells.

## Terminal Buffer Lesson

`refterm_example_terminal.h` defines:

```c
typedef struct {
    renderer_cell *Cells;
    uint32_t DimX, DimY;
    uint32_t FirstLineY;
} terminal_buffer;
```

`AllocateTerminalBuffer()` does exactly what the name says: allocate
`DimX * DimY` cells. `Clear()`, `ClearLine()`, and `GetCell()` keep the
cell-buffer boundary explicit.

Mothpad should copy the boundary, not the implementation:

- fixed `MOTH_COLS * MOTH_ROWS` array for tiny targets
- `moth_cell_at(x, y)` helper
- `moth_clear_cells()`
- render passes that fill cells before flush

## Layout Lesson

`LayoutLines()` in the terminal example clears the screen, walks recent line
metadata, replays source ranges into glyph cells, draws a prompt, draws a cursor
block, then sets `FirstLineY` for the renderer's circular screen ordering.

For Mothpad, the useful pattern is the pass order:

```text
clear cells
derive visible lines
draw document into center rows
draw prompt/status rows
draw cursor/selection overlay
flush cells
```

The circular terminal scrollback trick is not needed for a v0.1 editor. An
editor viewport should be boring: top visible line, left visible column, cursor
line, cursor column.

## Renderer Upload Lesson

`RendererDraw()` uploads a constant buffer and then uploads the terminal cells
as a structured buffer. It copies cells in two chunks because the terminal
buffer can wrap around `FirstLineY`.

Mothpad does not need that wrap unless the device display API benefits from it.
The important separation is:

```text
editor state changed
-> rebuild cells
-> flush cells
```

Do not let key handling draw directly to the display. That way lies cursed
little text box sludge.

## Glyph Cache Lesson

The glyph cache is a hash table plus LRU list. Public shape:

- configure table size and cache texture layout
- ask for glyph state by hash
- fill missing glyph texture slots
- update cache entry metadata
- read hit/miss/recycle stats

For Mothpad v0.1, quarantine this. It solves a harder problem than Mothpad has:
complex Unicode glyph runs and dynamic glyph rasterization.

Steal only the attitude:

- direct-map common ASCII if needed
- reserve simple slots for known glyphs
- add cache only after rendering proves slow

## Source Buffer Lesson

Refterm's source buffer is a circular scrollback buffer with clever double
mapping so wrapped data can be read contiguously. Good terminal trick. Wrong
first editor trick.

Mothpad is editing a file, not consuming an endless stream. Start with either:

- flat text buffer plus line table, matching the spec, or
- line-list buffer if the target runtime makes that cheaper

Do not import scrollback thinking into file editing.

## What Mothpad Should Steal

- Explicit cell buffer as the truce line between layout and display.
- Fixed rectangle layout derived from screen rows and columns.
- Render pass discipline: clear, document, bars/prompts, overlays, flush.
- Attributes as cell flags, not scattered draw-time conditionals.
- Direct mapped ASCII or fixed bitmap font path before any cache.
- Boring helper functions for bounds, cell access, and clearing.

## What Mothpad Should Refuse

- VT escape parsing.
- subprocess and pipe machinery.
- DirectWrite, Uniscribe, D3D, shaders.
- large scrollback buffer.
- dynamic glyph cache in v0.1.
- terminal circular screen ordering.
- copying GPL source by accident.

## Concrete Next Architecture

Add a small renderer model beside the current editor core:

```c
#define MOTH_COLS 40
#define MOTH_ROWS 26
#define MOTH_TOP_ROW 0
#define MOTH_BOTTOM_ROW (MOTH_ROWS - 1)
#define MOTH_TEXT_FIRST_ROW 1
#define MOTH_TEXT_ROWS (MOTH_ROWS - 2)

typedef enum {
    MOTH_CELL_CURSOR = 0x01,
    MOTH_CELL_STATUS = 0x02,
    MOTH_CELL_SELECTION = 0x04,
} MothCellFlags;

typedef struct {
    uint16_t ch;
    uint8_t fg;
    uint8_t bg;
    uint8_t flags;
} MothCell;
```

Hardware note, added after the first PicoCalc LCD tests: the ClockworkPi
hello-world font is 8x12, so the 320x320 PicoCalc display supports 40 columns
by 26 rows for Mothpad's fixed grid. The older 40x20 value was a conservative
pre-hardware placeholder.

Frame shape:

```text
moth_handle_input()
moth_update_state()
moth_clear_cells()
moth_draw_top_status()
moth_draw_document_view()
moth_draw_bottom_status_or_prompt()
moth_draw_cursor_overlay()
moth_flush_cells()
```

The device implementation carries this model directly in C; no separate Python
prototype is part of Mothpad.
