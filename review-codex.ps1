# Spin up a Codex instance as the code reviewer for the current changes.
# Standards live in AGENTS.md, CLAUDE.md, GEMINI.md, and docs/.
#
#   .\review-codex.ps1              open an interactive reviewer instance in a new window
#   .\review-codex.ps1 -Headless    run unattended, write docs/reviews/review-codex-<stamp>.md
#   .\review-codex.ps1 -Base <ref>  diff against <ref> instead of HEAD
[CmdletBinding()]
param(
    [string]$Base = 'HEAD',
    [switch]$Headless
)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot

if (-not (Get-Command codex -ErrorAction SilentlyContinue)) {
    throw "codex CLI not found on PATH."
}

$prompt = @"
Review the environ Win32 host changes in this working tree. Diff base: $Base.
Include staged, unstaged, and untracked changes. Apply your full review checklist
and the rules in AGENTS.md / CLAUDE.md / GEMINI.md / docs/ROADMAP.md / the
current docs/PHASE-*.md. Report findings grouped Critical / Warning /
Suggestion, each with file:line and a concrete fix. Read-only: do not modify
files.
"@

if ($Headless) {
    $outDir = Join-Path $root 'docs\reviews'
    if (-not (Test-Path $outDir)) {
        New-Item -ItemType Directory -Force $outDir | Out-Null
    }
    $outFile = Join-Path $outDir ("review-codex-{0}.md" -f (Get-Date -Format 'yyyyMMdd-HHmmss'))
    Write-Host "Running headless Codex review -> $outFile" -ForegroundColor Cyan

    # Codex's Windows read-only sandbox currently fails intermittently with
    # "windows sandbox: spawn setup refresh" on simple PowerShell reads in this
    # repo. Run the reviewer without the Codex sandbox; the prompt remains
    # explicitly read-only, like the Claude/Gemini review wrappers.
    codex --dangerously-bypass-approvals-and-sandbox exec -o $outFile $prompt
    if ($LASTEXITCODE -ne 0) {
        Remove-Item -LiteralPath $outFile -Force -ErrorAction SilentlyContinue
        throw "Codex review failed (codex exit $LASTEXITCODE)"
    }

    Write-Host "Done." -ForegroundColor Green
    Get-Item $outFile
}
else {
    $oneLine = ($prompt -replace '\r?\n', ' ').Trim()
    $shell = if (Get-Command pwsh -ErrorAction SilentlyContinue) { 'pwsh' } else { 'powershell' }
    Write-Host "Launching Codex reviewer instance in a new $shell window..." -ForegroundColor Cyan
    Start-Process $shell -ArgumentList '-NoExit', '-Command',
        "Set-Location '$root'; codex --dangerously-bypass-approvals-and-sandbox `"$oneLine`""
}
