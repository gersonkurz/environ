# Phase 2 — Inline editing

**Goal:** edit values in place with a skinned standard `EDIT` control, track dirty
state visually. **Still no registry write** — edits live in the grid's in-memory model;
persisting them is Phase 3.

## In scope
- Edit a scalar variable's value, or an individual path segment, in place.
- The editor is a **borderless standard `EDIT`** positioned over the value cell, themed
  via `WM_CTLCOLOREDIT` (the skinned-EDIT approach from the design discussion). No border
  or focus ring (deliberate user decision — see `NOTES-FOR-REVIEWERS.md`).
- Triggers: double-click a row's value, or `Enter` on the selected row. (Theme switching
  keeps `F1`/`F2`/`F3`, so `F2` is not an edit trigger.)
- Commit on `Enter`/focus-loss; cancel on `Esc`. `Tab`/`Shift+Tab` commit and move to the
  next/previous editable row.
- Dirty tracking: a row whose value differs from its original is drawn with the
  `rowDirty` style; editing it back to the original clears dirty.
- Read-only rows (machine vars when unelevated) are not editable.

## Out of scope (later phases)
Adding/removing/reordering path segments, renaming variables, and **saving to the
registry** (Phase 3). Snapshots, settings, elevation relaunch.

## Work items
1. **Theme:** add an `edit` `Style` (fill/text) to `ColorScheme` + built-ins + `theme.toml`.
   The editor is borderless; `edit.border` is reserved/unused for now.
2. **Grid model:** give each `Row` an `original` value; `dirty` = `col2 != original`.
   Add `BeginEdit()` (selected row) / `BeginEditAt(x,y)` (double-click) → value-cell rect
   (DIPs) + current text for an editable row (or none); `CommitEdit(text)` / `CancelEdit()`;
   `SelectNextEditable(dir)` for Tab; a value-cell rect helper that matches the painter.
   Draw dirty rows with `rowDirty`.
3. **Host editor (`app.cpp`):** own a child `EDIT` (`WS_CLIPCHILDREN` on the main window
   so D2D doesn't overpaint it). Subclass it to intercept `Enter`/`Esc`/`Tab`. Position it
   over the cell (pixels), set the themed font, select-all on open. `WM_CTLCOLOREDIT`
   paints it from the `edit` style. Commit/cancel writes back via the grid; end the edit
   on scroll/resize/theme-switch.

## Acceptance criteria (review gate)
- Builds clean at `/W4 /WX`, zero warnings.
- Double-click a row / `Enter` opens a borderless editor that matches the theme; `Esc`
  cancels, `Enter` commits, `Tab`/`Shift+Tab` move to the next/previous editable row.
- Committed changes show immediately; changed rows are marked dirty; reverting clears it.
- Editor tracks the right cell under scroll/resize/DPI; light/dark/blue all correct.
- Machine vars remain non-editable when unelevated; **no registry write path exists.**

## Handoff
Run `.\review.ps1` at completion; address findings before Phase 3 (apply/save).
