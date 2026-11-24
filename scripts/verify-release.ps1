# Verify staged Release payloads, PE architectures, app-local VC145 CRT, WinAppSDK self-contained DLLs,
# per-arch MSIs, and the cross-arch bundle executable.
$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path "$PSScriptRoot\..").Path
$versionHeader = Get-Content (Join-Path $repoRoot "version.h") -Raw
if ($versionHeader -notmatch 'ENVIRON_VERSION_STRING\s+"([^"]+)"') {
    throw "Could not parse ENVIRON_VERSION_STRING from version.h"
}
$ver = $Matches[1]

function Get-PeMachine([string]$exePath) {
    $fs = [IO.File]::OpenRead($exePath)
    try {
        $br = New-Object IO.BinaryReader($fs)
        $fs.Seek(0x3C, [IO.SeekOrigin]::Begin) | Out-Null
        $peOffset = $br.ReadInt32()
        $fs.Seek($peOffset + 4, [IO.SeekOrigin]::Begin) | Out-Null
        return $br.ReadUInt16()
    }
    finally {
        $fs.Dispose()
    }
}

$archChecks = @(
    @{ Platform = "x64";   ExpectedMachine = 0x8664; Msi = "setup\environ $ver x64.msi" },
    @{ Platform = "ARM64"; ExpectedMachine = 0xAA64; Msi = "setup\environ $ver ARM64.msi" }
)

$failures = 0

foreach ($check in $archChecks) {
    $plat = $check.Platform
    $stageDir = Join-Path $repoRoot "dist\stage\$plat"
    $exe = Join-Path $stageDir "environ.exe"

    Write-Host "[verify] Checking $plat stage: $stageDir"
    if (-not (Test-Path $exe)) {
        Write-Error "Missing $exe"
        $failures++
        continue
    }

    $machine = Get-PeMachine $exe
    if ($machine -ne $check.ExpectedMachine) {
        Write-Error "$exe PE machine is 0x$($machine.ToString('X4')), expected 0x$($check.ExpectedMachine.ToString('X4'))"
        $failures++
    }

    $requiredFiles = @(
        "knowledge.toml",
        "vcruntime140.dll",
        "msvcp140.dll",
        "Microsoft.WindowsAppRuntime.dll",
        "Microsoft.ui.xaml.dll"
    )
    if ($plat -eq "x64") {
        $requiredFiles += "vcruntime140_1.dll"
    }

    foreach ($rf in $requiredFiles) {
        $p = Join-Path $stageDir $rf
        if (-not (Test-Path $p)) {
            Write-Error "Missing required payload file for ${plat}: $rf"
            $failures++
        }
    }

    $themeCount = (Get-ChildItem (Join-Path $stageDir "themes\*.yaml") -ErrorAction SilentlyContinue | Measure-Object).Count
    if ($themeCount -lt 5) {
        Write-Error "Expected >= 5 themes in $stageDir\themes, found $themeCount"
        $failures++
    }

    $msiPath = Join-Path $repoRoot $check.Msi
    if (-not (Test-Path $msiPath) -or (Get-Item $msiPath).Length -lt 1MB) {
        Write-Error "Missing or undersized MSI: $msiPath"
        $failures++
    }
    else {
        $mb = [Math]::Round((Get-Item $msiPath).Length / 1MB, 2)
        Write-Host "[verify] OK: $($check.Msi) ($mb MB)"
    }
}

$bundlePath = Join-Path $repoRoot "setup\environ-$ver.exe"
if (-not (Test-Path $bundlePath) -or (Get-Item $bundlePath).Length -lt 1MB) {
    Write-Error "Missing or undersized bundle: $bundlePath"
    $failures++
}
else {
    $mb = [Math]::Round((Get-Item $bundlePath).Length / 1MB, 2)
    Write-Host "[verify] OK: setup\environ-$ver.exe ($mb MB)"
}

if ($failures -gt 0) {
    throw "Release verification failed with $failures error(s)."
}

Write-Host "[verify] All release checks passed!"
