# Notes for reviewers

**Non-gating** intentional tradeoffs in the **current working diff**. Reviewers: treat
each item as already considered — **do not re-flag it as a new finding**. Two caveats:
1. If an item actually violates the **current phase's acceptance criteria**, it is *not*
   waived — report it.
2. You may still push back on any item you believe is genuinely wrong — say so and explain.

**Format per item:** `**[area]** what — why — deferred to / tracked where.`

Keep this file current: when a phase lands, replace the "Current diff" section with the
next phase's items; move resolved items out.

## Current diff — Phase 3A (apply / save)
- **[save/UI]** The apply confirmation is a plain `MessageBox`, not a themed in-app review
  panel — deferred to 3B.
- **[save/conflict]** No external-change conflict detection yet — apply uses the load-time
  originals as the baseline; if the registry changed underneath us we'd overwrite. 3B.
- **[save/trigger]** `Ctrl+S` only fires when not mid-edit (the focused `EDIT` swallows it) —
  commit with Enter first. Forwarding `Ctrl+S` from the editor subclass is a follow-up.
- **[save/diff]** `compute_diff` is called once for the preview and again inside
  `apply_document_changes`; kept for a clean preview/apply split (cheap, lists are small).
- **[save/pathlist]** Path-list values are re-serialized via core `join_segments()` (mirrors
  the core split), called from the grid. Only **edited** path-lists are rejoined, so
  untouched ones keep their exact original value (no spurious diffs).
- **[save/expandable]** A scalar edit keeps the variable's original `REG_SZ` vs
  `REG_EXPAND_SZ` kind — typing `%VAR%` into a plain `REG_SZ` value stays non-expanding.
  Value-only editing for 3A; auto-promoting on `%...%` is a future enhancement.
- **[save/pathlist-fidelity]** The core split drops empty/trailing path entries, so editing a
  path-list that contains them re-serializes without them (normalization the user didn't
  request). Empty PATH entries are rare; structure-preserving rejoin is planned for 3B
  write-hardening (tracked via `TODO(review)` in `grid.cpp`).
- **[save/selection]** After a successful save the grid reloads, resetting selection to the
  top and scroll to home. Acceptable for 3A; the 3B themed panel can preserve position.

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
