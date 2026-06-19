# Build automation for environ — Windows environment variable editor
# Pure Win32 + Direct2D + DirectWrite, C++20, MSVC
# Requires: just (https://github.com/casey/just), msbuild on PATH

set windows-shell := ["cmd.exe", "/c"]

project := "environ.vcxproj"

# Map %PROCESSOR_ARCHITECTURE% to msbuild platform: AMD64 → x64, ARM64 → ARM64
_arch := env_var_or_default("PROCESSOR_ARCHITECTURE", "AMD64")
platform := if _arch == "ARM64" { "ARM64" } else { "x64" }

# Default: show available recipes
default:
    @just --list

# Build Debug (native arch)
build:
    msbuild {{project}} /p:Configuration=Debug /p:Platform={{platform}} /m /nologo /v:minimal

# Build Release (native arch)
release:
    msbuild {{project}} /p:Configuration=Release /p:Platform={{platform}} /m /nologo /v:minimal

# Build all (Debug+Release, x64+ARM64)
build-all:
    msbuild {{project}} /p:Configuration=Debug /p:Platform=x64 /m /nologo /v:minimal
    msbuild {{project}} /p:Configuration=Release /p:Platform=x64 /m /nologo /v:minimal
    msbuild {{project}} /p:Configuration=Debug /p:Platform=ARM64 /m /nologo /v:minimal
    msbuild {{project}} /p:Configuration=Release /p:Platform=ARM64 /m /nologo /v:minimal

# Build Debug + launch (native arch)
run:
    msbuild {{project}} /p:Configuration=Debug /p:Platform={{platform}} /m /nologo /v:minimal && bin\{{platform}}\Debug\environ.exe

# Clean all output
clean:
    if exist bin rmdir /s /q bin
    if exist temp rmdir /s /q temp

# Clean + rebuild
rebuild:
    just clean
    just build
