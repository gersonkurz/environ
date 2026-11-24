# Scopes and the knowledge base

[← Back to contents](index.md)

## The four row origins

Every variable row carries a per-row icon on the left so scopes stay clear under any sort
or filter order:

| Icon | Scope / Group | Editable? | Storage |
| --- | --- | --- | --- |
| <svg width="16" height="16" viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.3" stroke-linecap="round" stroke-linejoin="round" aria-label="User"><circle cx="8" cy="5" r="2.6"/><path d="M3 13.5c.6-2.4 2.4-3.7 5-3.7s4.4 1.3 5 3.7"/></svg> | **User** | Always | `HKCU\Environment` |
| <svg width="16" height="16" viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.3" stroke-linecap="round" stroke-linejoin="round" aria-label="Machine"><rect x="2" y="2.5" width="12" height="8.5" rx="1.2"/><path d="M5.5 13.5h5M8 11v2.5"/></svg> | **Machine** | When elevated (`Administrator`) | `HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Environment` |
| <svg width="16" height="16" viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.3" stroke-linecap="round" stroke-linejoin="round" aria-label="Session"><rect x="3.5" y="7" width="9" height="6.5" rx="1.2"/><path d="M5.5 7V5a2.5 2.5 0 0 1 5 0v2"/></svg> | **Session** | Read-only | Computed by Windows at sign-in (`USERPROFILE`, `LOCALAPPDATA`, `ProgramFiles`, …) |
| <svg width="16" height="16" viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.3" stroke-linecap="round" stroke-linejoin="round" aria-label="Inherited"><rect x="2" y="2.5" width="12" height="11" rx="1.2"/><path d="M4.8 6.2 7 8 4.8 9.8M8.3 9.8h3"/></svg> | **Inherited** | Read-only | Set by the process that launched Environ (shell prompt, Developer Command Prompt, Python venv, Windows Terminal) |

### Why inherited variables disappear when you click "Run as administrator"

Elevation does not fork the current process — Windows starts a fresh elevated process whose
parent is a system service rather than your shell. Variables that existed only in your
terminal session (such as `VIRTUAL_ENV`, `VisualStudioVersion`, or `WT_SESSION`) are
genuinely absent from that elevated process, and Environ shows what is actually there.

### Shadowed variables

If a persistent User or Machine variable has the same name as a Windows-computed volatile
variable and its expanded value differs from the effective value in the running process,
Environ flags it in its tooltip as *"Not in effect — overridden by Windows"* and displays
the effective value.

## The knowledge base (`knowledge.toml`)

Environ ships with `knowledge.toml` beside `environ.exe`, containing:

- **`[variables]`** — human-readable descriptions for ~90 common Windows, cloud, and
  developer environment variables (shown in row tooltips).
- **`[notes]`** — explanations for why Windows-managed session variables are read-only.
- **`[classification]`** — overrides for `path_like`, `scalar`, `folder`, `file`, and
  `volatile` roles.

### User overrides and runtime learning

You can layer your own overrides in `%LOCALAPPDATA%\environ\knowledge.toml` without editing
the shipped file. Environ also probes unclassified values on startup and learns whether
unknown variables point at folders, files, or path lists — saving those learnings to
`%LOCALAPPDATA%\environ\knowledge.toml` so the right Browse / Reveal button appears
automatically.
