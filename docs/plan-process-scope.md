# Plan — Process (read-only) variables, scope visibility & Explorer integration

Status: **proposed** (awaiting review). Author: implementation pass after the v1.0.0 cleanup.

## Problem

environ only enumerates the two **persistent** registry keys (`src/core/EnvStore.cpp`,
`registry_path`):

- `HKCU\Environment` → **User**
- `HKLM\…\Session Manager\Environment` → **Machine**

So variables like `USERPROFILE`, `LOCALAPPDATA`, `APPDATA`, `HOMEPATH`, `USERNAME` never
appear — they aren't in those keys. And because `PATH` exists in *both* User and Machine,
there is no clear visual way to tell the two `PATH` rows apart, especially when elevated
(both are editable, so the read-only graying that distinguishes them when unelevated is gone).

## Windows mechanics (the "why")

At sign-in Windows builds the process environment by merging, in order:

1. `HKLM\…\Session Manager\Environment` (machine)
2. `HKCU\Environment` (user)
3. **`HKCU\Volatile Environment`** — overlaid **last**, and **regenerated from scratch every
   logon**.

The "missing" variables fall into two buckets, **neither editable as an environment variable**:

- **Volatile** (`USERPROFILE`, `LOCALAPPDATA`, `APPDATA`, `HOMEPATH`, `HOMEDRIVE`, `USERNAME`,
  `USERDOMAIN`, `LOGONSERVER`, …): overlaid last, so any `HKCU`/`HKLM` override is ignored,
  and rewritten at next logon. Editing them is futile.
- **System-computed** (`ProgramFiles`, `ProgramFiles(x86)`, `SystemRoot`, `SystemDrive`,
  `ProgramData`, `PUBLIC`, `CommonProgramFiles`, …): mirror OS configuration held in *other*
  keys (`…\CurrentVersion`, `…\ProfileList`). Changing them means relocating system folders —
  OS surgery, not an env-var edit, and out of scope.

`USERPROFILE` *can* be changed (profile relocation), but not here — environ should **explain**
that rather than silently grey it out.

## Goals

1. **Show** the **process-env extras** — the variables in effect that aren't editable here —
   as a **read-only third category**, and surface when a persistent row is **shadowed** by them.
2. Make **scope unambiguous** visually — **without** using color.
3. Let users **Open in Explorer** for read-only path variables (and path values generally).
4. **Explain** non-editability via knowledge-base notes.

## Non-goals

- Editing volatile or system-computed variables (profile / Program Files relocation).
- Encoding scope in **color** (see rationale below).

## Decisions already made (with the product owner)

- **Source of the third category ("Process"):** names from the **process environment**
  (`GetEnvironmentStringsW`) that are **not already an *effective* User/Machine row** — i.e.
  "process-env extras" (`USERPROFILE`, `LOCALAPPDATA`, `ProgramFiles`, …), **not** a literal
  full dump. A persistent name is excluded when its persistent value *equals* the process value;
  if they differ it is *shadowed* **only when shadow-eligible** (KB-volatile, non-composed,
  expanded-compare — see §1 case 3); otherwise it stays a normal persistent row (§1 case 4).
- **Read-only:** the category is strictly read-only; no save path.
- **Scope stays binary in core** (`Scope { User, Machine }`); Process is a read-only *display*
  category read via a dedicated function, so it never enters the registry/writer paths — see
  the scope-modeling audit below (resolves the binary-ternary warning).
- **No color for scope.** The grid already uses color for *state* (dirty / invalid / duplicate /
  selected / hover / read-only). Scope is orthogonal; reusing the color channel collides.
  Scope is shown via **section headers + per-row icons** instead.

## Design

### 1. Process scope (read-only) — core

**Scope modeling.** Keep `enum class Scope` **binary** (`{ User, Machine }`). Process is a
read-only *display* category read via a dedicated function — **not** a third `Scope` value — so
the existing binary `scope == User ? … : …` sites stay correct (Process never reaches them).

- New core call, e.g. `std::vector<EnvVariable> read_process_extras()`: enumerate
  `GetEnvironmentStringsW`, read the two persistent keys (names **and** values), then per
  process-env name:
  1. **Not persistent** → include as a read-only Process variable.
  2. **Persistent, persistent value == process value** → exclude (the editable User/Machine row
     already shows the effective value).
  3. **Persistent, but shadowed** → flagged only under **narrow, KB-driven** conditions. A raw
     value-difference is far too broad (Critical-1, round 2), so mark a persistent row *shadowed*
     only when **all** hold:
     - the name is in a **KB-curated "volatile / system-computed" set** (`USERPROFILE`,
       `LOCALAPPDATA`, `APPDATA`, `HOMEPATH`, …) — arbitrary names are never auto-shadowed;
     - it is **not a merged/composed variable** — `Path`, `PSModulePath`, … are explicitly
       excluded, because process `Path` is the *concatenation* of Machine+User and legitimately
       differs from either row without anything being shadowed;
     - the **expanded** persistent value (not raw `%SystemRoot%…`) differs from the process value
       — so `REG_EXPAND_SZ` raw-vs-expanded and User-masking-Machine are not false positives.

     When flagged, tag the persistent row *shadowed / not in effect* and carry the effective value
     for its note (recommend annotating the persistent row over duplicating it into Process).
  4. **Persistent, differs, but NOT shadow-eligible** (a composed var like `Path`, a User-masking-
     Machine pair, or a raw-vs-expanded `REG_EXPAND_SZ` difference only) → **do not shadow** and
     **do not** add a Process row; it remains a normal persistent row. This is the common
     fall-through — most value differences land here, not in case 3.

**Shadowed-row edit behavior & warning timing.** A shadowed persistent value is a *real* registry
value the user may want to delete, so the row stays **editable**, but never silently (and not
fully read-only, which would block cleanup). The warning surfaces at three moments:
- **Passive:** while the shadowed row is selected, the detail strip shows "not in effect —
  overridden by Windows at sign-in; effective value: `…`" (always visible, no action needed).
- **At edit start:** beginning an inline edit reiterates "changes won't affect the effective value".
- **In the Apply / dry-run review:** the change line for a shadowed var is annotated the same way,
  so the user sees it again before committing. Save then writes the persistent value as normal.

- The comparison/flagging is business logic → **core** (two-layer rule), a small post-pass over
  (user, machine, process). It needs the KB volatile set (new `[classification]` list, e.g.
  `volatile = [...]`) and `expand_and_validate` run on the **persistent** rows first.
- Shadow state needs a `shadowed` flag (+ effective value) on `EnvVariable` and `Row`.
- Process values are already-expanded literals: `is_expandable = false`; `kind` via
  `classify_variable`. `detect_duplicates` is **not** run against Process.

### 2. Grid section headers, read-only rows & capability split — host

- `Grid::SetData` gains the Process list; render three groups with **header rows** (`User
  variables`, `Machine variables`, `Process · read-only`), reusing the `ThemeSelectionView`
  grouping/header pattern (non-selectable headers).
- **Host model change (required).** `Grid::Row` currently carries a *required*
  `Environ::core::Scope scope`, used for `OriginalVars`/struct/diff. Process rows have **no**
  registry scope, so add a separate **display grouping** — e.g. `enum class RowGroup { User,
  Machine, Process }` (or a `bool process` alongside the existing scope) used only for section
  headers + glyphs. `Row::scope` (registry) stays meaningful **only** for User/Machine rows;
  Process rows must never reach `OriginalVars`/`CurrentVars`/diff/apply.
- Process rows reuse `Row.readOnly` (already used for machine-when-unelevated): no inline edit,
  no browse-to-change, no save. Read-only **unconditionally** for Process.
- **Capability split (Critical 2).** Path actions are currently gated on *editability*:
  `GridView::SelectedPathRole()` returns `None` when `!SelectionEditable()` (`gridview.cpp:538`),
  which would also suppress the new *Open in Explorer*. Separate the two capabilities:
  - **canEdit** — inline edit / browse-to-*change* / save. False for read-only rows.
  - **canReveal** — *Open in Explorer* / Reveal. Independent of read-only; true for any path
    value (incl. Process vars).

  So read-only path rows reject edits but still expose *Open in Explorer*. `SelectedPathRole()`
  (or a new `RevealRole()`) must not short-circuit on `!SelectionEditable()` for the reveal path.
- `CurrentVars`/diff/apply ignore the Process group entirely (never written).

### 3. Per-row scope glyphs — host

- A left-gutter Segoe Fluent Icons glyph per row, scroll-stable: person = User, PC/devices =
  Machine, lock = Process/read-only. Footer also shows the selected row's scope.
- Color continues to mean **state only**.

### 4. Open in Explorer — host

- Context-menu action: **Open in Explorer** for folder values / **Reveal in Explorer** for file
  values; available for path-valued cells and PATH segments in any scope (the motivating case is
  read-only Process vars).
- In-cell affordance: read-only path rows show an **open** glyph button where editable folder/
  file rows show the **browse** ("change") button.
- Mechanism: expand `%VARS%` first, then `ShellExecuteW(…, L"open", folder, …)` for folders; for
  files, **`SHOpenFolderAndSelectItems`** (COM/Shell — same family the app already uses; avoids
  the command-line quoting edge cases of `explorer.exe /select`). COM is already initialized.
- Eligibility: gated on **canReveal** (§2), **not** `SelectionEditable()` — so it works for
  read-only Process vars (the motivating case). Determined by KB `folder`/`file` role, a PATH
  segment, or a value that looks like a path.

### 5. Knowledge-base `[notes]` — core + data

- New `[notes]` section in `knowledge.toml`: `NAME = "free-text guidance"`.
- `KnowledgeBase::note(name)` (case-insensitive), parallel to `describe`.
- Surfaced in the detail strip; also shown if the user attempts to edit a read-only var
  (instead of silently doing nothing — honors "never silently fail or succeed").
- Seed notes for the common read-only vars (`USERPROFILE`, `LOCALAPPDATA`, …) and extend the
  `folder` classification so Explorer/folder semantics apply to them.

## Files touched (estimate)

- `src/core/EnvStore.{h,cpp}` — dedicated `read_process_extras()` + shadow post-pass + a
  `shadowed`/effective-value field on `EnvVariable`. **`Scope` stays binary — no `Scope::Process`.**
- `src/core/KnowledgeBase.{h,cpp}`, `knowledge.toml` — `[notes]` + `note()`; a `volatile`
  classification list (shadow-eligible names); seed folder/notes for the read-only vars.
- `src/win32/grid.{h,cpp}` — third group + section headers, read-only rows, scope glyphs,
  open-in-Explorer button + hit-testing.
- `src/win32/gridview.{h,cpp}` — load Process scope, context-menu + Explorer action, detail-strip note.
- `src/win32/mainwindow.cpp` — footer scope of selection; menu dispatch.

## Scope-modeling audit (resolves the binary-ternary warning)

Adding a Process category must not silently break code that assumes *non-User ⇒ Machine*.
Chosen approach: **keep `Scope` binary and never give Process rows a registry `Scope`**, so the
existing binary ternaries remain correct by construction. The audit then verifies Process rows
never flow into any scope-keyed path:

- `EnvWriter::root_key` (`EnvWriter.cpp:20`) and the diff/apply chain — only ever see User/Machine.
- `Grid::OriginalVars` (`grid.cpp:350`) and the `m_userStruct`/`m_machineStruct` selection.
- `CurrentVars`, conflict re-check (`MainWindow`), and snapshot capture.
- Grep `Scope::User`/`Scope::Machine` and confirm each site only handles persistent rows.

If review prefers a third `Scope::Process` enumerator instead, every such ternary must become an
exhaustive `switch` with explicit Process handling — more churn, more latent-bug surface. The
binary-plus-display-category approach above is recommended.

## Build order (each independently verifiable)

1. Process scope + section headers (structural; the rest builds on it).
2. Open in Explorer.
3. Per-row scope glyphs.
4. KB `[notes]`.

## Acceptance criteria

- `USERPROFILE` / `LOCALAPPDATA` appear under **Process · read-only**, not editable, no save.
- `PATH` appears under both **User** and **Machine** with clear headers + glyphs; the selected
  row's scope is obvious even when headers have scrolled away.
- **Open in Explorer** opens the folder for a read-only path var; reveals a file var.
- Attempting to edit a read-only var surfaces its KB note rather than doing nothing.
- **Shadow case:** if a KB-volatile name (e.g. `USERPROFILE`) exists persistently and its
  *expanded* value differs from the process value, the persistent row is marked *shadowed*,
  shows the effective value/note, and editing it carries the override-won't-take-effect warning.
- **Negative shadow cases:** `Path` (composed), a `REG_EXPAND_SZ` whose expansion matches, and a
  Machine var masked by a User var are **not** falsely flagged shadowed.
- Builds clean at `/W4`, zero warnings; verified light + dark; correct unelevated **and** elevated.

## Resolved by review (Codex)

Round 1:
- **Critical — capability split.** `canEdit` vs `canReveal`; read-only rows still reveal (§2).
- **Warning — scope audit.** Keep `Scope` binary; Process is a display-only category (audit §).
- **Suggestion — naming.** "process-env extras", not "full effective environment".

Round 2:
- **Critical — shadow rule too broad.** Narrowed to KB-volatile names only, **expanded**-value
  comparison, and explicit exclusion of merged/composed vars (`Path`) — §1 case 3.
- **Warning — shadowed-row edit behavior.** Defined: editable with a hard "won't take effect"
  warning (not silent, not blocked) — §1.
- **Warning — `Row.scope` is required.** Added the explicit host-model change: a `RowGroup`
  display dimension separate from registry `Scope` — §2.
- **Warning — stale `Scope::Process`.** Removed from files-touched; dedicated reader instead.

Round 3:
- **Warning — high-level rule contradicted the narrowed one.** Reworded the Decisions bullet to
  say shadow only-when-eligible.
- **Warning — missing fall-through case.** Added **case 4** (persistent, differs, not eligible →
  normal row, no shadow, no Process row).
- **Suggestion — file reveal.** Use `SHOpenFolderAndSelectItems`, not `explorer /select` (§4).
- **Suggestion — warning timing.** Specified passive (detail strip) + at edit-start + in the
  Apply/review (§1).

## Decided (owner sign-off)

- **Shadowed persistent rows: annotate the persistent row** (do *not* also duplicate the
  effective value as a Process row). This is what the design already recommends.

## Open questions (non-blocking; settle during implementation)

1. Header style: exactly mirror `ThemeSelectionView` headers, or a grid-specific treatment?
2. Glyph choices for User/Machine/Process (Segoe Fluent Icons codepoints to confirm).
3. In-cell **Open** button for read-only path rows, or context-menu only for v1?
4. Offer `Open in Explorer` for *editable* path rows too, or only read-only?
