# Environ

> *L'éditeur des variables d'environnement. For English speakers, mostly.*

A modern environment variable editor for Windows 11.  
**License:** MIT  
**Repository:** https://github.com/[username]/environ

---

## Vision

Replace the built-in Windows environment variable dialog entirely. Everything it does, done properly, with a modern Windows 11 UI that looks and feels like a first-party app.

Spiritual successor to RapidEE, reimagined for 2026.

---

## Technology Stack

| Concern | Choice |
|---|---|
| Language | C++20 |
| UI Framework | WinUI 3 via Windows App SDK |
| UI Language | C++/WinRT + XAML |
| Build System | CMake |
| Logging | spdlog (header-only, git submodule) |
| Utilities | Author's personal tools library (header-only, git submodule) |
| Dependencies | Windows App SDK via NuGet (only external package manager dependency) |
| Target OS | Windows 11 only |
| Distribution | Not a Store app — regular Win32 executable |

No vcpkg. No C#. No .NET.

---

## Architecture

The app is cleanly split into two layers:

**Business logic layer** — plain C++20, no WinRT. Handles:
- Registry read/write (`HKCU` and `HKLM`)
- Path validation (existence checks, expansion, duplicate detection)
- `.REG` export (via author's tools library)
- Broadcasting `WM_SETTINGCHANGE` on save
- Elevation boundary management

**UI layer** — C++/WinRT + XAML + WinUI 3. Handles:
- All visual presentation
- NavigationView shell
- Data binding to business logic results
- Theme management

The boundary between layers is plain C++ function calls and standard types (e.g. `std::vector<std::wstring>`). WinRT weirdness stays contained in the UI layer.

---

## Elevation Model

- App launches **unelevated** by default
- **User variables** (`HKCU`) are fully editable without elevation
- **Machine variables** (`HKLM`) are shown but read-only when unelevated
- A **"Relaunch as Administrator"** button triggers UAC elevation properly
- Machine variable panel is visually grayed out when not elevated

---

## UI Design

### Shell
- **NavigationView** (left rail, collapsible) — no traditional menu bar
- Follows system Light/Dark theme automatically
- Fluent Design: Mica material, rounded corners, system accent colors
- Nav items: **Environment** (main view) · **Settings** · **About** (bottom)

### Main View — Environment
Two panels: **User Variables** (top or left) and **Machine Variables** (bottom or right).

Each panel:
- List of variables with instant search/filter at top
- Visual distinction between scalar variables and PATH-type variables
- Variables that shadow a machine-level variable get a subtle left-border accent

Selecting a variable opens it for **inline editing** in a detail area — no separate dialogs.

**Scalar variables:** text field + live expansion preview beneath it  
*(e.g. "Expands to: C:\Users\Gerson\AppData\Local\...")*

**PATH-type variables** (auto-detected heuristically, or manually tagged):
- Reorderable list, one entry per row
- Drag to reorder
- Delete button per row
- Add new entry at bottom
- Per-entry status icon (see Color Coding below)

### Color Coding

| Color | Meaning |
|---|---|
| Default text | Path exists, valid |
| 🔴 Red | Path does not exist on disk |
| 🟡 Amber | Contains `%VAR%` reference that is itself undefined |
| 🔵 Blue/accent | Duplicate — appears elsewhere in same variable, or shadows machine-level entry |
| ⚫ Muted/gray | Target exists but is a file, not a directory |

Variable list level:
- Well-known system variables (`PATH`, `TEMP`, `WINDIR`, `COMSPEC`) get a small badge/icon
- Variables shadowing a machine variable get a subtle left-border accent color

### Save / Discard
- Unsaved changes flagged visually
- **Save** writes to registry and broadcasts `WM_SETTINGCHANGE`
- **Discard** reverts all pending changes
- No auto-save

---

## Settings Screen

Single page, no tabs.

| Setting | Default | Notes |
|---|---|---|
| Theme | System | System / Light / Dark override |
| PATH-type detection | Auto | Heuristic (semicolons + filesystem paths); user can manually tag variables |
| Live path validation | On | Can disable for slow network path scenarios |
| Network path timeout | 2s | Timeout for existence checks on UNC paths |
| Broadcast on save | Immediate | vs. prompt first |

---

## MVP Scope (v1)

**In:**
- Full User variable editing (unelevated)
- Full Machine variable editing (elevated)
- PATH-type list editor with drag-to-reorder
- Color coding (invalid, duplicate, undefined reference, file-not-directory)
- Live expansion preview
- Instant search/filter
- Shadow detection (user overrides machine)
- Save / Discard with dirty state tracking
- `.REG` export
- Settings screen (theme, validation toggles)
- Proper UAC elevation handling

**Out (v2+):**
- Diff / change history
- Snapshots / profiles
- `.env` / JSON / TOML import-export
- Variable dependency graph
- Process-scope environment editing

---

## Project Structure (planned)

```
environ/
├── CMakeLists.txt
├── LICENSE
├── README.md
├── PROJECT.md
├── extern/
│   ├── spdlog/          # git submodule
│   └── pnandq/          # author's tools library, git submodule
├── src/
│   ├── core/            # plain C++20, no WinRT
│   │   ├── EnvStore.h/cpp       # registry read/write
│   │   ├── PathValidator.h/cpp  # existence, expansion, duplicate checks
│   │   └── RegExporter.h/cpp    # .REG export (wraps tools library)
│   └── ui/              # C++/WinRT + XAML
│       ├── App.xaml
│       ├── MainWindow.xaml
│       ├── EnvironmentPage.xaml
│       └── SettingsPage.xaml
└── assets/
    └── environ.ico
```

---

## Non-Goals

- Cross-platform support (Windows 11 only, proudly)
- Store submission
- Process environment editing (out of scope)
- Remote machine editing
- Any form of cloud sync
```
