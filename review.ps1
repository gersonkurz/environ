# Spin up a second Claude instance as the code reviewer for the current changes.
# Standards live in .claude/agents/code-reviewer.md and CLAUDE.md.
#
#   .\review.ps1              open an interactive reviewer instance in a new window
#   .\review.ps1 -Headless    run unattended, write docs/reviews/review-<stamp>.md
#   .\review.ps1 -Base <ref>  diff against <ref> instead of HEAD
[CmdletBinding()]
param(
    [string]$Base = 'HEAD',
    [switch]$Headless
)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot

if (-not (Get-Command claude -ErrorAction SilentlyContinue)) {
    throw "claude CLI not found on PATH."
}

$prompt = @"
Review the environ Win32 host changes in this working tree. Diff base: $Base (run: git diff $Base). Apply your full review checklist and the rules in CLAUDE.md / docs/ROADMAP.md / the current docs/PHASE-*.md. First read docs/NOTES-FOR-REVIEWERS.md and do NOT re-flag the intentional decisions / known gaps it lists (only challenge one if it is genuinely wrong, and say why). Report findings grouped Critical / Warning / Suggestion, each with file:line and a concrete fix. Read-only: do not modify files.
"@

if ($Headless) {
    $outDir = Join-Path $root 'docs\reviews'
    New-Item -ItemType Directory -Force $outDir | Out-Null
    $outFile = Join-Path $outDir ("review-{0}.md" -f (Get-Date -Format 'yyyyMMdd-HHmmss'))
    Write-Host "Running headless review -> $outFile" -ForegroundColor Cyan
    claude -p $prompt --agent code-reviewer | Out-File -FilePath $outFile -Encoding UTF8
    Write-Host "Done." -ForegroundColor Green
    Get-Item $outFile
}
else {
    $oneLine = ($prompt -replace '\r?\n', ' ').Trim()
    $shell = if (Get-Command pwsh -ErrorAction SilentlyContinue) { 'pwsh' } else { 'powershell' }
    Write-Host "Launching reviewer instance in a new $shell window..." -ForegroundColor Cyan
    Start-Process $shell -ArgumentList '-NoExit', '-Command',
        "Set-Location '$root'; claude --agent code-reviewer `"$oneLine`""
}
