# environ — Roadmap

## What this is
A modern Windows 11 environment-variable editor — a replacement for the cramped
built-in dialog. Native, fast, themeable, and a single small self-contained exe.

## Direction (decided 2026-05-30)
The UI host is **pure Win32 + Direct2D + DirectWrite**. We arrived here after two
C++/WinUI attempts failed in two different WinUI subsystems (input routing, then
deployment — see `docs/decisions/` history / project memory). Win32 + D2D deletes
that entire failure surface:

- one self-contained exe (~900 KB with core + deps linked), **no runtime to deploy**, instant startup
- full control over look and input — no black-box framework
- the host-agnostic `src/core/` logic is reused unchanged

Rejected alternatives: Qt (heavy new dep), C#/.NET WinUI (abandons the no-.NET rule),
staying on C++/WinUI (the path that failed twice).

## Design principles
- **No animation.** Paint-on-change only. Favors speed and matches the user's setup.
- **Modern = typography + palette, not motion.** Segoe UI Variable via DirectWrite,
  a careful color table, ~1px borders, Win11 chrome (Mica/rounded/dark title bar).
- **One theme table.** Every painter reads named per-state styles from a `ColorScheme`
  (inspired by ProAKT's `DataSet`/`ColorScheme`), loaded from `theme.toml`. No color
  literals in painting code.
- **Custom-drawn everything**, except the transient inline cell editor, which is a
  skinned standard `EDIT` (borderless, `WM_CTLCOLOREDIT`).
- **Tiny and dependency-light.** No new deps without asking. No vcpkg.

## Architecture
- `src/core/` — plain C++20 logic (`Environ::core`). No Win32/UI, no WinRT. **Stays.**
  `EnvStore` (read/expand/validate, elevation), `EnvWriter` (diff/apply/broadcast),
  `SnapshotStore` (SQLite history), `AppSettings`.
- `src/win32/` — the **new host**. `app`/window + message loop, the grid control,
  `theme.*`. Pure Win32 + D2D/DWrite.
- **Retired** (kept on disk until the pivot fully lands, then deleted):
  `scratch/EnvironNativeBaseline/` (WinUI baseline) and `src/ui/` (old handcrafted host).

## Build & run
```
just build             # build for native arch (x64 or arm64)
just build-arm64       # explicit ARM64 (cross-compile on x64 host)
just run               # build + launch
just clean             # wipe build dirs
```
Or via PowerShell directly: `.\build.ps1 [-Arch x64|arm64] [-Run] [-Clean]`.
Output: `build-x64\environ.exe` or `build-arm64\environ.exe` (+ `theme.toml` beside it).
Gate: builds clean at `/W4 /WX`, zero warnings.

## Phases
- **Phase 0 — Foundation (done).** Build loop, D2D/DWrite proof window, theme engine
  (TOML-driven, dark/light/blue, live swap).
- **Phase 1 — Read-only spine (done).** Custom title bar + virtualized themed grid
  wired to `EnvStore`, showing real user/machine vars. No editing. See `docs/PHASE-1.md`.
- **Phase 2 — Inline editing (done).** Skinned `EDIT` cell editor, dirty tracking,
  per-segment path editing. No registry write yet. See `docs/PHASE-2.md`.
- **Phase 3 — Apply / save (done).** `EnvWriter` apply path, dry-run review,
  `WM_SETTINGCHANGE` broadcast (3A); themed review modal, conflict detection, path-list
  fidelity (3B). See `docs/PHASE-3.md`.
- **Phase 4 — Editing completeness.**
  - **4A — Rename variables (done).** Editable name column, `EnvChange::Kind::Rename`.
  - **4B — Add / remove / reorder PATH entries (done).** Keyboard triggers (`Insert`,
    `Delete`, `Alt+Up/Down`), structural editing model, per-variable dirty flags.
  - **4C — Entry info display (done).** Detail strip showing expanded value, why-invalid,
    duplicate-of. Fixed `%VAR%` expansion for REG_SZ variables in validation.
  - **4D — Context menu + clipboard (done).** Right-click context menu with context-sensitive
    Insert/Remove/Copy (path entries vs whole variables). Single-row copy-to-clipboard.
    Variable-level insert (new blank variable) and delete.
  - **4E — Multi-row selection + clipboard (later).** The grid is fully custom-drawn (no
    underlying ListView), so multi-select (Shift-click ranges, Ctrl-click toggles) must be
    built from scratch. Goal: multi-row Copy; keep `m_selected` for all other operations.
- **Phase 5 — Snapshots & history.** `SnapshotStore` wiring, history view, restore, diff —
  an undo/safety net over the registry writes.
- **Phase 6 — Settings & theming UI.** In-app theme switch, custom schemes, window
  placement persistence, **Ctrl+Mousewheel zoom**, a **typography section in the theme
  table** (font family/size/weight — currently fonts live in code), UI **metrics**
  (caption/row height, button width) for Compact/Comfortable density modes, and a
  **user-configurable list of PATH-like variable names** (which variables get segment
  expansion; overlaps with `KnowledgeBase`).
- **Phase 7 — Elevation.** "Restart as Administrator" for `HKLM` editing; machine vars
  read-only until then.
- **Phase 8 — Polish.** Search/filter, TOML export/import, accessibility (UIA) assessment.
- **Cleanup (cross-cutting).** Delete retired hosts; update CLAUDE.md/memory.

## Later / not yet scheduled
- Window-placement persistence is folded into Phase 6 (settings).

## Done (cross-cutting)
- **ARM64 build + justfile** — `just build-arm64` cross-compiles; `build.ps1` accepts
  `-Arch x64|arm64`. Both x64 and ARM64 produce ~900 KB executables.

## Review workflow
Each phase ends at a reviewable boundary. A second Claude instance reviews the diff via
the `code-reviewer` subagent: run `.\review.ps1` (interactive) or `.\review.ps1 -Headless`
(writes `docs/reviews/review-<stamp>.md`). Standards live in `.claude/agents/code-reviewer.md`
and CLAUDE.md.
