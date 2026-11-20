# Environ — Codebase Review & WinUI 3 (C++/WinRT) Plan

**Status:** Plan settled on branch `environ-winui3` (keeps `main` and the Win32+Direct2D `ancusi` track untouched).

---

## 1. Critical Assessment & Settled Decisions

Unlike `pserv5` (migrating off ImGui), `environ` already ships a working, feature-complete 1.2.0 native Win32 + Direct2D/DirectWrite application (~9,000 LOC, 5.5 MB ARM64 MSI, ~40 ms cold startup). Moving the host to WinUI 3 is a deliberate trade-off, not a free lunch:

| Dimension | Current Win32 + Direct2D | WinUI 3 (C++/WinRT, Self-Contained 1.8) |
| --- | --- | --- |
| **Installer size** | ~5.5 MB MSI | ~33–35 MB per-arch MSI (~68 MB x64+ARM64 bundle) |
| **Cold startup** | ~30–50 ms | ~250–400 ms (XAML Island + WinAppSDK init) |
| **UI host complexity** | ~7,050 LOC custom chrome, hit-testing, scrollbars, modals, D2D device-loss, skinned `EDIT` | ~1,800–2,500 LOC XAML + C++/WinRT; native controls, scrollbars, dialogs, menus, Mica |
| **Accessibility / IME / High Contrast** | Hand-rolled / largely absent | Built-in UIA, Narrator, IME, touch, High Contrast |
| **Build system** | Can move to CMake + Ninja | MSBuild `.vcxproj` + NuGet (`packages.config`) |

### Settled Decisions
1. **Footprint / startup trade-off:** Accepted (~34 MB per-arch MSI, ~68 MB bundle, ~300 ms cold startup).
2. **Branch strategy:** Dedicated branch `environ-winui3`, leaving `main` (and the `ancusi` roadmap) untouched.
3. **Horizontal scrolling for long `PATH` values:** True **frozen `NAME` column + value-only horizontal scrolling** using a single virtualized `ListView` (`HorizontalScrollBarVisibility="Disabled"`), a resizable `NAME` splitter, a single shared bottom `HorizontalScrollBar` under the `VALUE` column, and per-row clipped `VALUE` containers shifted via `TranslateTransform` (updated across only the ~30 materialized visible containers on `ScrollBar::ValueChanged`, `Shift+Wheel`, and `WM_MOUSEHWHEEL`). Avoids the 1-frame vertical sync lag and focus/selection bugs of split side-by-side `ListView`s.
4. **Exception boundary:** `src/core/` stays strictly exception-free (`HRESULT` / return codes / `std::optional`). `src/winui/` compiles with `/EHsc` and catches `winrt::hresult_error` at window/entry/event boundaries — not because application logic wants exceptions, but because **C++/WinRT projection headers unconditionally call `winrt::check_hresult` and throw `winrt::hresult_error` on failed WinRT ABI calls**. Exceptions never cross into `src/core/`.

---

## 2. Review of the Current `environ` Codebase

### Reusable Core (`src/core/`, ~1,988 LOC)
Already headless and free of application UI; reuse directly:
- `EnvStore.{h,cpp}` — registry reads, process extras (`session` vs `inherited`), `%VAR%` expansion & path validation, duplicate detection, shadow detection, `preserve_env_form`, `apply_segment_edits`.
- `EnvWriter.{h,cpp}` — `compute_diff`, `validate_variables`, `apply_document_changes`, `build_diff_table`, `WM_SETTINGCHANGE` broadcast.
- `SnapshotStore.{h,cpp}` — SQLite history at `%LOCALAPPDATA%\environ\environ.db`, `reconstruct_variables`.
- `KnowledgeBase.{h,cpp}` — layered shipped + user `knowledge.toml`, runtime learning of folder/file/path-list roles.
- `EnvExport.{h,cpp}` — round-trippable TOML export/import/merge.
- `AppSettings.{h,cpp}` — TOML-backed window/theme/font/zoom persistence.

### Concrete Defects & Structural Warts Found
1. **`Debug|ARM64` defines `NDEBUG` instead of `_DEBUG`** (`environ.vcxproj`, line 226).
2. **Clean-machine VC runtime deployment gap:** `setup/environ-x64.msis` and `setup/environ-arm64.msis` declare `<requires type="vcredist" version="2026"/>` while linking `/MD` (`MultiThreadedDLL`). On a clean machine without VS 2026 redistributables installed, `environ.exe` fails to start. Fix: bundle app-local `Microsoft.VC145.CRT\*.dll` in Release (`CopyReleaseCRT`) and verify via `scripts/verify-release.ps1`.
3. **Architecture detection under emulation:** `justfile` reads `%PROCESSOR_ARCHITECTURE%` alone, misreporting `x64` when `just` runs under x64 emulation on ARM64 Windows. Fix: `IsWow64Process2` via `scripts/native-platform.ps1`.
4. **Domain state trapped inside the D2D widget:** `src/win32/grid.{h,cpp}` (1,800 LOC) fuses `m_userOrig`, `m_machineOrig`, `m_processVars`, row flattening, global sorting, filtering with promoted segment names, inline edit commit, path-segment add/remove/reorder, `CurrentVars()` reconstruction, `SetDataForRestore()`, and `CopyText()` with Direct2D painting and scrollbar drag geometry. **Extract this state machine into headless `src/core/EnvGridModel.{h,cpp}` before writing WinUI views.**
5. **UI-coupled Base16 theme loader:** `src/win32/theme.{h,cpp}` embeds `D2D1_COLOR_F` and `D2D1_RECT_F` directly in `ColorScheme`/`ThemeInfo`. Extract a UI-agnostic `Base16Theme` (`Color4` RGB) into `src/core/`.
6. **Raw COM calls in shell dialogs:** `src/win32/gridview.cpp` and `src/win32/mainwindow.cpp` call `->Release()` manually on `IFileOpenDialog`/`IFileSaveDialog`/`IShellItem`. Wrap with RAII / `wil::com_ptr`.
7. **Win32 host warts eliminated automatically by WinUI 3:** `EnsureRenderTarget` half-init null-brush crash, triple-duplicated caption geometry, `WM_NCHITTEST` interception breaking the top-left resize border, 468-line `HandleMessage` switch, polling `Check*Action()` twice per loop, and `environ.manifest` referenced by `environ.vcxproj` without existing on disk.

---

## 3. Solving the Two Prior WinUI 3 Failure Modes

`CLAUDE.md` records two prior C++/WinUI failures: **deployment** and **input routing**. Both have proven solutions validated in `pserv6`.

### A. Deployment (Proven Recipe)
- **Pin Windows App SDK `1.8.260317003`** (avoids 2.7.0's ~65 MB/arch of unused AI/ML/Search/Widgets DLLs), plus `Microsoft.Windows.CppWinRT 2.0.250303.1`, `Microsoft.Windows.SDK.BuildTools 10.0.26100.4654`, and `Microsoft.Windows.SDK.BuildTools.MSIX 1.7.20250829.1`.
- **Unpackaged self-contained mode:**
  ```xml
  <AppContainerApplication>false</AppContainerApplication>
  <AppxPackage>false</AppxPackage>
  <WindowsPackageType>None</WindowsPackageType>
  <UseWinUI>true</UseWinUI>
  <EnableMsixTooling>true</EnableMsixTooling>
  ```
- **Per-platform generated files:** `<GeneratedFilesDir>$(IntDir)Generated Files\</GeneratedFilesDir>` so x64 and ARM64 WinMD/XAML outputs never collide.
- **ARM64 PCH memory budget:** Include only the ~12 needed WinRT headers in `precomp.h` (pull feature-specific headers in `.cpp` files only), cap `ProcessorNumber=4` / `CL_MPCount=4`, and invoke MSBuild with `/m:2 /nr:false`.
- **Explicit `wWinMain` (`DISABLE_XAML_GENERATED_MAIN`):** Catch exceptions around `Application::Start` and keep startup log rotation non-throwing (prevents `0xC000027B` stowed exceptions across the ABI boundary).
- **App-local VC145 CRT:** `CopyReleaseCRT` copies `$(VCInstallDir)Redist\MSVC\$(VCToolsRedistVersion)\$(CRTPlatform)\Microsoft.VC145.CRT\*.dll` into `$(OutDir)` in Release.
- **Unelevated by default:** Keep `<UACExecutionLevel>AsInvoker</UACExecutionLevel>` so `HKCU` is editable and `HKLM` stays read-only until on-demand `ShellExecuteExW(L"runas", ...)` relaunch.

### B. Input Routing (Excel-Style Inline Editing in WinUI 3)
`environ`'s core interaction is Excel-style inline cell editing (`F2`, double-click, `Tab`/`Shift+Tab`, `Ins`/`Del`/`Alt+↑/↓`, `Ctrl+F`, `Ctrl+MouseWheel`). Naive WinUI `ListView` + per-row `TextBox` breaks focus, scrolling, and key routing. The clean design:

1. **Tunneling shortcuts:** Intercept global and grid shortcuts on `RootGrid` `PreviewKeyDown` (before `ListView`/`ListViewItem` consume them) and attach `PointerWheelChanged` with `AddHandler(UIElement::PointerWheelChangedEvent(), ..., /*handledEventsToo=*/true)`.
2. **Zero permanent per-row `TextBox`es:** Normal rows render lightweight `TextBlock`s populated lazily via `ContainerContentChanging` (only ~30 visible containers exist; zero eager WinRT row/cell objects).
3. **Single reusable inline `TextBox` state machine:**
   - `F2` or double-click on an editable Name or Value cell mounts a single reusable inline `TextBox` into that cell slot, focuses it, and calls `SelectAll()`.
   - `PreviewKeyDown` on that inline `TextBox` handles:
     - `Enter` → commit to `EnvGridModel`, detach `TextBox`, restore `TextBlock`, refocus `ListViewItem`.
     - `Escape` → cancel (setting `m_bSuppressLostFocusCommit`), detach `TextBox`, refocus `ListViewItem`.
     - `Tab` / `Shift+Tab` → commit, move to Value cell or `EnvGridModel::SelectNextEditable(+1/-1)`, scroll target into view, reopen inline `TextBox` on the next cell.
   - `LostFocus` → commit unless suppressed by explicit cancel or cell handoff.
4. **Container recycle guard:** In `OnContainerContentChanging`, if `args.InRecycleQueue()` fires on the container hosting the active inline `TextBox`, commit and detach immediately before WinUI recycles the container.
5. **Read-only edit feedback:** When `F2`/double-click hits a read-only row (`Machine` unelevated, `Session`, or `Inherited`), stage `KnowledgeBase::note(varName)` prominently in the detail strip instead of silently doing nothing.

---

## 4. Target Directory Layout

```
environ\
├── src\
│   ├── core\                         # Headless C++20, ZERO WinRT/XAML, exception-free
│   │   ├── AppSettings.{h,cpp}       # + atomic save (MoveFileExW)
│   │   ├── Base16Theme.{h,cpp}       # Extracted UI-agnostic Base16 YAML loader (Color4)
│   │   ├── EnvExport.{h,cpp}         # Unchanged
│   │   ├── EnvGridModel.{h,cpp}      # Extracted from win32/grid.cpp
│   │   ├── EnvStore.{h,cpp}          # Unchanged
│   │   ├── EnvWriter.{h,cpp}         # Unchanged
│   │   ├── KnowledgeBase.{h,cpp}     # Unchanged
│   │   └── SnapshotStore.{h,cpp}     # Unchanged
│   └── winui\                        # Thin WinUI 3 (C++/WinRT) frontend
│       ├── App.xaml / .h / .cpp      # DISABLE_XAML_GENERATED_MAIN, non-throwing startup
│       ├── MainWindow.idl
│       ├── MainWindow.xaml / .h / .cpp  # TitleBar, NavigationView, Grid, inline editor, detail strip
│       ├── HistoryPanel.{h,cpp}      # Snapshot list + monospace diff + Delete/Restore
│       ├── ThemePickerPanel.{h,cpp}  # 2-col Dark/Light + 16-color Base16 swatches + live preview
│       ├── FontPickerPanel.{h,cpp}   # DirectWrite font list + live whole-window preview + A-Z jump
│       └── ShellHelpers.{h,cpp}      # RAII IFileOpenDialog/IFileSaveDialog, Reveal in Explorer, UAC runas
├── tests\
│   └── environ_tests.cpp             # Headless console tests for src/core/ + EnvGridModel
├── scripts\
│   ├── native-platform.ps1           # IsWow64Process2 host arch detection
│   ├── run-tests.ps1                 # Compiles & runs environ_tests.exe
│   └── verify-release.ps1            # Verifies staged payloads, PE headers, VC145 CRT, MSIs, bundle
└── setup\
    ├── environ-x64.msis              # App-local VC145 CRT; no vcredist 2026 tag
    ├── environ-arm64.msis
    └── setup-bundle.msis
```

### Feature-by-Feature WinUI 3 Mapping

| Feature | WinUI 3 Implementation |
| --- | --- |
| Title bar + burger + `Run as Administrator` | `ExtendsContentIntoTitleBar(true)`, custom `AppTitleBar` with nav toggle, `environ <ver>` (+ `— Administrator` badge when elevated), and interactive `Run as Administrator` button (`&#xE730;` shield, hidden when elevated) excluded from caption drag region. |
| Navigation (`Themes`, `Fonts`, `History`, `Save`, `Export`, `Import`, `RunAsAdmin`, `About`, `Exit`) | `NavigationView` (`PaneDisplayMode="LeftMinimal"` so grid keeps 100% width) + optional compact top action strip (Save with dirty indicator). |
| Search bar (`Ctrl+F`) | Persistent `TextBox` with built-in clear button driving `EnvGridModel::SetFilter` + zero-flicker `IObservableVector` in-place delta updates. |
| 2-column grid (`NAME` frozen + `VALUE` h-scroll) | Virtualized `ListView` (`SelectionMode="Extended"`, `IsMultiSelectCheckBoxEnabled="False"`, non-overlaying vertical `ScrollBar`, empty `ItemContainerTransitions`, `ItemsUpdatingScrollMode="KeepScrollOffset"`) + resizable `NAME` splitter + clipped `VALUE` cell shifted via `TranslateTransform` from a shared bottom `HorizontalScrollBar`. Scope glyphs (`&#xE77B;` user, `&#xEC76;` machine, `&#xE72E;` session, `&#xE756;` inherited), red invalid path, orange duplicate, dim read-only, dirty accent bar. |
| In-cell Browse / Reveal button | Right-aligned icon `Button` (`&#xE8B7;` folder, `&#xE8A5;` file, `&#xE8A7;` reveal) on the selected row's Value cell, wired to `ShellHelpers::BrowseForPath` (`preserve_env_form`) or `OpenInExplorer`. |
| Context menu | `MenuFlyout`: Copy (`Ctrl+C`), Browse/Reveal, Insert entry (`Ins`), Remove entry (`Del`), Move Up/Down (`Alt+↑/↓`), separator, New user variable, New machine variable (disabled when unelevated), Delete variable. |
| Detail strip & status bar | Bottom `Border` showing expanded value, missing/duplicate/shadowed warnings, `KnowledgeBase` description + read-only `[notes]`, and footer (`Theme • N user, M machine, K session, J inherited • Zoom%`). |
| Dry-run Save Review | `ContentDialog` with scrollable diff rows (`EnvWriter::compute_diff`), concurrent external-change re-check before writing, automatic snapshot creation, Cancel / Apply Changes. |
| History browser (`Ctrl+H`) | Split pane: left snapshot `ListView` (timestamp, label, Delete) + right monospace diff (`build_diff_table`), Restore loads snapshot via `EnvGridModel::SetDataForRestore` as staged dirty edits. |
| Base16 Theme Picker | 2-column Dark / Light view, each row showing theme name + 16-color swatch (`base00..base0F`), plus `System (Default)` and High Contrast support. Live-preview updates `Application::Current().Resources()` theme brushes + `DWMWA_USE_IMMERSIVE_DARK_MODE`; `Enter`/Apply persists, `Esc`/Cancel reverts. |
| Live Font Picker | `ListView` of DirectWrite system font families (`IDWriteFactory::GetSystemFontCollection`); arrow keys and `A`–`Z` type-to-jump live-update `FontFamily` across the whole window; `Enter` persists, `Esc` reverts. |
| Zoom & DPI | `Ctrl+MouseWheel` / `Ctrl++` / `Ctrl+-` scales real font sizes and row heights (not `ScaleTransform`); WinUI handles per-monitor DPI natively. |

---

## 5. Phased Execution Plan & Exit Gates

### Phase 0 — Core Extraction & Headless Tests (Zero UI Risk)
1. Extract `EnvGridModel` from `src/win32/grid.{h,cpp}` into `src/core/EnvGridModel.{h,cpp}` (zero Win32 UI / D2D types).
2. Extract UI-agnostic `Base16Theme` from `src/win32/theme.{h,cpp}` into `src/core/Base16Theme.{h,cpp}`.
3. Make `AppSettings::save()` atomic (`MoveFileExW` + `MOVEFILE_WRITE_THROUGH`) and fix `Debug|ARM64` `NDEBUG`.
4. Add `tests/environ_tests.cpp`, `scripts/run-tests.ps1`, and `just test` covering `EnvStore`, `EnvWriter`, `EnvExport`, `SnapshotStore`, `KnowledgeBase`, `Base16Theme`, and `EnvGridModel`.
- **Exit gate:** `just test` compiles cleanly at `/W4` (zero warnings) and passes 100% headless.

### Phase 1 — WinUI 3 Host Scaffold & Deployment Proof First
1. Add `packages.config` (Windows App SDK `1.8.260317003` + CppWinRT `2.0.250303.1` + SDK BuildTools/MSIX).
2. Configure `environ.vcxproj` for WinUI 3 (self-contained unpackaged, `AsInvoker`, per-platform `GeneratedFilesDir`, `CopyAssets`, `CopyReleaseCRT`, `ProcessorNumber=4`, `/m:2 /nr:false`).
3. Create minimal `App.xaml*` and `MainWindow.xaml*` shell (Mica, custom titlebar with UAC button, `NavigationView`, Base16 resource overrides, `MonitorFromRect` window restore).
4. Update `setup/environ-{x64,arm64}.msis` (remove `<requires type="vcredist" version="2026"/>`, set `REMOVE_FOLDERS_ON_UNINSTALL=False`), add `scripts/native-platform.ps1` and `scripts/verify-release.ps1`, update `justfile`.
- **Exit gate:** `just release` builds x64 + ARM64 Release + MSIs + bundle cleanly and `scripts/verify-release.ps1` passes (proving deployment works before writing full views).

### Phase 2 — Grid, Frozen-Name H-Scroll, Inline Editor & Input Routing
1. Implement virtualized `ListView` + lazy `ContainerContentChanging` rows + sticky sortable header + resizable `NAME` splitter + clipped `VALUE` `TranslateTransform` horizontal scroll.
2. Implement single reusable inline `TextBox` state machine (`F2`, double-click, `Enter`, `Escape`, `Tab`/`Shift+Tab`, `LostFocus`, recycle-commit) wired to `EnvGridModel`.
3. Wire `RootGrid` `PreviewKeyDown` (`Ctrl+F`, `Ctrl+S`, `Ctrl+H`, `Ctrl+C`, `Ins`, `Del`, `Alt+↑/↓`, `Ctrl++/-`) and `PointerWheelChanged` (`Ctrl+MouseWheel`).
4. Wire right-click `MenuFlyout`, in-cell Browse/Reveal (`ShellHelpers` with RAII COM), detail strip (`KnowledgeBase` + read-only edit notes + invalid/duplicate/shadowed warnings), and status footer.
- **Exit gate:** Every keyboard shortcut and inline-edit path verified both in `environ_tests.exe` and interactively (unelevated and elevated).

### Phase 3 — Modals, Secondary Views & Full Feature Parity
1. Dry-run Save Review `ContentDialog` (with concurrent registry check + snapshot creation).
2. History View (`Ctrl+H`: snapshot list, monospace diff, Delete, Restore).
3. Theme Picker View (2-col Dark/Light + 16-color swatches + `System (Default)` + live preview + High Contrast).
4. Font Picker View (DirectWrite enumeration + live whole-window preview + `A`–`Z` jump).
5. UAC `Run as Administrator` relaunch (with unsaved-changes guard) and TOML export/import/merge.
- **Exit gate:** 100% feature parity with the current `README.md` feature set.

### Phase 4 — Hardening & Release Verification
1. `/W4` clean build audit (zero warnings in project code) across `Debug`/`Release` × `x64`/`ARM64`.
2. `just test` green; `just release` + `scripts/verify-release.ps1` green.
3. Interactive pass across light/dark Base16 themes + `System` + High Contrast, 100%/150%/200% DPI, unelevated HKLM edit attempts, and clean-machine MSI installation.
