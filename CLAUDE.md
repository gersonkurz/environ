# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Environ is a WinUI 3 desktop application for viewing and editing Windows environment variables. Written in C++/WinRT using the Windows App SDK 1.8. It reads/writes environment variables directly from the registry (User scope via HKCU, Machine scope via HKLM), supports snapshots for undo/history, and validates PATH segments.

## Build

Visual Studio C++ project using `.slnx` format (no `.sln`). Build via MSBuild:

```
msbuild Environ.vcxproj /p:Configuration=Debug /p:Platform=x64
```

Supported platforms: x64, ARM64, x86. NuGet packages are checked into `packages/` (no restore needed).

- C++ standard: C++20 (VS 18.0+) or C++17 (older)
- Platform toolset: v145 (VS 18.0+) or v143
- Min Windows version: 10.0.17763.0
- Capabilities: `runFullTrust`, `systemAIModels`
- DPI awareness: PerMonitorV2 (in `app.manifest`)
- Linker dependencies: `shell32.lib`, `advapi32.lib`

## Architecture

### IDL-First XAML Pattern

Every XAML page follows a quad-file pattern:
1. **`.idl`** — Runtime class definition (properties, methods, events)
2. **`.xaml`** — UI markup with `x:Bind` data bindings
3. **`.xaml.h`** — Includes generated `*.g.h`, declares implementation struct inheriting from `*T<>`
4. **`.xaml.cpp`** — Conditionally includes `*.g.cpp`, implements event handlers and logic

When adding a new page: create the `.idl` first, build once to generate skeleton headers under `Generated Files/`, then implement against them.

### Precompiled Header

`pch.h` includes all WinRT projection headers and external libraries. Critical: contains `#undef GetCurrentTime` to resolve the Win32 macro conflict with `Storyboard::GetCurrentTime`.

### Pages

- **MainWindow** — NavigationView shell with custom title bar (Mica backdrop). Left pane: Environment, History. Footer: Settings, About. Pages are lazy-loaded and cached. Handles window placement save/restore, theme application, and unsaved-changes-on-close dialog.
- **EnvironmentPage** — Main editor. Displays User and Machine variables as `ObservableVector<EnvVariableViewModel>`. Supports filtering, inline editing, PATH segment display/validation, duplicate detection, zoom (Ctrl+Scroll, 50-200%), and dirty tracking. Machine scope is read-only when not elevated.
- **HistoryPage** — Lists snapshots from SQLite database. Supports restoring environment to a previous snapshot state.
- **SettingsPage** — Theme selector (System/Light/Dark).
- **AboutPage** — App info and links.

### ViewModels

- **EnvVariableViewModel** — Wraps a single environment variable with `INotifyPropertyChanged`. Tracks dirty state by comparing current vs original values. Computed properties for UI presentation (scope glyphs, opacity, validation severity, description tooltips).
- **HistoryEntryViewModel** — Wraps a snapshot with timestamp, scope badge, label, and change summary.

### Core Library (`core/`)

All business logic lives in `Environ::core` namespace, separate from UI:

- **EnvStore** (`EnvStore.h/.cpp`) — Reads variables from registry, expands `REG_EXPAND_SZ`, validates path segments for existence, detects cross-variable duplicate path segments.
- **EnvWriter** (`EnvWriter.h/.cpp`) — Computes diffs (add/modify/delete/rename), applies changes to registry, broadcasts `WM_SETTINGCHANGE`. Machine scope requires elevation.
- **SnapshotStore** (`SnapshotStore.h/.cpp`) — SQLite database at `%LOCALAPPDATA%\environ\environ.db`. Creates/lists/loads/deletes snapshots, computes change descriptions between snapshots. Singleton via `snapshot_store()`.
- **AppSettings** (`AppSettings.h/.cpp`) — TOML-based settings at `%LOCALAPPDATA%\environ\environ.toml` using the pnq library. Sections: Window (placement) and Appearance (theme, zoom). Singleton via `app_settings()`.
- **VarDescriptions** (`VarDescriptions.h/.cpp`) — Loads environment variable descriptions from `variables.json`. Provides tooltip text for known variables. Singleton via `var_descriptions()`.

### External Libraries (Git Submodules in `Extern/`)

- **pnq** — Configuration management with TOML backend (author's own library)
- **spdlog** — Logging (logs to `%LOCALAPPDATA%\environ\environ.log`)
- **sqlite3-amalgamation** — SQLite for snapshot persistence
- **tomlplusplus** — TOML parsing (used by pnq)

WIL (Windows Implementation Library) is included via NuGet, not as a submodule.

### Key Patterns

- **Singletons**: `app_settings()`, `snapshot_store()`, `var_descriptions()` provide global access to core services.
- **Async**: Uses `winrt::fire_and_forget` and `co_await` for non-blocking UI operations.
- **Registry scopes**: User (HKCU\Environment) vs Machine (HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Environment). Machine writes require admin elevation; the app detects this at runtime via `is_elevated()`.
- **Dirty tracking**: EnvironmentPage compares current ViewModel state against originals to enable/disable Save/Discard and show modified-row indicators.
