# Codex Resume Notes

This file is the handoff for resuming `environ` work in a fresh Codex session on another machine.

## Current Goal

The project is in the middle of replacing the original custom CMake/WinUI host with a working WinUI/XAML baseline.

Do not continue feature work on the old `src/ui/` host.
That path was the source of the unrecoverable mouse-wheel/input failures.
The working direction is the promoted baseline under `scratch/EnvironNativeBaseline` and the top-level build staging that now wraps it.

## Repository Rules

Read and follow `AGENTS.md` first.
The important constraints are:
- every touched change must build clean with:
  - `cmake -B build -S . --preset windows`
  - `cmake --build build`
- zero warnings
- no WinRT/Windows App SDK includes in `src/core`
- no business logic in `src/ui`
- plain-English commits only

## What Was Proven

### 1. The old host path is not the future

The original app under `src/ui/` had intermittent mouse-wheel/input-routing failures.
Multiple attempted fixes were tried and ruled out:
- custom title bar removal
- `NavigationView` removal
- simplified `ScrollViewer`
- simplified `ListView`
- page simplification
- framework-dependent vs self-contained deployment experiments

A stock WinUI probe worked.
A native C++ WinUI probe worked.
That isolated the problem to the old handcrafted host path, not the machine or Windows App SDK in general.

### 2. The working baseline is real

`scratch/EnvironNativeBaseline` is a working native WinUI/XAML app that already contains the migrated product work:
- Environment page
- History page
- Settings page
- merged user/machine variable list
- path-list rendering
- inline scalar editing
- inline path-entry editing
- dirty state
- Apply / Discard
- apply confirmation dialog
- snapshot creation on apply
- history browsing
- snapshot restore
- theme switching
- window placement persistence
- custom title bar

This baseline is the functional source of truth, even though the repo has not fully moved it out of `scratch/` yet.

## Current Build Shape

The top-level CMake build no longer builds the old handcrafted UI host.
Instead it stages the working baseline app into a canonical output directory.

### Canonical build commands

```powershell
cmake -B build -S . --preset windows
cmake --build build
```

### Canonical runnable output

```text
build/Debug/environ/environ.exe
```

Do not use any other exe path as the primary launch target.

## Important Build/Packaging Fixes Already Made

### 1. Baseline project MSBuild wiring was repaired

After temp-folder cleanup, `scratch/EnvironNativeBaseline/EnvironNativeBaseline.vcxproj` stopped building.
The fix was to restore the missing package imports for:
- Windows App SDK Base
- Windows App SDK Foundation
- Windows App SDK InteractiveExperiences
- Windows App SDK WinUI
- Windows App SDK aggregate package
- WebView2 targets
- CppWinRT

This was necessary so MIDL / cppwinrt could see:
- `Microsoft.UI.Xaml`
- `Microsoft.UI`
- `Microsoft.Web.WebView2.Core`

If a future cleanup breaks the baseline build again, inspect the project imports first.
Do not guess at runtime bugs until MSBuild metadata resolution is healthy.

### 2. The staged exe must not be renamed after build

There was a broken staging version where CMake copied the working baseline app and renamed:
- `EnvironNativeBaseline.exe` -> `environ.exe`

That produced a binary that failed at startup.
The fix was:
- make the baseline project emit `environ.exe` natively via `TargetName`
- stop renaming the exe during staging

If startup suddenly regresses, check this first.

## Current Files That Matter

### Active build/migration files
- `CMakeLists.txt`
- `scratch/EnvironNativeBaseline/EnvironNativeBaseline.vcxproj`
- everything under `scratch/EnvironNativeBaseline/` that defines the working baseline app

### Core logic files already in use by the baseline
- `src/core/EnvStore.*`
- `src/core/EnvWriter.*`
- `src/core/SnapshotStore.*`
- `src/core/AppSettings.*`

### Old host path
These files still exist but are not the future:
- `src/ui/EnvironmentPage.cpp`
- `src/ui/MainWindow.cpp`
- `src/ui/MainWindow.h`
- `src/ui/main.cpp`

Treat them as legacy unless/until there is an explicit cleanup/removal step.
Do not resume feature work there.

## Known UI/Product State

### Environment page
Already working in the baseline:
- merged list of user/machine variables
- row selection
- per-path-entry selection for path-like variables
- inline editing for scalar values
- inline editing for path entries
- dirty-row visuals
- page-level dirty state
- Discard
- Apply
- apply confirmation dialog
- filtering, including path-segment-aware filtering

### History page
Already working in the baseline:
- full-page history view
- snapshot list
- change summary
- restore with confirmation
- pre-restore safety snapshot
- restore goes through the core write path

### Settings page
Already working in the baseline:
- theme: System / Light / Dark
- reset saved window placement
- persisted via `src/core/AppSettings.*`

### Title bar
Custom title bar is back on the baseline and accepted.
One known limitation remains:
- system caption glyphs stay black in dark mode on at least one machine

This was investigated and not resolved cleanly.
Do not spend time on it unless explicitly asked.

## Known Output/Layout Notes

The staged app folder is:
- `build/Debug/environ/`

That folder will contain many runtime files and WinMDs.
That is expected for the current self-contained WinUI baseline packaging path.

A leftover file may still appear:
- `EnvironNativeBaseline.winmd`

That is a metadata artifact, not a second launch target.
The canonical executable remains:
- `build/Debug/environ/environ.exe`

## Commits Already Made During Migration

These are important waypoints in case you need to inspect history:
- `51885c6` Add native WinUI baseline with real environment data
- `8905c7f` Port filterable merged variable list to native baseline
- `c28e0b7` Polish baseline variable list styling
- `1949485` Improve baseline row selection behavior
- `c3f4690` Select path entries as individual rows
- `a4b7d34` Add inline editing for scalar values
- `1e04b47` Use arrow keys to move between inline editors
- `1ba1b2a` Show unsaved changes and discard drafts
- `99bd9cc` Add apply flow with confirmation dialog
- `c59b0b6` Add history page and snapshot restore to baseline
- `636b597` Save and restore baseline window placement
- `5fb054a` Add settings page to native baseline
- `6c17c48` Add custom title bar to native baseline

## Immediate Next Recommended Work

The user explicitly deprioritized shell churn and accepted the current command placement.
The missing features they called out most recently were:
- rename variables
- context menu actions such as `Copy to clipboard`

The recommendation at the time was:
1. finish build cleanup / migration first
2. then low-risk UX such as context menu / clipboard
3. only then rename variables, because rename changes identity semantics and is much riskier

Given the current state, the next sensible order is:
1. verify the canonical staged app on the new machine
2. commit the latest build-cleanup changes if not already committed
3. add context menus / clipboard on the baseline path
4. leave rename until the migrated app path is considered settled

## If Build Breaks Again

Check in this order:
1. Does `scratch/EnvironNativeBaseline/EnvironNativeBaseline.vcxproj` build directly with MSBuild?
2. Are the Windows App SDK / WebView2 imports still present in that vcxproj?
3. Is `TargetName` still `environ`?
4. Is CMake staging copying the output directory without renaming the exe?
5. Is the canonical launch target still `build/Debug/environ/environ.exe`?

Do not debug runtime behavior from the staged app until the direct baseline MSBuild succeeds cleanly.

## If You Need to Explain the Architecture Quickly

Short version:
- `src/core` is the real logic layer and stays
- the old `src/ui` host is legacy and should not be extended
- `scratch/EnvironNativeBaseline` is the working migrated UI host
- top-level CMake now stages that baseline into `build/Debug/environ`
- the repository is still in transition from old host to new host

## What To Tell The User On Resume

Start from this position:
- the migrated baseline is the working app foundation
- the top-level build now produces one canonical staged app directory
- next work should stay on the baseline path, not the old handcrafted host

