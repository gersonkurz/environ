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

## 4B — Add / remove / reorder PATH entries (next)
- Insert a new segment, delete a segment, move up/down within a path-list variable.
- `CurrentVars` reconstruction handles structural changes (added/removed segments) — the
  `apply_segment_edits` structure-preservation only covers in-place edits, so additions/
  removals need their own path.
- Likely needs a small per-row action affordance (context menu or inline buttons) — design TBD.

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
