# Build automation for environ — Windows environment variable editor
# Pure Win32 + Direct2D + DirectWrite, C++20, MSVC
# Requires: just (https://github.com/casey/just)
#
# Why delegate to build.ps1?
# MSVC needs Enter-VsDevShell (a PowerShell module) to put cl.exe on PATH and
# set INCLUDE/LIB. In just, each recipe line is a *separate* shell invocation,
# so env vars set on line 1 are gone by line 2. The entire VS setup + compile
# must happen in one process — which means build.ps1.

set windows-shell := ["powershell.exe", "-NoLogo", "-Command"]

# Detect native architecture
native_arch := env_var_or_default("PROCESSOR_ARCHITECTURE", "AMD64")
arch := if native_arch == "AMD64" { "x64" } else if native_arch == "arm64" { "arm64" } else if native_arch == "ARM64" { "arm64" } else { "x64" }

cpu_count := env_var_or_default("NUMBER_OF_PROCESSORS", "8")

# Default: show available recipes
default:
    @just --list

# Build for native architecture
build:
    @.\build.ps1 -Arch {{arch}}

# Build for x64
build-x64:
    @.\build.ps1 -Arch x64

# Build for ARM64
build-arm64:
    @.\build.ps1 -Arch arm64

# Build all architectures
build-all:
    @just build-x64
    @just build-arm64

# Build + launch
run:
    @.\build.ps1 -Arch {{arch}} -Run

# Clean build directories
clean:
    if (Test-Path build-x64) { Remove-Item -Recurse -Force build-x64 }
    if (Test-Path build-arm64) { Remove-Item -Recurse -Force build-arm64 }
    if (Test-Path build-win32) { Remove-Item -Recurse -Force build-win32 }

# Clean + rebuild
rebuild:
    just clean
    just build

# Show build configuration
info:
    Write-Host "environ Build Information`n=========================`nNative arch: {{arch}}`nCPU cores:   {{cpu_count}}"
