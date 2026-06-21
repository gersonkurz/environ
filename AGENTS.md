# Repository Guidelines

## Project Structure & Module Organization
Environ is a Windows 11 environment-variable editor built with pure Win32, Direct2D, DirectWrite, C++20, and MSVC. Main application code lives in `src/`: `src/win32/` owns the UI host, views, grid, theme selection, and painting; `src/core/` owns environment-variable logic, settings, snapshots, import/export, and knowledge-base behavior. Keep business logic out of `src/win32/`, and keep Win32/UI types out of `src/core/`.

Assets and configuration live at the repository root and supporting folders: `themes/` contains Base16 YAML theme files, `knowledge.toml` ships variable metadata, `docs/` holds project planning, `setup/` contains installer scripts, and `extern/` contains vendored/submodule dependencies.

## Build, Test, and Development Commands
Use a Visual Studio Developer Command Prompt or another shell where `msbuild` is on `PATH`.

- `just build` builds Debug for the native architecture.
- `just release` builds Release for the native architecture.
- `just build-all` builds Debug and Release for x64 and ARM64.
- `just run` builds Debug and launches `bin\<platform>\Debug\environ.exe`.
- `just clean` removes `bin/` and `temp/`.

Direct build example: `msbuild environ.vcxproj /p:Configuration=Debug /p:Platform=x64 /m`.

## Coding Style & Naming Conventions
Use modern C++20 where it clarifies the code. Prefer uniform initialization (`int count{0}`), RAII ownership, `std::unique_ptr` for owning pointers, and explicit lifetime management for COM resources. Project code should remain exception-free; use return values, `HRESULT`, or `std::optional` instead of `throw`/`catch`. Log with `spdlog`; avoid `printf`, `std::cout`, and `OutputDebugString` in production paths.

Painting code must read colors and fonts through the theme table. Do not hardcode visual constants that belong in `theme.*`.

## Testing Guidelines
There is no automated application test suite yet. The current quality gate is a clean build at `/W4 /WX` with zero warnings. For UI changes, verify light and dark Base16 themes, DPI scaling, and unelevated behavior when machine variables are touched. Dependency tests under `extern/` belong to those libraries, not the app.

## Commit & Pull Request Guidelines
Commit history uses short, plain-English summaries such as `Add TOML export/import of environment variables`. Keep messages focused on what changed and why; do not add AI attribution.

Pull requests should describe user-visible behavior, note build commands run, link related issues or roadmap items, and include screenshots or short clips for UI changes.
