# Self-bootstrapping build for the pure-Win32 host.
# Locates VS18 and imports its dev environment every run, so it works from any
# shell with no dev prompt required. Builds src/win32/*.cpp -> build-{arch}/environ.exe
#
#   .\build.ps1                build (native arch)
#   .\build.ps1 -Arch arm64   cross-compile for ARM64
#   .\build.ps1 -Run           build + launch
#   .\build.ps1 -Clean         wipe output first
[CmdletBinding()]
param([switch]$Run, [switch]$Clean, [string]$Arch)
$ErrorActionPreference = 'Stop'

# --- detect native and target architecture ---
$nativeArch = switch ($env:PROCESSOR_ARCHITECTURE) {
    'ARM64'  { 'arm64' }
    'AMD64'  { 'x64' }
    default  { 'x64' }
}
if (-not $Arch) { $Arch = $nativeArch }
$Arch = $Arch.ToLower()
if ($Arch -notin @('x64', 'arm64')) { throw "Unsupported architecture: $Arch (expected x64 or arm64)" }

$root = $PSScriptRoot
$srcDir = Join-Path $root 'src\win32'
$out = Join-Path $root "build-$Arch"
$exe = Join-Path $out 'environ.exe'

# --- locate VS + enter dev shell (in-process; env does not need to pre-exist) ---
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { throw "vswhere not found at $vswhere" }
$vs = & $vswhere -latest -prerelease -property installationPath
if (-not $vs) { throw 'No Visual Studio installation found' }
Import-Module (Join-Path $vs 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation `
    -DevCmdArguments "-arch=$Arch -host_arch=$nativeArch -no_logo" | Out-Null

if ($Clean -and (Test-Path $out)) { Remove-Item $out -Recurse -Force }
New-Item -ItemType Directory -Force $out | Out-Null

# theme.toml lives next to the exe; the app loads it from its own directory.
Copy-Item (Join-Path $srcDir 'theme.toml') $out -Force

# Host sources (src/win32) plus the core logic the host links against. Only the core
# files actually used are compiled — read-only Phase 1 needs just EnvStore.
$hostSources = @(Get-ChildItem $srcDir -Filter *.cpp | ForEach-Object { $_.FullName })
if (-not $hostSources) { throw "no sources in $srcDir" }
$coreSources = @('EnvStore.cpp', 'EnvWriter.cpp') | ForEach-Object { Join-Path $root "src\core\$_" }
$sources = $hostSources + $coreSources

# Our code (src/core, src/win32) is policed at /W4 /WX. Third-party headers are external:
# angle-bracket includes get warnings turned off, so /WX only polices our own code.
# toml++ + the STL want exceptions on to compile clean; we keep our own code throw-free
# and run toml++ in no-throw mode (TOML_EXCEPTIONS=0). /utf-8 is required by spdlog's fmt.
$coreInc = Join-Path $root 'src\core'
$extInc = @('tomlplusplus\include', 'pnq\include', 'spdlog\include') |
    ForEach-Object { Join-Path $root "extern\$_" }
foreach ($d in @($coreInc) + $extInc) {
    if (-not (Test-Path $d)) { throw "include dir not found: $d (git submodule update --init --recursive?)" }
}

$clArgs = @(
    '/nologo', '/W4', '/WX', '/std:c++20', '/permissive-', '/O2', '/MT', '/EHsc', '/utf-8',
    '/DUNICODE', '/D_UNICODE', '/DWIN32_LEAN_AND_MEAN', '/DNOMINMAX',
    '/I', $coreInc,
    '/external:anglebrackets', '/external:W0'
) + ($extInc | ForEach-Object { @('/external:I', $_) }) + @(
    "/Fo:$out\", "/Fe:$exe"
) + $sources + @(
    '/link', '/SUBSYSTEM:WINDOWS',
    'd2d1.lib', 'dwrite.lib', 'dwmapi.lib', 'user32.lib', 'gdi32.lib', 'ole32.lib', 'advapi32.lib', 'comctl32.lib'
)

Write-Host "Compiling $($sources.Count) file(s) for $Arch..." -ForegroundColor Cyan
& cl @clArgs
if ($LASTEXITCODE -ne 0) { throw "build failed (cl exit $LASTEXITCODE)" }

$kb = [math]::Round((Get-Item $exe).Length / 1KB)
Write-Host "BUILD OK -> $exe ($kb KB)" -ForegroundColor Green

if ($Run) { Start-Process $exe }
