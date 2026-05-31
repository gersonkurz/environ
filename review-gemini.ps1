# Spin up a second Gemini instance as the code reviewer for the current changes.
# Standards live in .gemini/agents/code-reviewer.md and GEMINI.md.
#
#   .\review-gemini.ps1              open an interactive reviewer instance in a new window
#   .\review-gemini.ps1 -Headless    run unattended, write docs/reviews/review-gemini-<stamp>.md
#   .\review-gemini.ps1 -Base <ref>  diff against <ref> instead of HEAD
[CmdletBinding()]
param(
    [string]$Base = 'HEAD',
    [switch]$Headless
)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot

if (-not (Get-Command gemini -ErrorAction SilentlyContinue)) {
    throw "gemini CLI not found on PATH."
}

$prompt = "@code-reviewer Review the environ Win32 host changes in this working tree. Diff base: $Base. Apply your full review checklist and the rules in GEMINI.md / CLAUDE.md / docs/ROADMAP.md / the current docs/PHASE-*.md. Report findings grouped Critical / Warning / Suggestion, each with file:line and a concrete fix. Read-only: do not modify files."

if ($Headless) {
    $outDir = Join-Path $root 'docs\reviews'
    if (-not (Test-Path $outDir)) {
        New-Item -ItemType Directory -Force $outDir | Out-Null
    }
    $outFile = Join-Path $outDir ("review-gemini-{0}.md" -f (Get-Date -Format 'yyyyMMdd-HHmmss'))
    Write-Host "Running headless Gemini review -> $outFile" -ForegroundColor Cyan
    
    # Use --prompt for headless mode, --approval-mode plan for read-only research,
    # and --output-format text to get clean markdown output.
    # Suppress Node.js deprecation warnings that clutter output.
    $env:NODE_NO_WARNINGS = '1'
    gemini --prompt $prompt --approval-mode plan --output-format text | Out-File -FilePath $outFile -Encoding UTF8
    $env:NODE_NO_WARNINGS = '0'
    
    Write-Host "Done." -ForegroundColor Green
    Get-Item $outFile
}
else {
    $shell = if (Get-Command pwsh -ErrorAction SilentlyContinue) { 'pwsh' } else { 'powershell' }
    Write-Host "Launching Gemini reviewer instance in a new $shell window..." -ForegroundColor Cyan
    # Use --approval-mode auto_edit for a better interactive experience (allows it to read files automatically)
    Start-Process $shell -ArgumentList '-NoExit', '-Command',
        "Set-Location '$root'; gemini --approval-mode auto_edit `"$prompt`""
}
