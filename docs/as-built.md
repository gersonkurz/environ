# environ — the code as it is

A guide to the existing code, written to be read *next to* the code rather than instead of it.
Warts are named as warts. Where something is wrong, it says so and points at the line.

`docs/architecture.md` says where we are going. This says where we are. They will disagree with
each other until phase 4 is done; that is the point of having both.

**Status:** written incrementally, one subsystem at a time (issue #7). Sections not listed below
are not yet covered.

---

## The general flow

```
wWinMain                                     app.cpp:67
  └─ App::Run                                app.cpp:43
       ├─ InitLogging()                      %LOCALAPPDATA%\environ\environ.log
       ├─ SetProcessDpiAwarenessContext(PER_MONITOR_AWARE_V2)
       ├─ CoInitializeEx(STA)                for IFileOpenDialog / ShellExecuteEx
       ├─ MainWindow wnd; wnd.Create(...)
       └─ MessageLoop()                      GetMessage / Translate / Dispatch
```

`MainWindow::Create` runs `InitGraphics` (D2D + DWrite factories), `InitSettings`, `InitFonts`
and `InitData` **first**, and only then calls `D2DWindow::CreateD2DWindow` to register the class
and create the HWND. That order matters: the text formats exist before any window does, which is
why they are sized in DIPs and are not rebuilt on a DPI change. It also means creation and
`ShowWindow` can re-enter the window procedure with everything already initialised.

Every message arrives at `D2DWindow::StaticWndProc`, which recovers `this` from
`GWLP_USERDATA` and calls `HandleMessage`. `MainWindow` **overrides** that, handles 36 message
cases in 468 lines, and calls the base for anything it does not want — where 6 more cases live.

**Paint:** `WM_PAINT` → `EnsureRenderTarget` → `BeginDraw` → `OnPaint(size)` → `MainWindow`
draws the caption, then `m_activeView->Paint(MakeContext(), ViewBounds(sz))`, then the nav
panel, then modals over the top. → `EndDraw`, and `D2DERR_RECREATE_TARGET` discards the device
resources and invalidates.

**Input:** `MainWindow` decides whether an event belongs to the chrome (caption, burger, nav,
modal). If not, it forwards to the active view — `m_activeView->OnLButtonDown(ctx, x, y, …)` —
whose handlers return `bool` meaning *I handled this*, which the shell turns into a repaint.

**There is no navigation model.** `MainWindow::NavAction` is a nine-value enum mixing navigation
(`Themes`, `Fonts`, `History`) with commands (`Save`, `Export`, `Import`, `RunAsAdmin`, `About`,
`Exit`). Going *to* a view is `SwitchToView(View*)`. Coming *back* is polling:
`CheckHistoryAction()`, `CheckThemeAction()`, `CheckFontAction()` ask each view whether it has
produced a result, and all three are called from two separate points in the message switch
(`mainwindow.cpp:1267` and `:1366`).

---

## `d2dwindow.*` — the window base class

109 lines of header, 394 of implementation. Everything else in `src/win32/` sits on top of it.

### What it is

`D2DWindow` is the base of `MainWindow` and is described in its own comment as a "reusable
frameless Win32 + Direct2D/DirectWrite window base class". It is three things at once:

1. **A window.** `CreateD2DWindow` (`:254`) registers the class and creates the HWND;
   `StaticWndProc` (`:59`) is the trampoline; `HandleMessage` (`:304`) handles
   `WM_NCCALCSIZE`, `WM_NCHITTEST`, `WM_PAINT`, `WM_SIZE`, `WM_DPICHANGED`, `WM_DESTROY`.
2. **A Direct2D device-resource owner.** `InitGraphics`, `EnsureRenderTarget`,
   `DiscardDeviceResources`, `ReleaseGraphics`, and the factory/render-target/brush members.
3. **A custom title bar.** `DrawCaption`, `ApplyDarkTitleBar`, the caption-button geometry and
   glyphs, `SetupMouseTracking`, `UpdateCaptionHover`, `ResetCaptionTracking`,
   `HandleCaptionClick`, `m_capHover`.

Plus `TextFormat`, an unrelated move-only RAII wrapper for `IDWriteTextFormat*`, in the same
header.

The subclass hooks are `OnPaint` (pure virtual), `OnSize`, `OnDpiChanged`, `OnDestroy`.

### Where the frameless window is actually implemented

Worth reading as a unit, because it is the part that would be hardest to reconstruct:

- **`WM_NCCALCSIZE` (`:308`)** returns 0 for `wp != 0`, which removes the standard frame and
  makes the client area cover the whole window. When maximized it insets by
  `SM_CXFRAME + SM_CXPADDEDBORDER`, which is the standard workaround for a maximized frameless
  window spilling over the screen edge.
- **`WM_NCHITTEST` (`:325`)** re-implements what the removed frame used to do: an 8-DIP resize
  border on each edge and corner when not maximized, `HTCAPTION` for the top strip so dragging
  works, and `HTCLIENT` over the caption buttons so they can be clicked.
- **`CaptionButtonRects` / `CaptionButtonAt` (`:29`, `:37`)** are file-local; the buttons are
  laid out from the right edge in units of `kBtnW`.

### The warts

**The base class owns the caption, but the subclass puts widgets in it.** `MainWindow` draws a
hamburger button and a "Run as Administrator" button into the caption strip — and because
`D2DWindow::HandleMessage` would claim that strip as `HTCAPTION` (draggable), `MainWindow`
intercepts `WM_NCHITTEST` *before* delegating and returns `HTCLIENT` for its own two rectangles
(`mainwindow.cpp:1191-1203`, comment: "Burger and admin buttons must return HTCLIENT so they're
clickable, not draggable"). The base class's model of the caption is wrong for its only
subclass, and the subclass patches it by getting in first.

**The caption geometry is duplicated, and the duplication is documented rather than removed.**
`kCaptionH{40.0f}` and `kBtnW{46.0f}` are file-local in `d2dwindow.cpp:15-16`. In
`mainwindow.cpp` the same numbers reappear as `kCapBtnW{46.0f}` — with the comment "mirrors
d2dwindow" (`:67`) — and as `kBurgerW{46.0f}` / `kBurgerH{40.0f}` (`:61-62`). `ViewBounds` then
hardcodes the caption height a third time as `40.0f + 8.0f` (`:314`).

**`EnsureRenderTarget` can succeed halfway (`:110`).** It early-returns `S_OK` whenever `m_rt`
is non-null, then creates the render target and *returns the HRESULT of the brush creation*. If
the render target is created but `CreateSolidColorBrush` fails, `m_rt` is left non-null and
`m_brush` null; the next call sees `m_rt` and returns `S_OK`, and painting dereferences a null
brush. Nothing logs it. `WM_PAINT` checks `SUCCEEDED(...)` and skips the frame, so the *first* paint is
merely blank — but the second one proceeds, and `DrawCaption`/`DrawString` dereference the null
brush. **This crashes, it does not just paint nothing.**

**Magic numbers in the caption layout.** `titleBox` at `:162` is
`RectF(14.0f + titleLeftInset, 0.0f, widthDip - 3 * kBtnW - 8.0f, kCaptionH)` — a 14-DIP left
inset and an 8-DIP gap before the buttons, neither named nor themed.

**A text layout is allocated on every caption paint (`:170`).** `CreateTextLayout` and
`layout->Release()` per frame, so the title's bold/normal split costs an allocation each
repaint. Failure falls back to `DrawString` silently.

**Error handling is absent or unhelpful.** `InitGraphics` (`:79`) logs "D2D1CreateFactory
failed" without the HRESULT — the example that started this cleanup. `RegisterClassExW`'s
return is ignored (`:264`, survivable: `CreateWindowExW` then fails and *is* logged).
`ApplyDarkTitleBar` ignores `DwmSetWindowAttribute`'s HRESULT (`:213`). (`SetDpi` returns `void`,
so there is nothing to check there — an earlier draft of this document claimed otherwise.)

**`MakeFormat` hand-rolls a lossy wide-to-narrow conversion purely to log a failure** (`:136`),
truncating each `wchar_t` to `char`. `pnq::string::encode_as_utf8` exists.

**`StaticWndProc` assumes `lpCreateParams` is non-null** on `WM_NCCREATE` (`:65-66`): a window
created with this class name but without a `D2DWindow*` dereferences null. Not reachable today —
`CreateD2DWindow` is the only caller — but the class is registered process-wide.

### What is good here, and should survive

The device-loss handling is correct and complete: `D2DERR_RECREATE_TARGET` from both `EndDraw`
(`:359`) and `Resize` (`:371`) discards device resources, and `DiscardDeviceResources` releases
exactly the device-dependent ones while keeping the factories. `WM_DPICHANGED` (`:377`)
repositions from the suggested rect *and* discards device resources, which is what makes
per-monitor DPI work. `TextFormat` is a correct move-only RAII wrapper. The frameless
implementation is small and conventional.

---

## Design implications

What the reading has taught us, accumulated as we go. This section is the point of the exercise:
`docs/architecture.md` says what we are building, and this says what the existing code proves we
need. Every entry names the evidence.

### From `d2dwindow.*`

**A rich-text abstraction is not speculative — the code hand-rolls it three times already.**
`DrawCaption` builds an `IDWriteTextLayout` by hand and sets a weight range so the subtitle reads
lighter than the title, with a silent fallback when that fails. The grid's detail strip composes
a knowledge-base description with separately coloured guidance (`gridview.cpp:225`). The message
box renders an arbitrary prose body. Three sites, three hand-rolled solutions, no shared concept.
See "Rich text" below for the shape.

**The title bar is a component, not a base-class feature.** The evidence is not aesthetic: the
base claims the whole caption strip as `HTCAPTION`, so its only subclass intercepts
`WM_NCHITTEST` first (`mainwindow.cpp:1191`) — and that interception costs the top-left resize
corner and parts of the top and left resize borders. The geometry is duplicated three times over
two files. A caption that owns its own layout, hit testing, hover and press state, and *tells*
the window what inset the content gets, removes all of that.

**Mouse-leave tracking is a window concern fused with a caption concern.** One `TME_LEAVE`
registration serves every hover in the application — caption buttons, burger, admin button, nav
items, grid rows. The base owns the flag; the subclass owns the handlers that drive it. Even the
names disagree: `SetupMouseTracking` is generic, `ResetCaptionTracking` is not, and the reset
does two unrelated jobs.

**One error convention, written down.** Four lifecycle functions, three conventions: `bool`
(`InitGraphics`), `void` (`DiscardDeviceResources`, `ReleaseGraphics`), `HRESULT`
(`EnsureRenderTarget`). CLAUDE.md permits all three and says nothing about when to use which, so
this is not a violation — it is an unwritten rule. The rule to write: `HRESULT` when a COM call's
code is the useful information, `bool` when the only question is "did it work", `void` only when
failure is impossible.

**Resources need an invariant, not a sequence.** `EnsureRenderTarget` builds two objects and
publishes the first before the second is known to have succeeded, so a failed brush leaves a
state that reports success and then crashes. Build into temporaries, publish together.

**Geometry needs a type and a single owner.** `40.0f` and `46.0f` are DIPs, but nothing in the
code says so — the unit lives in variable-name suffixes (`widthDip`) and in comments. A `Dips`
strong type answers "which unit" at every call site, and the caption component answers "who
decides".

**A library window may not end the application.** `WM_DESTROY` calls `PostQuitMessage(0)` in the
base class. That is application policy, not window behaviour: in ancusi, closing one window must
not stop the caller's message loop. Related: the destructor never destroys the HWND and
`WM_NCDESTROY` never clears `GWLP_USERDATA`, so "the object outlives its window" is an unstated
contract that happens to hold.

**Naming.** Beyond the convention question already settled in `docs/architecture.md`: names here
describe the wrong thing. `boldLen` is the length of the part that is *not* made lighter.
`DipScale` returns pixels per DIP. `CaptionButtonAt` returns an index, not a position.
`CreateD2DWindow` restates its own class name.

### Rich text — the shape

Two layers, and only the first is needed now:

1. **Styled text over `IDWriteTextLayout`.** A string plus ranges carrying weight, style,
   underline, colour and a hit-testable payload for links; measure, draw, hit-test. DirectWrite
   already provides all of it through `SetFontWeight`, `SetDrawingEffect` and `HitTestPoint` —
   this layer gives the three existing hand-rolled sites one implementation.
2. **A Markdown subset that emits layer 1.** Worth separating, because the cost is lopsided:
   *inline* markdown (bold, italic, code spans, links, line breaks) maps almost directly onto
   ranges and is a few hundred lines. *Block* markdown (lists, nested quotes, tables, fenced code,
   images) is a layout engine, and that is where a "small markdown renderer" stops being small.

Windows ships no Markdown renderer, and the usual answer — hosting a WebView — is precisely the
runtime dependency this project exists to avoid. So a subset is the normal solution, not a
reinvention. Build layer 1 first, and let a real consumer decide how much of layer 2 is wanted.

---

## Not yet covered

`view.h`, `app.*`, `mainwindow.*`, `grid.*`, `gridview.*`, `historyview.*`,
`themeselectionview.*`, `fontselectionview.*`, `theme.*`.
