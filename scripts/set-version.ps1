<#
.SYNOPSIS
    Set the environ version in both the code global (version.h) and the
    VERSIONINFO resource (environ.rc). Invoked by:  just version X.Y.Z
#>
param([Parameter(Mandatory = $true)][string]$Version)

if ($Version -notmatch '^(\d+)\.(\d+)\.(\d+)$') {
    Write-Error "Version must be X.Y.Z (e.g. 1.2.3); got '$Version'"
    exit 1
}
$major = $Matches[1]; $minor = $Matches[2]; $patch = $Matches[3]
$root = Split-Path -Parent $PSScriptRoot   # repo root (scripts\..)

# 1) version.h — code-side global (UTF-8, no BOM, CRLF).
$header = (@(
    '#pragma once'
    ''
    '// environ version - single source of truth for code.'
    '// Bump with:  just version X.Y.Z   (also patches environ.rc VERSIONINFO).'
    "#define ENVIRON_VERSION_MAJOR $major"
    "#define ENVIRON_VERSION_MINOR $minor"
    "#define ENVIRON_VERSION_PATCH $patch"
    "#define ENVIRON_VERSION_STRING `"$Version`""
    ''
) -join "`r`n")
[System.IO.File]::WriteAllText((Join-Path $root 'version.h'), $header,
    (New-Object System.Text.UTF8Encoding($false)))

# 2) environ.rc — patch the VERSIONINFO block (UTF-16; preserve encoding).
$rcPath = Join-Path $root 'environ.rc'
$rc = Get-Content -Raw $rcPath
$rc = $rc -replace 'FILEVERSION \d+,\d+,\d+,\d+', "FILEVERSION $major,$minor,$patch,0"
$rc = $rc -replace 'PRODUCTVERSION \d+,\d+,\d+,\d+', "PRODUCTVERSION $major,$minor,$patch,0"
$rc = [regex]::Replace($rc, '(VALUE "FileVersion", ")[^"]*(")',
    { param($m) $m.Groups[1].Value + "$major.$minor.$patch.0" + $m.Groups[2].Value })
$rc = [regex]::Replace($rc, '(VALUE "ProductVersion", ")[^"]*(")',
    { param($m) $m.Groups[1].Value + $Version + $m.Groups[2].Value })
Set-Content -Path $rcPath -Value $rc -Encoding Unicode -NoNewline

# 3) Installer manifests — patch PRODUCT_VERSION (UTF-8, no BOM). The BUILD_TARGET
#    filenames and bundle MSI references derive from it via {{PRODUCT_VERSION}}.
$utf8 = New-Object System.Text.UTF8Encoding($false)
foreach ($name in @('environ-x64.msis', 'environ-arm64.msis', 'setup-bundle.msis')) {
    $msisPath = Join-Path $root (Join-Path 'setup' $name)
    if (-not (Test-Path $msisPath)) { continue }
    $msis = Get-Content -Raw $msisPath
    $msis = [regex]::Replace($msis, '(<set name="PRODUCT_VERSION" value=")[^"]*(")',
        { param($m) $m.Groups[1].Value + $Version + $m.Groups[2].Value })
    [System.IO.File]::WriteAllText($msisPath, $msis, $utf8)
}

Write-Host "environ version set to $Version (version.h + environ.rc + setup\*.msis)"
