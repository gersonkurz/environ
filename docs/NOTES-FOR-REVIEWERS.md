# Notes for reviewers

**Non-gating** intentional tradeoffs in the **current working diff**. Reviewers: treat
each item as already considered — **do not re-flag it as a new finding**. Two caveats:
1. If an item actually violates the **current phase's acceptance criteria**, it is *not*
   waived — report it.
2. You may still push back on any item you believe is genuinely wrong — say so and explain.

**Format per item:** `**[area]** what — why — deferred to / tracked where.`

Keep this file current: when a phase lands, replace the "Current diff" section with the
next phase's items; move resolved items out.

## Current diff — Phase 4B (add / remove / reorder PATH entries)
- **[entry/trigger]** Keyboard only (chosen): `Insert` adds a blank entry below the selection
  and opens its editor; `Delete` removes it; `Alt+Up`/`Alt+Down` move it (via `WM_SYSKEYDOWN`).
  Ignored on scalars, the header, and read-only machine rows. Themed context menu is Phase 8.
- **[entry/first]** Removing the first entry (on the variable row, which also holds the name)
  promotes the next entry into it; removing the last leaves an empty value (on reload such a
  variable reclassifies as a scalar — fine).
- **[entry/reserialize]** `CurrentVars` rebuilds each variable from its **contiguous block of
  rows in display order** (the ops preserve block contiguity). Structurally-edited path-lists
  get a clean `join_segments` (original empty/trailing structure intentionally dropped — the
  user restructured); in-place-only path-lists still use `apply_segment_edits`; untouched ones
  keep their exact value.
- **[entry/dirty]** A structurally-edited variable marks *all* its rows amber (even rows whose
  text didn't change, e.g. after a reorder or a blank insert) — a deliberate "this var changed"
  signal. Tracked by per-variable `m_userStruct`/`m_machineStruct`, reset on reload.
- **[entry/empty-seg]** An added-but-unedited entry is an empty path segment; `validate_variables`
  only rejects empty/`=`/duplicate variable **names**, not empty path entries, so it's written
  as-is. Intentional (you can have a trailing entry); not a name-validation case.
- **[entry/scalar]** A scalar variable can't gain entries (Insert no-ops on scalars) — turning a
  scalar into a path-list isn't supported yet.

## Carried (Phase 2/3/4A, still true, not in this diff)
- Rename: double-click the name cell; validated (empty/`=`/duplicate) before any write, in host
  and defensively in `apply_document_changes`. `Ctrl+S` only when not mid-edit; preview/apply
  double-diff; scalar edits keep original `REG_SZ`/`REG_EXPAND_SZ`; save resets selection/scroll;
  single-click-to-edit deferred; review modal doesn't trap the title bar / list clips past ~half.

## Carried over from Phase 2 (still true, not in this diff)
- Single-click-to-edit is deferred (edit opens on double-click / Enter / Tab).
- Committing an empty value is allowed (no field validation).
- `ColorScheme::edit.border` is intentionally unused (borderless editor by user decision).
- Display value font (11.5) vs the GDI editor font is not a pixel-exact match.

## Standing (every diff)
- Uniform initialization (`int x{42}`) is the rule and is applied to new and hot-path code;
  some pre-existing `=` value-inits remain in older host code and are converted
  incrementally. Flag uniform-init on **newly added** lines, not as a blanket sweep of
  untouched code. (`auto x = ...` correctly stays `=`.)
- `/EHsc` is enabled because toml++ and the STL need it under `/WX`; toml++ runs in
  `TOML_EXCEPTIONS=0` (no-throw) mode and our own code stays throw-free. Not a finding.
- Do **not** suggest using `pnq`'s `text_file`/file helpers in the Win32 host: they
  transitively include toml++ in exceptions mode, breaking the `TOML_EXCEPTIONS=0` setup.
  `theme.cpp` reads files via a small `CreateFileW` helper on purpose.
- Build artifacts (`build-win32/`, MSBuild intermediates) and `docs/reviews/` are
  gitignored by design.
