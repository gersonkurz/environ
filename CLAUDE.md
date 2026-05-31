# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is
A Windows 11 environment-variable editor — a modern replacement for the built-in dialog.
**Pure Win32 + Direct2D + DirectWrite, C++20, MSVC.** No WinRT, no C#, no .NET, no vcpkg.
Our own code is exception-free. Goal: one small self-contained exe (~300 KB), instant startup.

See `docs/ROADMAP.md` for direction and phases, and the active `docs/PHASE-*.md` for the
current slice (Phase 3 — apply/save — as of this writing).

## Direction — READ THIS FIRST
The UI host was **rewritten from WinUI to pure Win32 + Direct2D/DirectWrite** (2026-05-30),
after two C++/WinUI attempts failed in two different WinUI subsystems (input routing, then
deployment). The Win32 host has no runtime to deploy, no manifest/PRI/version-matrix, and
starts instantly.

- **`src/win32/`** — the host. Window + message loop, the grid control, `theme.*`.
  **This is where UI work goes.**
- **`src/core/`** — plain C++20 logic (`Environ::core`). No Win32/UI, no WinRT. **Stays.**
  Reused unchanged by the new host.
- **Retired — do NOT extend, kept on disk until the pivot fully lands then deleted:**
  `scratch/EnvironNativeBaseline/` (WinUI baseline) and `src/ui/` (old handcrafted host).
  The old top-level CMake/MSBuild build targets the retired WinUI baseline; ignore it for
  host work. `codex-readme.md` is WinUI-era history only.

## Build & run
```
.\build.ps1            # self-bootstrapping: locates VS18 + enters its dev env, no dev prompt needed
.\build.ps1 -Run       # build + launch
.\build.ps1 -Clean     # wipe build-win32/ first
```
- Output: `build-win32\environ.exe` (with `theme.toml` copied beside it; the app loads it
  from its own directory, falling back to built-in dark/light/blue if absent).
- `extern/` deps are git submodules — `git submodule update --init --recursive` first.
- No automated test suite. **The gate is: builds clean at `/W4 /WX`, zero warnings.**

## Architecture
### Host (`src/win32/`)
- Pure Win32 + D2D/DWrite. Paint-on-change, **no animation** (deliberate — favors speed).
- **Theme table** (`theme.h`/`theme.cpp`/`theme.toml`): a `ColorScheme` of named per-state
  `Style`s; every painter reads from it. Inspired by ProAKT's `DataSet`/`ColorScheme`.
  **No hardcoded color literals in painting code.** New states go in both the built-in
  fallback (`theme.cpp`) and `theme.toml`.
- Custom-drawn everything, except the transient inline cell editor → a skinned standard
  `EDIT` (borderless, `WM_CTLCOLOREDIT`).
- Direct2D device-loss must be handled (`D2DERR_RECREATE_TARGET` → discard + recreate).
  Per-monitor-v2 DPI: everything scales.

### Core modules (`src/core/`, namespace `Environ::core`)
- `EnvStore` — `read_variables(Scope)`, `expand_and_validate`, `detect_duplicates`,
  `is_elevated`. The `EnvVariable` struct carries segments, kind, `segment_valid`,
  `segment_duplicate`, expanded values.
- `EnvWriter` — `compute_diff`, `apply_changes`, broadcasts `WM_SETTINGCHANGE`
  (direct Win32 `RegSetValueExW`/`RegDeleteValueW`, not pnq::regis3).
- `SnapshotStore` — SQLite history at `%LOCALAPPDATA%\environ\environ.db`.
- `AppSettings` — theme, persisted window placement.
- `EnvExport`, `KnowledgeBase` — present but not yet wired into the host; verify before use.

### Boundaries & known facts
- **Two-layer rule:** no business logic in `src/win32/`; no Win32/UI/WinRT types in
  `src/core/`. If you're writing path validation in window code, you're doing it wrong.
- Namespace is `Environ::core`, NOT `environ::core` — MSVC's CRT defines `environ` as a macro.
- pnq uses `std::string` (UTF-8); the core boundary uses `std::wstring`; convert with
  `pnq::unicode::to_utf16()`.
- App launches **unelevated**; `HKCU` editable, `HKLM` read-only until a "Restart as
  Administrator" relaunch. Anything touching `HKLM` must check elevation and surface failure
  visibly — never silently fail or succeed.
- **Exceptions:** our own code is exception-free (HRESULT / return codes / `std::optional`).
  `/EHsc` is enabled only because toml++ and the STL need it to compile under `/WX`; toml++
  runs in `TOML_EXCEPTIONS=0` (no-throw) mode. Do not introduce `throw`/`catch` in our code.

## Code style
- C++20: concepts, ranges, `std::format`, structured bindings where they clarify — not to show off.
- Uniform initializers everywhere: `int x{42}`, not `int x = 42` and not `int x(42)`.
- No raw owning pointers — `std::unique_ptr` (pimpl `unique_ptr<Impl>` needs its dtor in the
  `.cpp`). COM objects RAII-owned or explicitly released.
- Use `gersonkurz/pnq` where it covers the need; don't reimplement what's there.
- spdlog for all logging. No `OutputDebugString`, `printf`, or `std::cout` in production paths.
- If unsure something is correct, leave `// TODO(review): <specific question>`.

## What "done" means
- Compiles clean at `/W4 /WX`, zero warnings. A suppressed warning is explained in a comment
  at the suppression site, in the same commit.
- Follows the two-layer rule and the theme-table discipline.
- Verified in light, dark, and blue schemes; correct under per-monitor DPI scaling.
- Doesn't crash when run unelevated and the user touches a machine variable.
- Stays within the current phase's scope (`docs/PHASE-*.md`).

## If the build breaks, check in this order
1. Does `.\build.ps1` locate VS18? (`vswhere` → `Enter-VsDevShell`; the script re-enters the
   dev env every run because tool shells don't persist environment.)
2. Are the `extern/` submodules present (toml++, pnq, spdlog, sqlite3)?
3. Compiler/linker errors in `src/win32/` or `src/core/` — read them; don't guess.

## Review workflow
Each phase ends at a reviewable boundary. Run `.\review.ps1` to spin up a second Claude
instance as the `code-reviewer` subagent (`.claude/agents/code-reviewer.md`); `-Headless`
writes `docs/reviews/review-<stamp>.md`. Address findings before the next phase.

## Do not
- Add dependencies without asking — the dependency list is deliberately minimal.
- Introduce vcpkg. Not negotiable.
- Resume work in the retired hosts (`scratch/EnvironNativeBaseline/`, `src/ui/`).
- Hardcode colors/fonts in painting code — go through the theme table.
- Add "Generated by Claude" / `Co-Authored-By` / any AI attribution to commits. Commit
  messages are plain English: what changed and why, nothing else.
