# environ — Roadmap

## What this is
A modern Windows 11 environment-variable editor — a replacement for the cramped
built-in dialog. Native, fast, themeable, and a single small self-contained exe.

## Direction (decided 2026-05-30)
The UI host is **pure Win32 + Direct2D + DirectWrite**. We arrived here after two
C++/WinUI attempts failed in two different WinUI subsystems (input routing, then
deployment — see `docs/decisions/` history / project memory). Win32 + D2D deletes
that entire failure surface:

- one self-contained exe (~300 KB), **no runtime to deploy**, instant startup
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
.\build.ps1            # self-bootstrapping (finds VS18, no dev prompt needed)
.\build.ps1 -Run       # build + launch
.\build.ps1 -Clean     # wipe build-win32/ first
```
Output: `build-win32\environ.exe` (+ `theme.toml` copied beside it).
Gate: builds clean at `/W4 /WX`, zero warnings.

## Phases
- **Phase 0 — Foundation (done).** Build loop, D2D/DWrite proof window, theme engine
  (TOML-driven, dark/light/blue, live swap).
- **Phase 1 — Read-only spine (done).** Custom title bar + virtualized themed grid
  wired to `EnvStore`, showing real user/machine vars. No editing. See `docs/PHASE-1.md`.
- **Phase 2 — Inline editing (done).** Skinned `EDIT` cell editor, dirty tracking,
  per-segment path editing. No registry write yet. See `docs/PHASE-2.md`.
- **Phase 3 — Apply / save (current).** `EnvWriter` apply path, dry-run review,
  `WM_SETTINGCHANGE` broadcast (3A); themed review panel + conflict detection (3B).
  See `docs/PHASE-3.md`.
- **Phase 4 — Snapshots & history.** `SnapshotStore` wiring, history view, restore, diff.
- **Phase 5 — Settings & theming UI.** In-app theme switch, custom schemes, window
  placement persistence, and a **typography section in the theme table** (font
  family/size/weight join colors per CLAUDE.md — currently fonts live in code).
  UI **metrics** (caption/row height, button width) likewise move into the theme for
  Compact/Comfortable density modes.
- **Phase 6 — Elevation.** "Restart as Administrator" for `HKLM` editing; machine vars
  read-only until then.
- **Phase 7 — Polish.** Search/filter, context menu + clipboard, rename, TOML export/import,
  accessibility (UIA) assessment.
- **Cleanup (cross-cutting).** Delete retired hosts; update CLAUDE.md/memory.

## Review workflow
Each phase ends at a reviewable boundary. A second Claude instance reviews the diff via
the `code-reviewer` subagent: run `.\review.ps1` (interactive) or `.\review.ps1 -Headless`
(writes `docs/reviews/review-<stamp>.md`). Standards live in `.claude/agents/code-reviewer.md`
and CLAUDE.md.
