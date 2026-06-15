# Phase 4 — Editing completeness

**Goal:** the editing features that make environ fully replace the built-in dialog. Done in
reviewable increments. Saving still flows through Phase 3's review modal + `EnvWriter`.

## 4A — Rename variables (this increment)
- Make a variable's **name** editable (the name column), alongside the existing value editing.
- Trigger: **double-click the name cell** edits the name; double-clicking the value (or Enter)
  edits the value as before. Segment rows have no name.
- The editor is generalized to target either the name or value cell of a row.
- A renamed row is shown dirty (name differs from original).
- On save, `CurrentVars` sets `name` = new and `original_name` = old for renamed variables, so
  `compute_diff` emits an `EnvChange::Kind::Rename` (core already supports this end-to-end).
- Read-only (machine, unelevated) rows are not renamable.

## 4B — Add / remove / reorder PATH entries (this increment)
- **Keyboard triggers** (chosen): `Insert` = add a blank entry after the selection and edit
  it; `Delete` = remove the selected entry; `Alt+Up` / `Alt+Down` = move it (the latter via
  `WM_SYSKEYDOWN`). Only on path-list entries; read-only rows excluded. Footer hints the keys.
- **Model:** `CurrentVars` is reworked to reconstruct each variable from its **block of rows
  in display order** (the variable row + its segment rows), handling rename, in-place edits,
  and structural changes uniformly. A per-variable "structurally edited" flag selects the
  serialization: structurally-edited path-lists get a clean `join_segments`; in-place-only
  ones keep `apply_segment_edits` (preserving original empty/trailing structure); untouched
  ones keep their exact original value.
- Removing the first entry (on the variable row) promotes the next entry into it; moving
  swaps entry text between adjacent rows of the same variable so the name stays put.
- A themed context menu is still a later (Phase 8) nicety.

## 4C — Entry info display (next)
- Surface the data `EnvStore` already computes: the **expanded value** (`expanded_value` /
  `expanded_segments`), **why-invalid** (a missing path), and **duplicate-of** (the message in
  `segment_duplicate`). As a hover tooltip and/or a detail strip / status bar.

## Out of scope (later phases)
Snapshots/history (Phase 5), settings (Phase 6), elevation relaunch (Phase 7), search/clipboard/
export (Phase 8).

## Acceptance criteria (4A review gate)
- Builds clean at `/W4 /WX`.
- Double-click a variable's name → themed inline editor on the name cell; commit renames it,
  shown dirty; Esc cancels.
- Saving a rename produces a Rename in the review modal and persists (old name gone, new name
  present) across a reload and a fresh process.
- Machine names are not editable when unelevated; no registry write path regressions.
- Value editing, dirty tracking, and the review/apply flow are unchanged for value edits.

## Handoff
Run `.\review.ps1` at completion; address findings before 4B.
