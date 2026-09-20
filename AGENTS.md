# Repository Guidelines

## Project Structure & Module Organization
Environ is a Windows 11 environment-variable editor built with pure Win32, Direct2D, DirectWrite, C++20, and MSVC. Main application code lives in `src/`: `src/win32/` owns the UI host, views, grid, theme selection, and painting; `src/core/` owns environment-variable logic, settings, snapshots, import/export, and knowledge-base behavior. Keep business logic out of `src/win32/`, and keep the **application's UI** out of `src/core/` — core calls Win32 system APIs freely (registry, environment, files, and the `WM_SETTINGCHANGE` broadcast in `Environ::core::broadcast_environment_change()`, a namespace-level function in `EnvWriter.cpp`), but creates no window, owns no message loop, handles no message, and names no rendering/view/theme type. The goal is that core can be exercised headless; no harness exists yet, so today this is a design requirement rather than an enforced gate.

Assets and configuration live at the repository root and supporting folders: `themes/` contains Base16 YAML theme files, `knowledge.toml` ships variable metadata, `docs/` holds project planning, `setup/` contains installer scripts, and `extern/` contains vendored/submodule dependencies.

## Working Agreement
Work is tracked as GitHub issues in `gersonkurz/environ` (`gh` is installed and logged in).
Open and assign the issue before writing code (`gh issue create --assignee gersonkurz`),
comment the decisions onto it as they are made, and close it with the commit (`Fixes #<n>`).
Two review gates, in order: Codex reviews first, then the human reviews and says commit.
Nothing is committed before the human has read it.

## Build, Test, and Development Commands
Use a Visual Studio Developer Command Prompt or another shell where `msbuild` is on `PATH`.

- `just build` builds Debug for the native architecture.
- `just build-release` builds Release for the native architecture. (`just release` is the
  installer/packaging recipe, not a compile.)
- `just build-all` builds Debug and Release for x64 and ARM64.
- `just run` builds Debug and launches `bin\<platform>\Debug\environ.exe`.
- `just clean` removes `bin/` and `temp/`.

Direct build example: `msbuild environ.vcxproj /p:Configuration=Debug /p:Platform=x64 /m`.

## Coding Style & Naming Conventions
Use modern C++20 where it clarifies the code. Prefer uniform initialization (`int count{0}`), RAII ownership, `std::unique_ptr` for owning pointers, and explicit lifetime management for COM resources. Project code should remain exception-free; use return values, `HRESULT`, or `std::optional` instead of `throw`/`catch`. Format with `fmt::format` (vendored fmtlib), not `std::format` and not spdlog's bundled copy; spdlog is built with `SPDLOG_FMT_EXTERNAL` and `SPDLOG_USE_STD_FORMAT` must not be defined. Existing code does not comply yet; that migration is issue #4.

Readability is the yardstick, and `extern/pnq` is what readable looks like in this project: one job per function, names that read as a sentence at the call site, a `///` line above anything public, Allman braces, no function that needs a scrollbar. Match a pnq header when in doubt.

Log through pnq, not `spdlog::` directly (the indirection is what allows swapping the backend): `PNQ_LOG_INFO`/`PNQ_LOG_ERROR`/... from `pnq/log.h` for ordinary messages, `pnq/logging.h` for setup and Windows-error helpers; avoid `printf`, `std::cout`, and `OutputDebugString` in production paths. A log that reports a failure without its reason is a bug — decode the HRESULT or `GetLastError()`, e.g. `pnq::logging::report_windows_error(context, hr, "D2D1CreateFactory")`. Existing code does not comply yet; that migration is issue #2.

UI and logic stay separated: a `View` paints and routes input, and everything that *decides* lives in a plain C++ object with no `HWND` in reach, so a test can get at it without a window. Not MVC — the test is reachability from a unit test, not a class diagram.

Painting code must read colors and fonts through the theme table. Do not hardcode visual constants that belong in `theme.*`.

## Testing Guidelines
There is no automated application test suite yet. The current quality gate is a clean build at `/W4` with zero warnings (`/WX` is not set — read the build output, the exit code won't tell you). For UI changes, verify light and dark Base16 themes, DPI scaling, and unelevated behavior when machine variables are touched. Dependency tests under `extern/` belong to those libraries, not the app.

## Commit & Pull Request Guidelines
Commit history uses short, plain-English summaries such as `Add TOML export/import of environment variables`. Keep messages focused on what changed and why; do not add AI attribution.

Pull requests should describe user-visible behavior, note build commands run, link related issues or roadmap items, and include screenshots or short clips for UI changes.
