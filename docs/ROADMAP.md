# environ — Roadmap

## What this is
A modern Windows 11 environment-variable editor — a replacement for the cramped
built-in dialog. Native, fast, themeable, and a single small self-contained exe.

The product works — and it does not scale. The current effort is not about features: it is
about morphing something that works into something genuinely good, and then extracting a UI
library — **ancusi** — from it.

`docs/architecture.md` describes the structure we are heading for and the decisions behind it.
`docs/as-built.md` describes the code as it actually is today. This file is the route from one
to the other.

## Direction (decided 2026-05-30)
The UI host is **pure Win32 + Direct2D + DirectWrite**. We arrived here after two
C++/WinUI attempts failed in two different WinUI subsystems (input routing, then
deployment). Win32 + D2D deletes that entire failure surface:

- one self-contained exe, **no runtime to deploy**, instant startup
- full control over look and input — no black-box framework
- the host-agnostic logic is reused unchanged

Rejected alternatives: Qt (heavy new dep), C#/.NET WinUI (abandons the no-.NET rule),
staying on C++/WinUI (the path that failed twice).

## Design principles
- **Paint-on-change.** We repaint when something changed rather than rebuilding the UI every
  frame — retained state, not immediate mode. **No animation in the early phases**; this is a
  sequencing decision, not a permanent ban, and animation may well be added once the paint path
  is something we trust.
- **Modern = typography + palette, not motion.** Variable fonts via DirectWrite, a careful
  colour table, ~1px borders, Win11 chrome (rounded, dark title bar).
- **One theme table, indexed semantically.** Painters ask for meaning — `accent`,
  `foreground`, `foreground_highlighted`, `error` — not for a palette slot and never for a
  literal. Base16 YAML files are one *source* that fills those semantic slots; they are not the
  index. Today's table is indexed by appearance and by environ specifics (`rowInvalid`,
  `readonlyText`), which is precisely what has to change for it to live in a library.
- **Custom-drawn everything**, except the transient inline cell editor, which is a
  skinned standard `EDIT` (borderless, `WM_CTLCOLOREDIT`).
- **Tiny and dependency-light.** No new deps without asking. No vcpkg.
- **Testable by construction.** Anything that decides something is reachable from a console test
  binary. This is new, and most of the codebase does not satisfy it yet.

## Where the code stands
The feature roadmap is finished: the editor, per-segment PATH editing, validation and duplicate
detection, SQLite snapshot history, dry-run review, Base16 theming, elevation, search, the
browse picker, TOML import/export and horizontal scrolling all shipped between 2026-05 and
2026-06. That history lives in git, not here.

What did not ship is structure: no tests, no library boundary, no error handling worth the name,
and no navigation model. That is what the phases below are for.

---

## Phases

Each phase is a series of small, separately reviewed changes, not one commit. A phase is done
when its exit criterion holds — and every exit criterion is something that can be executed or
inspected, not merely asserted.

**Phases 2 onward are provisional.** They are written down so the direction is not lost, but
they are to be revisited in the light of what phase 0.b teaches us. Planning a restructure of
code nobody has fully read is how plans go wrong.

### Phase 0.a — the goal, written down (current)
- `docs/architecture.md` — the target structure and the decisions.
- This file — direction, principles, phases.
- `CLAUDE.md` / `AGENTS.md` — the rules and the process (issue #1).
- `docs/plan-process-scope.md` deleted: a working plan for a feature that shipped.

**Exit:** an agent reading the repo cold knows what we are building and how we work.

### Phase 0.b — the code as it is, written down
**Before anything is restructured, the existing code is understood — by the human, personally,
warts and all.** There is code nobody is currently confident to move because it is unclear what
it does; `ui::FontSelectionView::Paint` is the named example, and it is not the only one.

- `docs/as-built.md` — a walkthrough of the code that exists: what each file owns, how a paint
  pass actually flows, how input is routed, where state lives, and what the parts nobody
  understands are actually doing. Warts documented as warts, not tidied in the prose.
- Written to be read and challenged, the way these documents were.

**Exit:** the human says he understands what we have. Not a proxy metric — the actual statement.

**Why this gates everything:** phases 2–4 describe surgery on code whose behaviour is not yet
fully known. Any of them may look different, or unnecessary, after this phase. That is the point
of doing it first rather than discovering it halfway through phase 3.

### Phase 1 — foundations (mechanical, not architectural)
Nothing here changes a design. Everything here is depended on by everything after it, and all of
it is the kind of work that is cheaper to do once, now, than twice across two libraries later.

- `environ-core.lib` + `environ-tests.exe`, console, headless. First real tests:
  `EnvWriter::compute_diff`, `EnvStore` expansion and validation, `EnvExport` round-tripping.
- **`ui-tests.exe`, also here** — compiled against the existing `src/win32` sources, no
  extraction required. It covers the UI logic that is not paint: hit testing, selection
  arithmetic, scroll clamping, the edit state machine. It becomes `ancusi-tests` in phase 4.
  This is not deferrable to phase 4: phase 3 rewrites grid behaviour, so the tests that pin
  that behaviour must predate it.
- **CMake replaces `environ.vcxproj`** (decision and trade-offs in `docs/architecture.md`),
  while there is one target to convert rather than five. **A Ninja preset is required, not
  optional**: the Visual Studio generator ignores `CMAKE_EXPORT_COMPILE_COMMANDS` (verified on
  this machine), and `compile_commands.json` is what fixes the editor's false errors and what
  the naming check reads.
- The configuration every target must agree on becomes one CMake interface target (issue #4).
- fmtlib adopted; `SPDLOG_FMT_EXTERNAL` replaces `SPDLOG_USE_STD_FORMAT` (issue #4).
- Logging routed through pnq, with every failure logging its decoded reason (issue #2).
- `.clang-format` and `.clang-tidy`, with the naming table from `docs/architecture.md`, wired
  to the compilation database CMake produces.

**Exit:** `just test` runs both test binaries and is green; `compile_commands.json` exists from
the Ninja preset and clang-tidy has run against it; the mechanical migrations are done once.

*Can run in parallel with 0.b — it changes no structure and settles no design question.*

### Phase 1.b — the text migration
UTF-8 everywhere, converted at the Win32 edge (`docs/architecture.md` has the decision). This is
**not** mechanical, which is why it is not in phase 1: it changes core's entire public surface,
and `EnvVariable` carries wide strings today.

It comes directly after phase 1 because phase 1's tests are what make it safe, and before phase 3
because the grid split would otherwise be done twice — once against wide strings and once again
after. **It also requires phase 0.b**, not just phase 1: phase 1 may run alongside 0.b because
it settles no design question, but 1.b changes core's public surface, and the rule that nothing
is restructured before the code is understood applies to it as much as to phases 2–4.

**Exit:** no `std::wstring` in a core public header; non-ASCII names and values survive
read → edit → write → read unchanged; empty values stay empty rather than becoming absent;
snapshot and TOML export/import round-trip byte-identically across the change. All four executed,
not argued.

### Phase 2 — the host, made readable *(provisional)*
The complaints that started this, fixed in place, while the code is still in one project.

- `d2dwindow.h` stops mixing concerns; `HandleMessage`'s single switch becomes named handlers.
- Every Win32 and COM call's result is checked, and every failure says why it failed.
- Magic constants in painting code move behind the theme/layout table.
- The theme table is reindexed semantically.
- The hamburger "menu" becomes a real menu, and navigation becomes a described thing rather
  than nine hand-hit-tested items.
- The Direct2D device-context migration lands here, after the paint path is readable.

**Exit:** the files that are about to become ancusi are files we would be willing to publish.

### Phase 3 — the grid split *(provisional)*
`Grid` is 1,800 lines that are simultaneously a widget and environ's data model.

- The application owns the state; the grid renders it (see `docs/architecture.md`).
- The decisions come out into plain objects with no `HWND` in reach, under test: row building,
  selection arithmetic, the edit state machine, scroll clamping, sorting.

**Exit:** grid behaviour is covered by tests that run with no window on screen; the widget no
longer names an `Environ::core` type.

### Phase 4 — carve out ancusi *(provisional)*
Mechanical, if phases 2 and 3 did their job.

- `ancusi.lib` with public headers under `src/ancusi/include/ancusi/`; `ui-tests` becomes
  `ancusi-tests.exe` and from here links ancusi alone, which is what makes the boundary real.
- The generic grid contract, with environ's specialization on top of it.
- `AppSettings`' UI half moves into ancusi.

**Exit:** ancusi links without environ-core; environ is behaviourally unchanged; every ancusi
feature is exercised by an ancusi test.

### Phase 5 — later, and deliberately unplanned
ancusi to its own repository. pserv as a second consumer, which is what will prove ancusi is a
library rather than environ's UI with a second caller. The shape of both depends on what phases
1–4 teach us.

---

## Decisions and open questions
Both live in `docs/architecture.md` rather than being duplicated here. Settled so far: the build
system (CMake, with a Ninja preset for tooling), naming (snake_case, clang-tidy enforced, with
`Environ` a required exception because the CRT defines `environ` as a macro), text (UTF-8 with
`wstr_param` at the Win32 edge, phase 1.b), grid state ownership (the application's), and the
Direct2D generation (device context, in phase 2).
