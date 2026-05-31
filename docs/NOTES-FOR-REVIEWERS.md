# Notes for reviewers

**Non-gating** intentional tradeoffs in the **current working diff**. Reviewers: treat
each item as already considered — **do not re-flag it as a new finding**. Two caveats:
1. If an item actually violates the **current phase's acceptance criteria**, it is *not*
   waived — report it.
2. You may still push back on any item you believe is genuinely wrong — say so and explain.

**Format per item:** `**[area]** what — why — deferred to / tracked where.`

Keep this file current: when a phase lands, replace the "Current diff" section with the
next phase's items; move resolved items out.

## Current diff — Phase 2 (inline editing)
- **[edit/UX]** Editing opens on double-click / Enter / Tab, not single-click — avoids
  fighting row selection. Single-click-to-edit is a deferred UX follow-up (not required by
  PHASE-2).
- **[edit/empty]** Committing an empty value is allowed — field validation is Phase 3.
- **[type/font]** The display value font (11.5) and the GDI editor font are not a
  pixel-exact match — GDI vs DirectWrite metrics differ; close enough, exact match deferred.
- **[theme]** `ColorScheme::edit.border` is intentionally unused — the editor is borderless
  by user decision; the field stays in the struct + `theme.toml` for a possible future
  focus border.
- **[edit/validation]** Edited path rows drop their invalid/duplicate flag until
  re-validation at save — out of Phase 2 scope (Phase 3).

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
