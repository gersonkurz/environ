# Build automation for environ — Windows environment variable editor
# Pure Win32 + Direct2D + DirectWrite, C++20, MSVC
# Requires: just, msbuild on PATH (VS Developer Command Prompt); MSIS 3.x for packaging.

set windows-shell := ["cmd.exe", "/c"]

project := "environ.vcxproj"

# Detect native platform accurately even under x64 emulation on ARM64.
platform := `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\native-platform.ps1`
platform_suffix := if platform == "ARM64" { "arm64" } else { "x64" }

# Default: show available recipes
default:
    @just --list

# Fail early with a clear message when not run from a VS 2026 Developer shell.
# A Developer Command Prompt / Developer PowerShell sets VisualStudioVersion and
# puts msbuild on PATH; without it the build fails cryptically.
[private]
_require-devshell:
    @if not "%VisualStudioVersion%"=="18.0" (echo. & echo ERROR: This needs a Visual Studio 2026 Developer shell. & echo Open "Developer Command Prompt for VS 2026" or "Developer PowerShell for VS 2026", then retry. & echo Expected VisualStudioVersion=18.0 but found "%VisualStudioVersion%". & exit /b 1)

# Restore NuGet packages (WinAppSDK 1.8, CppWinRT, SDK BuildTools)
restore:
    nuget restore packages.config -PackagesDirectory packages

# Run MSBuild for one configuration/platform.
[private]
_msbuild configuration platform: _require-devshell
    msbuild {{project}} /p:Configuration={{configuration}} /p:Platform={{platform}} /m:2 /nr:false /nologo /v:minimal

# Build and run headless core tests (/W4 /WX)
test: _require-devshell
    powershell -NoProfile -ExecutionPolicy Bypass -File scripts\run-tests.ps1

# Build Debug (native arch)
build: (_msbuild "Debug" platform)

# Build Release (native arch) — compile only, no installer
build-release: (_msbuild "Release" platform)

# Build all (Debug+Release, x64+ARM64)
build-all: (_msbuild "Debug" "x64") (_msbuild "Release" "x64") (_msbuild "Debug" "ARM64") (_msbuild "Release" "ARM64")

# Build Debug + launch (native arch)
run: build
    @bin\{{platform}}\Debug\environ.exe

# --- Packaging (MSIS 3.x) ---

# Verify staged payloads, PE headers, VC145 CRT, MSIs, and bundle.
verify:
    powershell -NoProfile -ExecutionPolicy Bypass -File scripts\verify-release.ps1

# Build the release installers: both MSIs plus the cross-arch bundle executable, then verify.
release: package-all bundle verify

# Build the MSI for the native arch.
package: (_stage platform "Release") (_stage_symbols platform "Release") (_package "environ-" + platform_suffix + ".msis")

# Build the x64 MSI.
package-x64: (_stage "x64" "Release") (_stage_symbols "x64" "Release") (_package "environ-x64.msis")

# Build the ARM64 MSI.
package-arm64: (_stage "ARM64" "Release") (_stage_symbols "ARM64" "Release") (_package "environ-arm64.msis")

# Build both per-architecture MSIs.
package-all: package-x64 package-arm64

# Build the multi-architecture bundle executable (requires both MSIs built first).
bundle:
    msis /BUILD setup\setup-bundle.msis

# Stage the Release payload (environ.exe + themes\ + knowledge.toml) for one platform.
[private]
_stage platform configuration: (_msbuild configuration platform)
    @if exist dist\stage\{{platform}} rmdir /s /q dist\stage\{{platform}}
    @mkdir dist\stage\{{platform}}
    @robocopy bin\{{platform}}\{{configuration}} dist\stage\{{platform}} /E /XF *.pdb *.exp *.lib *.ilk >nul & if errorlevel 8 exit /b 1

# Stage the Release debug symbols for one platform (optional installer feature).
[private]
_stage_symbols platform configuration: (_msbuild configuration platform)
    @if exist dist\symbols\{{platform}} rmdir /s /q dist\symbols\{{platform}}
    @mkdir dist\symbols\{{platform}}
    @copy /Y bin\{{platform}}\{{configuration}}\*.pdb dist\symbols\{{platform}}\ >nul

# Build one MSIS manifest from setup/.
[private]
_package script:
    @cd setup&& msis /BUILD /STANDALONE {{script}}

# Set the version (X.Y.Z) in version.h and environ.rc's VERSIONINFO.
version VERSION:
    powershell -NoProfile -ExecutionPolicy Bypass -File scripts\set-version.ps1 {{VERSION}}

# Clean all output
clean:
    if exist bin rmdir /s /q bin
    if exist temp rmdir /s /q temp
    if exist dist rmdir /s /q dist

# Clean + rebuild
rebuild:
    just clean
    just build
