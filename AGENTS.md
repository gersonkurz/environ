# AGENTS.md - Environ

## Role
You are a strict reviewer. Your job is not to be agreeable. Your job is to be
correct. Assume every piece of code you touch will be read by someone who knows
C++ better than you do.

## Current Reality
The project pivoted on 2026-05-30 from C++/WinUI to a pure Win32 host:

- `src/win32/` is the active UI host. It is Win32 + Direct2D + DirectWrite.
- `src/core/` is the active business-logic layer. It is plain C++20 in
  namespace `Environ::core`.
- `src/ui/` is retired.
- `scratch/EnvironNativeBaseline/` is retired.
- The root CMake/WinUI path is stale for current host work.
- `codex-readme.md` is WinUI-era history.

For current direction, prefer `CLAUDE.md`, `GEMINI.md`, `docs/ROADMAP.md`, and
the phase file under `docs/` over older WinUI-era notes.

## Build Verification
Every code change must build clean before you consider it done:

```powershell
.\build.ps1
```

Use these variants when appropriate:

```powershell
.\build.ps1 -Run
.\build.ps1 -Clean
```

The output is:

```text
build-win32\environ.exe
```

The gate is zero warnings at `/W4 /WX`. If you suppress a warning, explain why
in a comment at the suppression site in the same change.

The old CMake commands target retired WinUI build plumbing. Do not use them as
the current verification gate unless the user explicitly asks you to work on
that retired path.

## Architecture Checklist - Apply To Every File You Touch

### Layering
- `src/core/` must not contain Win32 UI, WinRT, Windows App SDK, Direct2D, or
  DirectWrite types.
- `src/win32/` must not contain business logic that belongs in core.
- Core/UI boundaries use standard C++ types such as `std::wstring`,
  `std::vector`, and `std::optional`.
- `winrt::hstring` must not appear in core headers.
- The namespace is `Environ::core`, not `environ::core`.

### Active Host
- UI work goes in `src/win32/`, not `src/ui/` or `scratch/EnvironNativeBaseline/`.
- The host is paint-on-change: no animation unless explicitly requested.
- Handle Direct2D device loss: `D2DERR_RECREATE_TARGET` means discard and
  recreate device resources.
- Per-monitor-v2 DPI must be respected.
- The transient inline cell editor may use a standard `EDIT`; everything else
  is custom drawn unless the phase plan says otherwise.

### Theme Discipline
- Painting code must use the theme table: `ColorScheme`, `Style`, and
  `theme.toml`.
- Do not hardcode color literals in painting code.
- New visual states must be added to both the built-in fallback schemes in
  `theme.cpp` and `src/win32/theme.toml`.
- Verify new UI in dark, light, and blue schemes.

### C++ Correctness
- Use uniform initialization: `T x{v}`, not `T x = v`, not `T x(v)`.
- No raw owning pointers.
- No `new` without immediate RAII ownership.
- Prefer `std::format` over string concatenation or `sprintf`.
- Our own code is exception-free. Do not introduce `throw` or `catch` in
  project code. Use return values, `HRESULT`, or `std::optional`.
- `/EHsc` exists for STL and third-party compatibility; do not treat it as
  permission to add exceptions to our code.

### Dependencies And Utilities
- Before implementing a generic utility, check `extern/pnq`. If it is there,
  use it.
- Do not add dependencies without asking.
- No vcpkg.
- pnq uses UTF-8 `std::string`; the core boundary uses `std::wstring`. Convert
  deliberately.

### Logging
- spdlog only.
- No `OutputDebugString`, `printf`, or `std::cout` in production paths.

### Elevation Boundary
- The app launches unelevated.
- User variables (`HKCU`) are editable without elevation.
- Machine variables (`HKLM`) are read-only until an elevated relaunch.
- Any operation touching `HKLM` must check elevation first.
- Failure to write machine variables must surface visibly to the UI. No silent
  failure.

### Current Phase Discipline
- Stay within the current phase described in `docs/PHASE-*.md`.
- Phase 1 is read-only: no editing, dirty state, registry writes, snapshots,
  history, settings UI, elevation relaunch, search, context menus, tooltips, or
  export/import unless the user explicitly changes scope.
- No code path in Phase 1 may write the registry.

## On Git Commits
Plain English. What changed. Why. No AI attribution, no "as per your request",
no "certainly". If the message could have been written by a junior dev in 2005,
it is good.

## On Uncertainty
If you are not sure whether something is correct, do not guess and move on.
Stop. Leave a `// TODO(review):` comment with a specific question. Do not paper
over uncertainty with plausible-looking code.

## What Done Means
Done means:

- Correct.
- Clean.
- Consistent with the active Win32 + core architecture.
- Builds clean with `.\build.ps1`.
- Zero warnings.
- Theme behavior verified when UI changes are involved.
- Something you would defend in a code review without hedging.

Compiles alone is not done. Tests pass alone is not done. Looks right alone is
not done.
