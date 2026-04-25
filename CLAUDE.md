# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Environ is a WinUI 3 desktop application written in C++/WinRT using the Windows App SDK 1.8. It uses NuGet packages (managed via `packages.config`, not PackageReference) and builds as an MSIX-packaged app.

## Build

This is a Visual Studio C++ project (`.vcxproj`) with no `.sln` file — it uses the newer `.slnx` format (`Environ.slnx`). Build via Visual Studio or MSBuild:

```
msbuild Environ.vcxproj /p:Configuration=Debug /p:Platform=x64
```

Supported platforms: x64, ARM64, x86. NuGet packages are checked into the `packages/` directory.

## Architecture

The project follows the standard WinUI 3 C++/WinRT pattern:

- **IDL-first approach**: Runtime classes are declared in `.idl` files (e.g., `MainWindow.idl`), which the XAML compiler uses to generate implementation headers under `Generated Files/`.
- **XAML + code-behind**: Each XAML page has a `.xaml`, `.xaml.h`, and `.xaml.cpp` triplet. The `.xaml.h` includes the generated `*.g.h` header; the `.xaml.cpp` conditionally includes `*.g.cpp`.
- **Precompiled header**: `pch.h` / `pch.cpp` — all C++/WinRT projection headers go here. Note the `#undef GetCurrentTime` workaround for the Win32 macro conflict with `Storyboard::GetCurrentTime`.
- **App entry point**: `App.xaml.cpp` — `App::OnLaunched` creates and activates `MainWindow`.

When adding a new XAML page or control:
1. Add the IDL file defining the runtime class.
2. Build once to generate skeleton headers in `Generated Files/`.
3. Implement the class using the generated skeleton as reference.

## Key Configuration

- C++ standard: C++20 (VS 18.0+) or C++17 (older)
- Platform toolset: v145 (VS 18.0+) or v143
- Min Windows version: 10.0.17763.0
- App capabilities: `runFullTrust`, `systemAIModels`
- DPI awareness: PerMonitorV2 (declared in `app.manifest`)
