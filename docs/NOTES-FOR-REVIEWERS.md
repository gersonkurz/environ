# Notes for reviewers

**Non-gating** intentional tradeoffs in the **current working diff**. Reviewers: treat
each item as already considered — **do not re-flag it as a new finding**. Two caveats:
1. If an item actually violates the **current phase's acceptance criteria**, it is *not*
   waived — report it.
2. You may still push back on any item you believe is genuinely wrong — say so and explain.

**Format per item:** `**[area]** what — why — deferred to / tracked where.`

Keep this file current: when a phase lands, replace the "Current diff" section with the
next phase's items; move resolved items out.

## Current diff — Phase 4A (rename variables)
- **[rename/trigger]** Double-click the **name** cell renames; double-click the value (or
  Enter) edits the value. No rename-via-keyboard yet. Segment rows have no name.
- **[rename/validation]** Renames are validated before review/write by core
  `validate_variables`: empty names, names containing `=`, and case-insensitive duplicates
  within a scope are rejected with a message and **no write happens**. `apply_document_changes`
  also re-validates defensively (refuses to write on bad names regardless of caller). (Resolved
  this diff, per Codex — these were gating data-loss bugs.)
- **[rename/font]** The name editor now uses a 14 semibold GDI font to match the displayed
  name; the value editor stays 12 normal. Two cached fonts, selected per `EditTarget::isName`.
- **[save/expandable]** A scalar edit keeps the variable's original `REG_SZ` vs
  `REG_EXPAND_SZ` kind — value-only for now.
- **[save/selection]** A successful save reloads and resets selection/scroll to the top.
- **[review/modal]** The review modal doesn't trap the title bar (window still movable);
  the change list clips past ~half height (no inner scroll yet).

## Carried (Phase 2/3, still true, not in this diff)
- `Ctrl+S` only when not mid-edit; preview/apply double-diff; single-click-to-edit deferred;
  `edit.border` unused (borderless editor); review-modal scroll/title-bar notes above.

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
