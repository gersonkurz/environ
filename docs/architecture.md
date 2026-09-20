# environ — Architecture

This document describes the architecture we are **going to have**. Where it describes what
exists today, it says so explicitly and marks it as the starting point, not the design.

`docs/ROADMAP.md` holds the phases that get us from one to the other. `docs/as-built.md`
describes the existing code as it actually is.

---

## The goal

Five build targets: two libraries, the application, and a test binary for each library.

```
                    ┌───────────────────────┐
                    │      environ.exe      │   entry point, wiring,
                    │        (thin)         │   environ's own views
                    └───────────┬───────────┘
                    ┌───────────┴───────────┐
                    ▼                       ▼
        ┌───────────────────────┐   ┌───────────────────────┐
        │      ancusi.lib       │   │   environ-core.lib    │
        │   UI. No app domain.  │   │  Logic. No UI.        │
        └───────────┬───────────┘   └───────────┬───────────┘
                    └───────────┬───────────────┘
                                ▼
                    ┌───────────────────────┐
                    │  pnq (header-only)    │  + spdlog, fmt, fkYAML, toml++, sqlite
                    └───────────────────────┘

   ┌───────────────────────┐        ┌───────────────────────┐
   │    ancusi-tests.exe   │        │   environ-tests.exe   │
   │  travels with ancusi  │        │  console, headless    │
   └───────────────────────┘        └───────────────────────┘
```

**ancusi.lib and environ-core.lib do not know about each other.** Be precise about what enforces
that, because "they are separate libraries" does not by itself:

- **Include paths do the real work.** ancusi's target does not have `src/core` on its include
  path, so `#include "EnvStore.h"` fails to *compile*. CMake's per-target
  `target_include_directories` is what makes this true, and it is the check that catches the
  common case.
- **An independent consumer catches the rest — but only with `/WHOLEARCHIVE`.** A static library
  may contain unresolved references; the archiver does not mind, and by default MSVC pulls only
  the *referenced* objects out of an archive, so an unused object carrying a core dependency
  slips through a plain link. `ancusi-tests.exe` therefore links ancusi **alone and with
  `/WHOLEARCHIVE:ancusi.lib`** (in CMake, `$<LINK_LIBRARY:WHOLE_ARCHIVE,ancusi>`), which forces
  every archive member to resolve.

  Measured, not assumed. A probe with one unused object referencing an absent library:

  ```
  link main.obj ui.lib                  -> exit 0        (ui_unused.obj never pulled in)
  link main.obj /WHOLEARCHIVE:ui.lib    -> LNK2019 in ui.lib(ui_unused.obj)
  ```

  Without the flag the guarantee is only about the code the tests happen to link; with it, the
  guarantee is about every object in the library.
- **What is still not caught:** a domain type used in a way that emits no symbol at all — a
  header-inline struct, a type named only in a template that is never instantiated. The include
  path check above catches the ordinary form of this (the header cannot be reached), so the
  residue is narrow, and it is a review rule rather than an automated one. Stated plainly so
  nobody reads the table below as exhaustive.

### ancusi — ANCient USer Interface

A Windows-only, Direct2D/DirectWrite UI library. The name is set against MUI (Modern UI); the
joke is the point — small, immediate, owing nothing to a framework.

**The mid-term goal is a library you can build modern Windows UIs with, without the bloat of
WinUI 3.** Not "environ's UI with a second caller". That ambition is real and it is what the
design serves, even though the first phases deliberately build only what environ drives.

**What it contains:** the window host and its D2D/DWrite lifecycle, the `View` abstraction,
**navigation** — a declarative description of the screens and the flows between them, which does
not exist in any form today — the theme table and its semantic colour slots, the grid, the menu,
the modal, the form primitives, and the settings that are really UI state (window placement,
theme, fonts, zoom).

**What it depends on, by design:** Win32, Direct2D, DirectWrite, pnq, fmt, spdlog, fkYAML. It is
a Windows UI library; depending on the Windows UI stack is not a leak.

**What it must not contain is application-domain concepts** — an environment variable, a registry
scope, a PATH segment, a service, a process. No public ancusi header names an `Environ::core`
type. When a colour means "this path segment does not exist", that meaning lives in the
application; ancusi knows only that a row paints in the `error` slot.

**It lives in this repository for now and moves to its own repo before long.** That costs nothing
extra provided its public headers live under `src/ancusi/include/ancusi/` from the start and are
included as `<ancusi/grid.h>` even from environ: the eventual split is then a directory move plus
a submodule, not a rewrite of every include. For the same reason ancusi has **its own test
executable** rather than sharing environ's — two test binaries, one harness, so that ancusi's
tests travel with ancusi when it leaves.

**What "general" must not become:** speculative API surface with no caller and no test. The
ambition above is served by ancusi being *good* at what it does, not by it being large early. The
discipline: a feature ships when an `ancusi-tests` case exercises it — a feature only environ
exercises is environ-shaped code in a library-shaped folder.

### environ-core — the logic

Reading and writing environment variables, diffing, validation, snapshots, import/export, the
knowledge base.

**Core is a Win32 component.** It calls the Win32 API freely: the registry, the process
environment, the filesystem, and `SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, …)` to
tell the rest of the system that the environment changed — a system service, not a user
interface.

What core must not do is depend on *the application's* UI: it creates no window, owns no message
loop, handles no message, and names no rendering, view or theme type. The property we are buying
is that core can be exercised headless, by a console test binary, with no window on screen.

### environ.exe — thin

The entry point, the concrete main window, environ's own views, and the wiring that connects a
grid in ancusi to variables in core. Anything in here that *decides* something is in the wrong
place; decisions belong in core where a test can reach them.

### The test executables — the net

`environ-tests.exe` links environ-core and creates no window. `ancusi-tests.exe` links ancusi
alone and exercises everything in it that is not paint: hit testing, selection arithmetic, scroll
clamping, the edit state machine, menu and navigation models, theme loading. Same harness, two
binaries, for the reason given above.

**Both start in phase 1, before any extraction.** The UI-side binary does not wait for ancusi to
exist: in phase 1 it is a `ui-tests` target compiled against the existing `src/win32` sources,
and it is renamed to `ancusi-tests` in phase 4 when the library is carved out. This ordering is
the whole point — phase 3 rewrites grid behaviour, so the tests that pin that behaviour have to
predate it. A regression net delivered after the surgery is not a net.

---

## Decisions

### Build system — CMake

Replace the hand-maintained `environ.vcxproj` with CMake, in phase 1, **before** the split
rather than during it.

**For:**
- Five targets are coming. In MSBuild that is five `.vcxproj` with hand-maintained source lists
  — pserv, the comparable project, carries 62 and 53 entries plus three `.filters` files, and
  its own agent docs warn that files are "not discovered automatically". That tax is per file,
  forever.
- `CMAKE_EXPORT_COMPILE_COMMANDS=ON` emits `compile_commands.json` as a side effect of
  configuring — **but only under the Ninja and Makefile generators; the Visual Studio generator
  silently ignores it.** Verified on this machine (CMake 4.2.1, VS 18): configuring a minimal
  project with `-G "Visual Studio 18 2026"` produced no `compile_commands.json`; `-G Ninja`
  produced one. So the tooling configuration is a **Ninja preset**, with the VS generator
  available for IDE use and never relied on for the database. That file is what clang-tidy's
  naming enforcement needs — and what clangd needs, which is why the editor currently reports
  false errors on this repository ("precomp.h not found", missing std types, undeclared Win32).
- The configuration contract stops being a document. What would otherwise be a shared `.props`
  file plus a convention becomes an ordinary CMake interface target that every target links.
- pnq is already CMake — INTERFACE target, Catch2 via FetchContent, ctest on x64 and ARM64. When
  ancusi moves to its own repository and is consumed as a submodule, `add_subdirectory` is the
  path pnq already uses.
- ctest is the test runner phase 1 needs anyway.

**Against, stated rather than hidden:** the `.rc` resource, the application manifest, the
x64/ARM64 matrix and the MSIS packaging all currently run `justfile` → msbuild and must be
re-established. PCH is `target_precompile_headers`. Visual Studio opens CMake projects natively,
so the IDE is not a loss; `environ.slnx` simply stops being hand-maintained.

**When:** phase 1, while there is one target to convert instead of five. It must land before
naming enforcement, which depends on its output. Phase 1 does not count CMake as done until a
`compile_commands.json` exists from the Ninja preset and clang-tidy has actually run against it.

### Naming — snake_case, and enforced

Consistency matters more than which convention. The split, Python-flavoured:

| | |
|---|---|
| types — class, struct, enum, concept | `CamelCase` |
| functions, methods, variables, parameters | `snake_case` |
| namespaces | `snake_case` — **except `Environ`**, see below |
| members | `m_snake_case` |
| compile-time constants | `snake_case` |

**`Environ` is a required exception, not an oversight.** The namespace must stay capitalised
because MSVC's CRT defines `environ` as a macro, so a lowercase namespace does not compile. The
`.clang-tidy` configuration has to carve it out explicitly; a mechanical lowercasing pass would
break the build. `ancusi` has no such problem and is lowercase.

**This is enforceable, mechanically.** LLVM 22.1.3 ships with Visual Studio 18 —
`clang-tidy.exe` and `clang-format.exe` are already on this machine. `clang-tidy`'s
`readability-identifier-naming` check enforces exactly this table from a `.clang-tidy` file, and
`--fix` performs the renames itself; `clang-format` handles layout. Neither is quite `go fmt`:
clang-tidy needs a compilation database (`compile_commands.json` — the compiler command line per
translation unit, which is how a tool that parses rather than builds your code learns your
includes, defines and standard). MSBuild does not emit one; CMake does, which is one of the
reasons for the decision above. Until it is wired the convention is a review rule; after, it is
a build step.

### Text — UTF-8 everywhere, converted at the Win32 edge

Our types hold `std::string` in UTF-8, matching pnq. Conversion happens only where Win32 is
called:

- **inbound to Win32:** wrap the argument in `pnq::win32::wstr_param`, which converts on the fly
  and yields `LPCWSTR`. This is exactly what `pnq::regis3` does — `RegOpenKeyExW(hive,
  wstr_param{relative_path}, …)` — so core's registry code converges on the pattern pnq already
  uses.
- **outbound from Win32:** `pnq::string::encode_as_utf8(std::wstring_view)`.

No `std::wstring` in our own interfaces. Today core is `std::wstring` throughout and
`EnvVariable` carries wide strings, so this is a migration across core's entire public surface —
not a rule that can simply be declared. It is scheduled as **phase 1.b**, deliberately after the
core tests exist, because those tests are what make the conversion safe.

**Done means, concretely:** no `std::wstring` in a core public header; non-ASCII variable names
and values survive read → edit → write → read unchanged; empty values stay empty rather than
becoming absent; and snapshot and TOML export/import round-trip byte-identically across the
change.

### Grid state — the application owns it

The grid renders state the application owns; it does not become the home of the data. This is the
model that let pserv's controller survive five different UI technologies, and it is the opposite
of what `Grid` does today.

### Direct2D — the device-context path

Move from `ID2D1HwndRenderTarget` to `ID2D1DeviceContext` on a flip-model DXGI swapchain.

**For:** it is the path that still gets developed — effects, current text rendering, DXGI
interop and proper DWM composition all live there, and the Hwnd render target is a frozen Windows
7-era API. Lower presentation latency.
**Against:** more setup (D3D11 device, DXGI factory, swapchain), device-loss handling grows, and
it changes the paint path of a working application.

**When:** once, in environ, while there is exactly one consumer — and after the paint path is
understood and readable, not before. Doing it after ancusi has two consumers costs twice.

---

## Where we are today (the starting point)

Honest measurements, 2026-09-20. One `environ.vcxproj`, no libraries, no tests.
`docs/as-built.md` is the detailed account; this is the summary.

| | LOC | |
|---|---:|---|
| `src/win32/` | 7,053 | the host — but eight of its nine files reference `Environ::core` |
| `src/core/` | 1,988 | the logic |
| `src/win32/grid.{h,cpp}` | 1,800 | see below |
| `src/win32/mainwindow.{h,cpp}` | 1,721 | the shell, plus a hand-rolled nav and modal |

**The boundary does not exist yet.** `grid.h` includes `EnvStore.h`. The two-layer rule has been
in `CLAUDE.md` all along and is violated in eight files, which is what happens to a rule nothing
enforces.

**`Grid` is everything at once:** the display-row model, the selection model, the edit state
machine, hit testing, scrolling, painting — and environ's data model, holding `m_userOrig` /
`m_machineOrig` and computing what changed.

**The theme table is environ-specific, and indexed by appearance rather than meaning.**
`ColorScheme` ships `rowInvalid` ("path segment does not exist"), `rowDuplicate`, `readonlyText`
and `inheritedText`. A library cannot ship a colour named after a PATH segment.

**There is no navigation model.** The hamburger nav is nine hand-hit-tested items; flows between
screens are implicit in window code.

**The renderer is Direct2D 1.0** — `ID2D1Factory`, `ID2D1HwndRenderTarget`, `IDWriteFactory`.

**Error handling is mostly absent.** Win32 and COM calls are assumed to succeed; where a failure
is logged at all, it is usually logged without its reason.

---

## How each boundary is enforced

A boundary that is only asserted gets violated. Each row names what actually catches it.

| Boundary | Enforced by |
|---|---|
| ancusi cannot include core's headers | per-target include paths — `src/core` is not on ancusi's |
| ancusi references no core symbol | `ancusi-tests.exe` links ancusi alone **with `/WHOLEARCHIVE`**, so every archive member must resolve |
| domain type used with no emitted symbol | not automated — review, narrowed by the include-path check |
| core contains no application UI | `environ-tests.exe` links and runs core with no window |
| naming convention | `.clang-tidy` `readability-identifier-naming`, from the Ninja preset's database |
| painting reads no colour literal | review, and the absence of literals to copy |
| build configuration agrees across libraries | one CMake interface target that every target links |

That last row is not bureaucracy. `SPDLOG_USE_STD_FORMAT` versus `SPDLOG_FMT_EXTERNAL` differing
between two static libraries in one executable is an ODR violation that links without a
diagnostic and misbehaves at runtime. Once there is more than one target the configuration is a
contract, and the cheapest way to keep a contract is to have exactly one copy of it that
nothing can opt out of.

---

## Deliberately far future

Recorded because they shape API decisions now, not because anything is planned:

- **A declarative flavour.** Navigation is the first place this shows up. If ancusi's screen and
  flow descriptions are data rather than code, a SwiftUI-shaped API — or a non-Windows backend —
  remains conceivable instead of being designed out on day one.
- **pserv as the second consumer**, which is what will prove ancusi is a library.

Neither justifies a single line of speculative code today.
