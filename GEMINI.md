# GEMINI.md — Environ

This file provides foundational mandates and context for Gemini CLI when working on the **Environ** project.

## Project Overview
**Environ** is a modern, high-performance environment variable editor for Windows 11, designed to replace the built-in system dialog. It is a spiritual successor to RapidEE, built with a focus on speed, typography, and a "first-party" Windows 11 feel.

### Architectural Pivot (2026-05-30)
The project pivoted from C++/WinUI 3 to a **pure Win32 + Direct2D + DirectWrite** host.
- **Why:** To eliminate runtime dependencies, fix deployment issues, and achieve instant startup.
- **Retired:** `scratch/EnvironNativeBaseline/` and `src/ui/`. **Do not work in these directories.**
- **Active:** `src/win32/` (Host) and `src/core/` (Logic).

## Core Architecture (Two-Layer Rule)
1. **Core Logic (`src/core/`, namespace `Environ::core`)**:
   - Plain C++20. No Win32, no UI, no WinRT headers.
   - Handles registry I/O, path validation, expansion, and duplicate detection.
   - Boundary types: `std::wstring`, `std::vector`, `std::optional`. No `winrt::hstring`.
2. **Win32 Host (`src/win32/`)**:
   - Pure Win32 API, Direct2D, DirectWrite.
   - Handles the window, message loop, custom-drawn title bar, and virtualized grid.
   - **Theme-driven:** All colors/styles must come from the `ColorScheme` (see `theme.h`) and `theme.toml`. No hardcoded color literals.

## Development Standards & Conventions
- **Language:** C++20 (MSVC).
- **Strictness:** `/W4 /WX` (Warnings as Errors) is non-negotiable. Zero warnings.
- **Initialization:** Use uniform initialization everywhere (`T x{v}`, not `T x = v`).
- **Memory Management:** No raw owning pointers. Use `std::unique_ptr` or RAII wrappers.
- **Exceptions:** Our code is exception-free. Use `HRESULT`, return codes, or `std::optional`. `/EHsc` is only for STL/third-party compatibility.
- **String Handling:** `std::wstring` for boundary and Win32 calls. Use `pnq::unicode::to_utf16()` for conversions from UTF-8.
- **Logging:** Use `spdlog` only. No `printf`, `cout`, or `OutputDebugString` in production.
- **Utilities:** Check `gersonkurz/pnq` (`extern/pnq`) before implementing generic utilities.

## Building and Running
The project uses a custom PowerShell build script that self-bootstraps the Visual Studio environment.

- **Build:** `.\build.ps1`
- **Build & Run:** `.\build.ps1 -Run`
- **Clean Build:** `.\build.ps1 -Clean`
- **Output:** `build-win32\environ.exe` (with `theme.toml` copied beside it).

*Note: The root `CMakeLists.txt` is deprecated and targets the retired WinUI baseline. Use `build.ps1` for current development.*

## Key Documentation
- `CLAUDE.md`: Critical technical constraints and "READ THIS FIRST" direction.
- `AGENTS.md`: Strict reviewer checklist and code quality mandates.
- `docs/ROADMAP.md`: Project phases (Current: Phase 1/2).
- `docs/PHASE-1.md`: Scope and details of the initial read-only spine.

## Implementation Guidelines
- **Elevation:** App starts unelevated. `HKLM` is read-only until a "Restart as Administrator" relaunch (Phase 6). Always check elevation before attempting machine-level writes.
- **DPI Awareness:** Per-monitor V2. Everything must scale correctly.
- **Custom UI:** Custom-draw everything (frameless window, grid). Only the inline cell editor (Phase 2) uses a standard `EDIT` control.
- **No Animation:** The UI should be instant and static (paint-on-change).
