# Phase 4 — Editing completeness

**Goal:** the editing features that make environ fully replace the built-in dialog. Done in
reviewable increments. Saving still flows through Phase 3's review modal + `EnvWriter`.

## 4A — Rename variables (done)
- Make a variable's **name** editable (the name column), alongside the existing value editing.
- Trigger: **double-click the name cell** edits the name; double-clicking the value (or Enter)
  edits the value as before. Segment rows have no name.
- The editor is generalized to target either the name or value cell of a row.
- A renamed row is shown dirty (name differs from original).
- On save, `CurrentVars` sets `name` = new and `original_name` = old for renamed variables, so
  `compute_diff` emits an `EnvChange::Kind::Rename` (core already supports this end-to-end).
- Read-only (machine, unelevated) rows are not renamable.

## 4B — Add / remove / reorder PATH entries (done)
- **Keyboard triggers:** `Insert` = add a blank entry after the selection and edit
  it; `Delete` = remove the selected entry; `Alt+Up` / `Alt+Down` = move it (the latter via
  `WM_SYSKEYDOWN`). Only on path-list entries; read-only rows excluded. Footer hints the keys.
- **Model:** `CurrentVars` reconstructs each variable from its **block of rows in display
  order** (the variable row + its segment rows), handling rename, in-place edits, and
  structural changes uniformly. A per-variable "structurally edited" flag selects the
  serialization: structurally-edited path-lists get a clean `join_segments`; in-place-only
  ones keep `apply_segment_edits` (preserving original empty/trailing structure); untouched
  ones keep their exact original value.
- Removing the first entry (on the variable row) promotes the next entry into it; moving
  swaps entry text between adjacent rows of the same variable so the name stays put.

## 4C — Entry info display (done)
- Surface the data `EnvStore` already computes: the **expanded value** (`expanded_value` /
  `expanded_segments`), **why-invalid** (a missing path), and **duplicate-of** (the message in
  `segment_duplicate`). As a hover tooltip and/or a detail strip / status bar.
- **Fix `%VAR%` expansion false positives:** paths containing unexpanded environment variables
  (e.g. `%USERPROFILE%\Go\bin`) are currently flagged invalid because validation checks the
  literal text, not the expanded form. The fix belongs in `EnvStore::expand_and_validate` —
  expand `%VAR%` references before checking whether the path exists.

## 4D — Context menu + clipboard (next)
- **Right-click context menu** on grid rows: Insert entry, Remove entry (mirrors the keyboard
  triggers from 4B), Copy.
- **Copy-to-clipboard:** copy one or more selected variables / entries as text. Single entry →
  its value; variable → `NAME=value`; multiple selection → newline-separated.
- Themed owner-drawn menu consistent with the rest of the UI.

## Out of scope (later phases)
Snapshots/history (Phase 5), settings & theming incl. Ctrl+Mousewheel zoom and PATH-like
variable list (Phase 6), elevation relaunch (Phase 7), search/TOML export/UIA (Phase 8).

## Acceptance criteria (4C review gate)
- Builds clean at `/W4 /WX`.
- Paths with `%VAR%` references that resolve to valid directories are no longer flagged invalid.
- Hover or detail strip shows expanded value, invalid reason, or duplicate info for the
  selected / hovered entry.
- No regressions in editing, dirty tracking, or the review/apply flow.

## Handoff
Run `.\review.ps1` at completion of each increment; address findings before the next.
