# Getting started

[← Back to contents](index.md)

## Installing

Download `environ-2.0.0.exe` from the [GitHub Releases](https://github.com/gersonkurz/environ/releases)
page and run it. The bootstrapper automatically selects the native **x64** or **ARM64**
installer for your PC, bundles all required Windows App SDK and C++ runtime files (no
separate runtime downloads), optionally adds `environ` to your `PATH`, and offers to launch
the app when finished.

## The single-row title bar

Environ keeps all controls in a single 48 px Windows 11 title bar so the grid gets the
entire window body:

- **Title & role badge** (`environ 2.0.0 · Standard user` or `Administrator`)
- **Menu bar** (`File`, `Edit`, `View`, `Help`)
- **Inline toolbar**:
  - **Save** (`Ctrl+S`) — lights up with a dot (`Save ●`) whenever you have staged edits
  - **Refresh** (`F5`) — reloads variables from the Windows registry
  - **History** (`Ctrl+H`) — opens the snapshot history browser
  - **+ New ▾** — creates a new User or Machine variable
  - **Filter** (`Ctrl+F`) — live substring filter across variable names and values
- **Run as administrator** — hidden once elevated

Drag any empty space in the title bar to move the window.

## Standard user vs. Administrator

Environ starts **unelevated** by default:

- **User** variables (`HKCU\Environment`) are immediately editable.
- **Machine** variables (`HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Environment`)
  are displayed read-only so you can inspect them safely without a UAC prompt on launch.

When you need to edit a machine variable, click **Run as administrator** in the title bar
(or choose **File → Restart as Administrator**). If you have unsaved edits, Environ asks for
confirmation before relaunching elevated. Once elevated, the badge switches to
`Administrator` and machine rows unlock.
