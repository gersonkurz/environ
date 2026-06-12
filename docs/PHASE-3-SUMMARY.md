# environ — review context (Phase 3B)

Orientation for a code reviewer: the overall arc, then the focus of the **current staged
diff (Phase 3B)**. See also `CLAUDE.md`, `docs/ROADMAP.md`, `docs/PHASE-3.md`, and
`docs/NOTES-FOR-REVIEWERS.md`.

## What the project is
A Windows 11 environment-variable editor. **Pure Win32 + Direct2D/DirectWrite, C++20,
MSVC** — no WinRT/.NET/vcpkg, exception-free in our own code, single ~900 KB exe, instant
start. The UI host was pivoted off WinUI (which failed twice — input routing, then
deployment) to pure Win32; `src/core/` (`Environ::core`) is host-agnostic logic reused
unchanged.

## Architecture (two layers)
- **`src/core/`** — `EnvStore` (read / expand / validate / elevation; segment split + join),
  `EnvWriter` (`compute_diff`, `apply_document_changes`, `WM_SETTINGCHANGE` broadcast). No
  Win32/UI types.
- **`src/win32/`** — the host: `app.cpp` (window, message loop, save flow, modal),
  `grid.{h,cpp}` (virtualized themed grid + edit model), `theme.{h,cpp,toml}` (a
  `ColorScheme` of named per-state `Style`s; **no color literals in painting code**).
- Build: `.\build.ps1` (self-bootstraps VS18). Gate: clean at `/W4 /WX`.

## Phase progress
- **Phase 1 (committed `abd11e2`)** — read-only spine: frameless custom title bar +
  virtualized themed grid wired to `EnvStore`.
- **Phase 2 (committed `2d6093e`)** — inline editing via a skinned borderless `EDIT`, dirty
  tracking.
- **Phase 3A (committed `e37ba2e`)** — save: `Ctrl+S` -> diff preview (MessageBox) ->
  `apply_document_changes` -> reload.
- **Phase 3B (current staged diff — the thing to review)** — write-hardening + themed review
  modal.
- Next: Phase 4 snapshots/history; later niceties like restoring window placement.

## What the staged diff (3B) changes — review focus
1. **Path-list fidelity (core):** new `Environ::core::apply_segment_edits(original_value,
   edited_segments)` replaces only edited segments in place, **preserving the original
   separator structure** (empty/trailing entries the display split drops). The grid calls it
   instead of the naive `join_segments`. *Check: round-trip correctness, the count-mismatch
   fallback.*
2. **Conflict detection (host):** `SaveChanges` re-reads the registry and, if
   `compute_diff(loaded_baseline, fresh)` is non-empty, warns before overwriting.
3. **Themed review modal (host, `app.cpp`):** replaces the MessageBox. Centered card over a
   scrim, scope-grouped change list, Cancel/Apply (hover states), Enter/Esc,
   click-scrim-to-cancel. `SaveChanges` now *opens* the modal (stores `g_reviewCur*` +
   diffs); `ApplyReviewed` / `CancelReview` finish it. While open, `WndProc` intercepts input
   and the grid is inert; paint/size/dpi fall through. *Check: modal input/lifetime, that
   apply still writes correctly, no stuck state.*
4. **Theme additions:** `accentText` (on-accent button text) and `scrim` (modal backdrop)
   added to `ColorScheme` + all three built-ins + `theme.toml` (so the panel re-skins, no
   literals).

## Invariants to hold us to
- `/W4 /WX` clean; two-layer rule; no color literals in painters; exception-free our-code
  (`/EHsc` on only for toml++/STL; toml++ runs `TOML_EXCEPTIONS=0`).
- Unelevated never writes HKLM (machine rows aren't editable; `apply_changes` also guards
  defensively); never silently succeed/fail.
- On apply failure, in-memory edits are **kept** (not discarded).

## Known / intentional (don't re-flag — see `NOTES-FOR-REVIEWERS.md`)
`Ctrl+S` only when not mid-edit; double-diff (preview + apply re-diff); scalar edits keep
original `REG_SZ`/`REG_EXPAND_SZ`; save resets selection/scroll; review modal doesn't trap
the title bar; change list clips (no inner scroll) past ~half height; single-click-to-edit
deferred; `edit.border` unused (borderless by decision).

## Verified manually
Edit a value -> `Ctrl+S` -> themed review -> Apply -> relaunch shows the change (and a
freshly launched shell sees it, confirming the broadcast).
