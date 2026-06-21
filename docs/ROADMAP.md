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

## Build & run
```
just build             # build for native arch (x64 or arm64)
just build-arm64       # explicit ARM64 (cross-compile on x64 host)
just run               # build + launch
just clean             # wipe build dirs
```

## Phases
- **Phase 6 — Settings & theming UI.** In-app theme switch, custom schemes, window
  placement persistence, **Ctrl+Mousewheel zoom**, a **typography section in the theme
  table** (font family/size/weight — currently fonts live in code), UI **metrics**
  (caption/row height, button width) for Compact/Comfortable density modes, and a
  **user-configurable list of PATH-like variable names** (which variables get segment
  expansion; overlaps with `KnowledgeBase`).
- **Phase 7 — Elevation.** "Restart as Administrator" for `HKLM` editing; machine vars
  read-only until then.
- **Phase 8 — Polish.** Search/filter, TOML export/import, accessibility (UIA) assessment.

