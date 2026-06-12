# Phase 3 — Apply / save

**Goal:** persist the in-memory edits from Phase 2 to the registry via core `EnvWriter`,
behind a confirmation preview, then reload so the grid reflects the saved state.
**This is the first phase that writes the registry** — treat it carefully.

## In scope (increment 3A)
- **Build current state:** the grid reconstructs `current` `EnvVariable` lists (per scope)
  from its edits — scalar value = edited cell; path-list value = rejoin of edited segments
  (only for variables actually edited, to avoid spurious normalization diffs).
- **Dry-run preview:** `compute_diff(original, current)` per scope; show the change list
  (`EnvChange::describe()`) for confirmation before any write. *3A uses a `MessageBox`; a
  themed in-app review panel is a 3B follow-up.*
- **Save:** `Ctrl+S` → preview → on confirm, `apply_document_changes(...)` (HKCU always;
  HKLM only when elevated — and machine vars aren't editable unelevated, so there are no
  machine changes to write in that case) → `WM_SETTINGCHANGE` broadcast (done by the core).
- **Reload after apply:** re-read the registry, rebuild the grid → dirty clears, saved
  state shown. Report failures (per-scope `error`) visibly; never silently succeed/fail.
- **Discard:** revert edits by reloading from the registry.

## Out of scope (later increments / phases)
- Themed review panel (3B), external-change **conflict detection** (3B).
- Add / delete / rename variables (Phase 2 only edits values, so the diff is Modify-only).
- Snapshots & history (Phase 4), settings (Phase 5), elevation relaunch (Phase 6).

## Work items
1. **Grid model:** retain the original user/machine `EnvVariable` lists; tag each `Row` with
   `scope` + `varIndex` + `segIndex`. Add `OriginalVars(scope)`, `CurrentVars(scope)`
   (overlay edits + rejoin edited path-lists), `HasChanges()`.
2. **Host save flow (`app.cpp`):** `Ctrl+S` commits any active edit, builds current lists,
   diffs, previews, applies on confirm, reports, reloads. Link `EnvWriter.cpp`.

## Acceptance criteria (review gate)
- Builds clean at `/W4 /WX`.
- Editing a user value + `Ctrl+S` shows an accurate preview; confirming writes HKCU and the
  change survives a reload *and* is visible to a newly-launched process (broadcast).
- Cancelling the preview writes nothing.
- Unedited path-lists do **not** appear as changes (no spurious normalization diffs).
- Running unelevated never writes HKLM and never crashes; machine edits (if any) are
  reported as skipped, not silently dropped.
- After a successful apply, dirty markers clear.

## Increment 3B — write-hardening + themed review
- **Path-list fidelity:** preserve the original separator structure (empty/trailing entries)
  when re-serializing an edited path-list — replace only the edited segments in place
  (core helper), instead of normalizing via `join_segments`.
- **Conflict detection:** at save, re-read the registry and compare to the load-time
  baseline; if it changed underneath us, warn before overwriting.
- **Themed review panel:** replace the `MessageBox` confirm with a custom-drawn, themed
  in-app review (scope-grouped change list, Apply/Cancel) — design confirmed with the user
  before building.

## Handoff
Run `.\review.ps1` at completion; address findings before Phase 4 (snapshots/history).
