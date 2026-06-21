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
  (inspired by ProAKT's `DataSet`/`ColorScheme`), mapped from Base16 YAML files in a
  `themes/` directory beside the exe. No color literals in painting code.
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
- **Phase 6 — Settings & theming UI (done).** In-app theme switch, Base16 YAML custom
  schemes, window-placement persistence, Ctrl+Mousewheel zoom (with zoom-scaled columns),
  and configurable typography (font family/size via `[Appearance]` settings). The
  knowledge base (`knowledge.toml`) drives per-variable descriptions and
  path-list/folder/file classification, layered shipped + user override.
- **Phase 7 — Elevation (done).** "Run as Administrator" relaunch (hamburger nav +
  title-bar button) for `HKLM` editing; machine vars read-only until then.
- **Phase 8 — Polish (done).** Search/filter, the folder/file browse picker, TOML
  export/import, and horizontal scrolling of long values are all in.

