# Phase 1 — Read-only spine

**Goal:** prove the architecture end-to-end with the smallest slice that's worth
reviewing: a custom-titled window containing a virtualized, themed grid wired to live
`EnvStore` data. **Read-only — no editing, no registry writes.**

## In scope
- Custom title bar (frameless window, our own caption, Win11 chrome).
- A real virtualized grid control, fully theme-driven.
- Live data from `src/core` `EnvStore`: merged user + machine variables.
- Row states derived from data: scalar vs path-list, invalid path segments flagged,
  duplicates flagged, machine rows marked read-only (when unelevated).
- Keyboard navigation + mouse-wheel scrolling over a long list.
- Per-monitor DPI; light / dark / blue all correct.

## Out of scope (later phases)
Editing, dirty tracking, registry writes, snapshots/history, settings UI, elevation
relaunch, search, context menus, tooltips, export/import.

## Work items
1. **Restructure** `src/win32/`: split the proof `main.cpp` into
   `app.cpp` (entry, window, message loop, data load) and `grid.{h,cpp}` (the control).
   Keep `theme.*`. `build.ps1` already globs `*.cpp`, so no build change needed.
2. **Custom title bar:** `WM_NCCALCSIZE` to drop the default frame; draw the caption
   (title + min/max/close) with D2D; `WM_NCHITTEST` → caption drag, button zones,
   resize borders; keep Win11 rounded corners + dark/light title attribute driven by the
   scheme; double-click-maximize and system menu.
3. **Grid model:** adapt `EnvVariable` into a flat display-row list. Merged user+machine.
   Path-list variables expand into per-segment child rows (mirrors prior app behavior).
   A header row. Track scope + read-only per row.
4. **Grid control (virtualized):** paint only visible rows; custom vertical scrollbar
   (thumb hit-test + drag, `WM_MOUSEWHEEL`); hover via `TrackMouseEvent`/`WM_MOUSELEAVE`;
   selection; keyboard (Up/Down/PgUp/PgDn/Home/End). No animation.
5. **Theme additions:** extend `ColorScheme` + `theme.toml` (and the built-in fallback)
   with `row_hover`, `header`, `duplicate`, and a `machine_readonly` style. Keep built-ins
   and TOML in sync.
6. **Data load (read-only):** on startup call `read_variables(User)` +
   `read_variables(Machine)`, then `expand_and_validate`, then `detect_duplicates`;
   build the row list. Use `is_elevated()` to mark machine rows read-only. All of this
   stays in the host calling into core — **no logic added to `src/core`, no UI types in core.**

## Acceptance criteria (the review gate)
- Builds clean at `/W4 /WX`, zero warnings.
- Window opens with a custom title bar; min/max/close/drag/resize all work.
- Real user and machine variables are listed; path-lists show per-segment rows.
- Invalid path segments and duplicates are visibly flagged via theme styles.
- Mouse-wheel and keyboard scrolling are smooth over a long list (virtualized).
- light / dark / blue all render correctly; DPI scaling correct on a 150%/200% monitor.
- Machine variables are visibly read-only when unelevated; running unelevated never crashes.
- Exe stays small; startup is immediate.
- **No code path writes the registry.**

## Handoff
At completion, run `.\review.ps1` and address the `code-reviewer` findings before
starting Phase 2.
