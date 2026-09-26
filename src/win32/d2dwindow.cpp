#include "precomp.h"

// D2DWindow — reusable frameless Win32 + Direct2D/DirectWrite window base class.
// Handles WndProc trampoline, D2D lifecycle, NCCALCSIZE/NCHITTEST for frameless
// window behavior, custom-drawn caption buttons, DPI, and device-loss recovery.

#include "d2dwindow.h"

/*GK: ??? What would be an immersive dark mode, and how does it differ from a regular dark mode? */
//CC: "Immersive" is Microsoft's internal name for the Windows 8 "immersive shell" (Metro); it stuck.
//CC: The attribute tells DWM to paint THIS WINDOW'S non-client area dark -- title bar, border, frame
//CC: (The documented contract covers frame COLOURS. I also claimed the shadow, which it does not
//CC: promise -- and on a window whose caption we removed, the precise visible effect is worth simply
//CC: observing rather than asserting.)
//CC: It says nothing about the client area, which we paint ourselves. Since NCCALCSIZE already
//CC: removed the standard caption, what it still affects here is the border and the DWM frame.
//CC: The value is 20 on Windows 10 20H1 and later; on 1809-1903 it was 19. We hardcode 20 and never
//CC: check the build, so on those older builds this silently does nothing.
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

namespace
{
    //GK: unclear what these are, which unit they are in (for sure not "k"), and why float - every instinct in me says to avoid float at any cost?
    //CC: Unit is DIPs -- device-independent pixels, 1/96 inch. D2D works in DIPs and the render target's
    //CC: SetDpi() maps them to physical pixels, which is what makes the app DPI-correct for free.
    //CC: 'k' is the Google style prefix for a constant; it collides with the snake_case we just chose.
    //CC: float is not a taste decision here: D2D's API *is* float (D2D1_RECT_F, DrawText, FillRectangle).
    //CC: Using int would mean converting at every call. Your instinct is right for money and indices and
    //CC: wrong for a vector-graphics API whose native unit is a real number. What is missing is the TYPE:
    //CC: a `Dips` strong type would answer 'which unit' at every call site instead of in a comment.
    constexpr float kCaptionH{40.0f};
    constexpr float kBtnW{46.0f};

    // Segoe Fluent Icons glyphs (PUA codepoints, numeric to avoid source-encoding issues).
    constexpr wchar_t kGlyphMin{0xE921};
    constexpr wchar_t kGlyphMax{0xE922};
    constexpr wchar_t kGlyphRestore{0xE923};
    constexpr wchar_t kGlyphClose{0xE8BB};

    struct CaptionButtons
    {
        D2D1_RECT_F min, max, close;
    };

    //GK: strange idiom: why not a simple constructor?    
    //CC: Two callers, not one: CaptionButtonAt below (hit testing) and DrawCaption (painting).
    //CC: That makes it less objectionable than I first said -- a free function returning layout data is
    //CC: already a testable shape, and it is one of the few things here that could be unit-tested with
    //CC: no window at all. A constructor would read slightly better; there is no correctness argument.
    CaptionButtons CaptionButtonRects(float widthDip)
    {
        return CaptionButtons{
            D2D1::RectF(widthDip - 3 * kBtnW, 0.0f, widthDip - 2 * kBtnW, kCaptionH),
            D2D1::RectF(widthDip - 2 * kBtnW, 0.0f, widthDip - kBtnW, kCaptionH),
            D2D1::RectF(widthDip - kBtnW, 0.0f, widthDip, kCaptionH)};
    }

    //GK: I guess this means you want to check if a given point is over one of the caption buttons.
    //GK: a) then why not say so, b) float???? c) what is widthDip?
    //CC: Correct. Returns which caption button is under the point, or -1 for none.
    //CC: a) agreed, the name should say 'which'. b) float: DIPs, see above. c) widthDip is the window's
    //CC: client width in DIPs -- needed because the buttons are laid out from the RIGHT edge.
    //CC: The real wart is the int: 0/1/2 are magic numbers repeated in DrawCaption and HandleCaptionClick.
    //CC: An enum class CaptionButton { None, Minimize, Maximize, Close } removes all three.
    int CaptionButtonAt(float xDip, float yDip, float widthDip)
    {
        if (yDip < 0.0f || yDip >= kCaptionH) return -1;
        const CaptionButtons b = CaptionButtonRects(widthDip);
        if (Contains(b.min, xDip, yDip)) return 0;
        if (Contains(b.max, xDip, yDip)) return 1;
        if (Contains(b.close, xDip, yDip)) return 2;
        return -1;
    }
}

// ---------------------------------------------------------------------------
// D2DWindow
// ---------------------------------------------------------------------------

ui::D2DWindow::~D2DWindow()
{
    ReleaseGraphics();
}

// --- WndProc trampoline ---

//GK: Common standard idiom. Note that there can be messages before WM_NCCREATE, can there not?
//CC: Yes, there are -- and this matters, because your other comment below says the opposite.
//CC: CreateWindowEx sends WM_GETMINMAXINFO before WM_NCCREATE for an overlapped window. That arrives
//CC: when GWLP_USERDATA is still 0, so `self` really is null there and the guard is load-bearing.
//CC: Deleting it would crash during window creation.
LRESULT CALLBACK ui::D2DWindow::StaticWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    D2DWindow* self{nullptr};
    if (msg == WM_NCCREATE)
    {
        auto* cs = reinterpret_cast<CREATESTRUCT*>(lp);
        self = static_cast<D2DWindow*>(cs->lpCreateParams);
        self->m_hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    else
    {
        self = reinterpret_cast<D2DWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    // self can never not be nullptr, otherwise we'd have crahsed before in the "self->m_hwnd" line ;)
    //CC: Not so -- see the note on the function above. WM_GETMINMAXINFO arrives before WM_NCCREATE with
    //CC: GWLP_USERDATA still unset, so this branch is taken on a real message during CreateWindowEx.
    //CC: What IS unguarded is the line above: if someone created a window of this class without passing
    //CC: a D2DWindow* as lpCreateParams, `self->m_hwnd` dereferences null. Not reachable today, but the
    //CC: window class is registered process-wide.
    if (self) return self->HandleMessage(msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// --- D2D lifecycle ---

/*GK: the name says it initializes graphics

a) it is unclear why SINGLE_THREADED is a desirable here. Why not multi-threaded (honest question)?
//CC: a) The factory type decides whether D2D takes an internal lock on every call. SINGLE_THREADED
//CC:    means no locking and all D2D calls must come from one thread. We only ever paint on the UI
//CC:    thread, so it is both correct and cheaper. MULTI_THREADED would buy nothing and cost a lock.
//CC: b) They are two different libraries. Direct2D draws (geometry, bitmaps, brushes, render targets).
//CC:    DirectWrite does text (font enumeration, shaping, layout, glyph runs). D2D can render a
//CC:    DirectWrite layout, but they are separate COM libraries, hence two separate factories.
//CC:    Rule of thumb: m_d2d makes things that paint; m_dw makes things that describe text.
b) What I also don't understand: we create a factory, and a "WriteCreateFactory" - what's the differnce?

*/
bool ui::D2DWindow::InitGraphics()
{
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &m_d2d)))
    {
        spdlog::error("D2D1CreateFactory failed");
        return false;
    }
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(&m_dw))))
    {
        spdlog::error("DWriteCreateFactory failed");
        return false;
    }
    return true;
}

void ui::D2DWindow::DiscardDeviceResources()
{
    if (m_brush) { m_brush->Release(); m_brush = nullptr; }
    if (m_rt)    { m_rt->Release();    m_rt = nullptr; }
}

void ui::D2DWindow::ReleaseGraphics()
{
    DiscardDeviceResources();

    /*GK: Why: doesn't the destructor does this? I mean, why would we want to have a separate ReleaseGraphics method?
    especially, given we also have DiscardDeviceResources(), and that for sure sounds like a RAII candidate?
    Also, that Release() dance really is something we should do in a COM RAII helper
    */
    //CC: The destructor DOES call this (see ~D2DWindow). It is separate because WM_DESTROY calls it too,
    //CC: to release the render target while the HWND is still alive rather than after it is gone.
    //CC: You are right that this is the RAII-shaped hole: if every COM pointer here were a smart pointer
    //CC: (WRL ComPtr or our own), the destructor would need no help and WM_DESTROY would reset one member.
    //CC: DiscardDeviceResources would still exist, but as a deliberate device-loss step, not as cleanup.
    m_fmtCaption.reset();
    m_fmtGlyph.reset();
    if (m_dw)  { m_dw->Release();  m_dw = nullptr; }
    if (m_d2d) { m_d2d->Release(); m_d2d = nullptr; }
}

/* GK: so this thing does a lot. 

- It finds the current client rect (presumably, redone on every resize?). Client rect is a Win32GUI term in my book.
- Then it creates a render target in there - presumably, the details needed for D2D to draw in there.
- Then it also changes the DPI for that render target to match the window's DPI.
- And finally, it creates a brush the color of white (which is not part of the theme, is it?)

*/
//CC: All four readings are right. On the white brush: it is never white in practice -- every draw
//CC: call does SetColor first (see DrawString), so this is one scratch brush reused for everything
//CC: and the initial colour is arbitrary. It is not themed and could not be.
//CC: The bug: the early return at the top treats 'm_rt exists' as 'everything exists'. If
//CC: CreateHwndRenderTarget succeeds and CreateSolidColorBrush fails, we return the failing HRESULT
//CC: but leave m_rt set -- and the NEXT call returns S_OK immediately with m_brush still null.
//CC: Nothing logs it. WM_PAINT skips that frame, so the FIRST paint is blank -- but the NEXT call
//CC: returns S_OK on the early path and DrawCaption/DrawString dereference the null brush.
//CC: So this CRASHES; 'blank window' was my understatement.
//CC: Fix: create both into temporaries and publish only once both succeeded (or discard the target
//CC: when the brush fails), so the invariant is 'both exist or neither does'.
HRESULT ui::D2DWindow::EnsureRenderTarget()
{
    if (m_rt) return S_OK;
    RECT rc{};
    GetClientRect(m_hwnd, &rc);
    const HRESULT hr = m_d2d->CreateHwndRenderTarget(
        D2D1::RenderTargetProperties(),
        D2D1::HwndRenderTargetProperties(m_hwnd, D2D1::SizeU(
            static_cast<UINT32>(rc.right - rc.left),
            static_cast<UINT32>(rc.bottom - rc.top))),
        &m_rt);
    if (FAILED(hr)) return hr;
    const UINT dpi = GetDpiForWindow(m_hwnd);
    m_rt->SetDpi(static_cast<float>(dpi), static_cast<float>(dpi));
    return m_rt->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), &m_brush);
}

// --- Text helpers ---
/*GK: Hardcoding a lot here. 

- Why do we create FONT_STYLE_NORMAL: presumably a style bold could be useful? 
//CC: Right -- italic is simply unreachable through this function; nothing in the app can request it.
//CC: On rich formatting: it does not live on IDWriteTextFormat, which is uniform by construction.
//CC: It lives on IDWriteTextLayout, which carries per-range overrides. DrawCaption below already
//CC: reaches for exactly that to make the subtitle lighter than the title -- so the abstraction you
//CC: are describing is not speculative; the code already needs it in one place and hand-rolls it.
- Looks to me like D2D gives us rich formatting, we must introduce an abstract to use that later.

*/
IDWriteTextFormat* ui::D2DWindow::MakeFormat(const wchar_t* family, float size,
                                              DWRITE_FONT_WEIGHT weight, bool vcenter, bool hcenter)
{
    IDWriteTextFormat* fmt{nullptr};
    if (FAILED(m_dw->CreateTextFormat(family, nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
                                      DWRITE_FONT_STRETCH_NORMAL, size, L"en-us", &fmt)))
    {
        //GK: WTF is this:?
        //CC: A hand-rolled lossy conversion: each wchar_t truncated to a char, purely to log a font name.
        //CC: Non-ASCII family names become garbage. pnq::string::encode_as_utf8 does this correctly.
        std::string narrow;
        for (const wchar_t* c = family; *c; ++c) narrow.push_back(static_cast<char>(*c));
        spdlog::error("CreateTextFormat failed for '{}'", narrow);
        return nullptr;
    }
    fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    fmt->SetParagraphAlignment(vcenter ? DWRITE_PARAGRAPH_ALIGNMENT_CENTER
                                       : DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    if (hcenter) fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    return fmt;
}

//GK: So we created a white brush only then later to change the color as needed. I get it. 
//GK: but presumably, we should check - most of the text will be in mostly the same color. Who needs rainbows?
//CC: Reasonable worry, but a per-colour brush cache would almost certainly cost more to maintain than
//CC: it saves, and one mutable brush is a normal D2D idiom. Leave it.
//CC: Caveat on my first answer: I claimed SetColor does 'no device work, no allocation, no flush'.
//CC: That is an assumption about the implementation, not something the documented contract promises.
//CC: Read it as 'probably cheap, not measured' rather than as fact.
void ui::D2DWindow::DrawString(const std::wstring& s, IDWriteTextFormat* fmt,
                                const D2D1_RECT_F& box, const D2D1_COLOR_F& c)
{
    m_brush->SetColor(c);
    m_rt->DrawTextW(s.c_str(), static_cast<UINT32>(s.size()), fmt, box, m_brush,
                    D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

// --- Caption ---

void ui::D2DWindow::DrawCaption(const theme::ColorScheme& s, float widthDip,
                                 const wchar_t* title, float titleLeftInset,
                                 const wchar_t* subtitle)
{
    //GK: Magic constants wtf?
    //CC: 14.0f = left inset of the title text; 8.0f = gap between title and the first caption button;
    //CC: 3 * kBtnW = the width of the three buttons. So the box runs 'from 14 DIPs in, to 8 DIPs before
    //CC: the buttons start'. None of it is named or themed, and the caption layout is duplicated in
    //CC: mainwindow.cpp (kCapBtnW, comment 'mirrors d2dwindow'; kBurgerW/kBurgerH) and hardcoded a third
    //CC: time in ViewBounds as '40.0f + 8.0f'.
    const D2D1_RECT_F titleBox{
        D2D1::RectF(14.0f + titleLeftInset, 0.0f, widthDip - 3 * kBtnW - 8.0f, kCaptionH)};

    //GK: the variable says boldLen - because we use boldFace? But we didn't specify it when creating the font?
    //CC: Right, and the naming is misleading. m_fmtCaption is created SEMI_BOLD, so the whole string is
    //CC: semi-bold already; this code then sets the SUBTITLE range to NORMAL. So boldLen is really
    //CC: 'length of the part that keeps the format's own weight'. Nothing is made bold here -- the
    //CC: subtitle is made lighter. The name describes the opposite of what the code does.
    std::wstring text{title};
    const UINT32 boldLen{static_cast<UINT32>(text.size())};
    if (subtitle) text += subtitle;

    // Use a text layout so the subtitle range can carry a lighter weight than the title.
    IDWriteTextLayout* layout{nullptr};
    if (m_dw && SUCCEEDED(m_dw->CreateTextLayout(
            text.c_str(), static_cast<UINT32>(text.size()), m_fmtCaption.get(),
            titleBox.right - titleBox.left, titleBox.bottom - titleBox.top, &layout)) && layout)
    {
        if (subtitle)
            layout->SetFontWeight(DWRITE_FONT_WEIGHT_NORMAL,
                                  DWRITE_TEXT_RANGE{boldLen, static_cast<UINT32>(text.size()) - boldLen});
        m_brush->SetColor(s.headerText);
        m_rt->DrawTextLayout(D2D1::Point2F(titleBox.left, titleBox.top), layout, m_brush,
                             D2D1_DRAW_TEXT_OPTIONS_CLIP);
        layout->Release();
    }
    else
    {
        DrawString(text, m_fmtCaption.get(), titleBox, s.headerText);
    }

    //GK: I notice even LLMs are unclear when to sprinkle in `auto`, and when not to. 
    //CC: Fair, and there is no rule being followed here. A defensible one: `auto` when the type is already
    //CC: stated on the right (`auto* p = reinterpret_cast<T*>(x)`) or is unspellable; the explicit type
    //CC: when the type is the point. `const CaptionButtons b = ...` and `const bool zoomed = ...` are the
    //CC: explicit form of that rule; elsewhere in this file it goes the other way for no stated reason.
    const CaptionButtons b = CaptionButtonRects(widthDip);
    const bool zoomed = IsZoomed(m_hwnd) != 0;

    //GK: Magic constants again. Probably the buttons in the title bar?
    //CC: Yes -- and the 0/1/2 here are the same magic indices CaptionButtonAt returns and
    //CC: HandleCaptionClick compares against: three places agreeing by convention. The enum class from
    //CC: the earlier note removes all three at once.
    const struct { D2D1_RECT_F rect; int idx; wchar_t glyph; } buttons[]{
        {b.min, 0, kGlyphMin},
        {b.max, 1, zoomed ? kGlyphRestore : kGlyphMax},
        {b.close, 2, kGlyphClose}};

    for (const auto& btn : buttons)
    {
        const bool hover = (m_capHover == btn.idx);
        const bool isClose = (btn.idx == 2);
        if (hover)
        {
            m_brush->SetColor(isClose ? s.captionCloseHover.fill : s.rowHover.fill);
            m_rt->FillRectangle(btn.rect, m_brush);
        }
        const D2D1_COLOR_F glyphColor = (hover && isClose) ? s.captionCloseHover.text : s.headerText;
        m_brush->SetColor(glyphColor);
        m_rt->DrawTextW(&btn.glyph, 1, m_fmtGlyph.get(), btn.rect, m_brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
}

void ui::D2DWindow::ApplyDarkTitleBar(bool dark)
{
    //GK: well, honestly, why not pass BOOL directly instead of converting from bool?
    //CC: Because the conversion is load-bearing, though the ternary is not.
    //CC: DwmSetWindowAttribute takes a void* plus a size, so it needs the ADDRESS of a real 4-byte BOOL.
    //CC: BOOL is a typedef for int. `bool` is 1 byte. Passing &dark while claiming sizeof(BOOL) == 4
    //CC: would have DWM read three bytes past the object -- so a local of the exact type is required,
    //CC: not stylistic.
    //CC: What IS redundant is `dark ? TRUE : FALSE`: `const BOOL dwmDark{dark};` compiles and means the
    //CC: same thing (bool -> int in list-initialisation is a promotion, not a narrowing conversion).
    //CC: Should the PARAMETER be BOOL instead? No, and deliberately so. BOOL is an int, so it admits 2,
    //CC: -1 and 17 -- values this API does not define -- while our callers are C++ passing true/false.
    //CC: Keeping Win32 types out of our own signatures is also the boundary rule we just wrote into
    //CC: CLAUDE.md. So: parameter stays bool, the local stays, the ternary goes.
    //CC: Separately: the HRESULT is dropped here, like most of this file.
    const BOOL dwmDark{dark ? TRUE : FALSE};
    DwmSetWindowAttribute(m_hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dwmDark, sizeof(dwmDark));
}

// --- Caption interaction helpers ---
//GK: What is mouse-tracking used for?
//CC: For finding out when the mouse LEAVES. Windows sends WM_MOUSEMOVE while the cursor is inside
//CC: the window and then simply stops -- there is no 'mouse left' message unless you ask for one.
//CC: Without it, any hover highlight stays lit forever once the cursor moves out of the window: move
//CC: off the Close button by leaving the window entirely and it would stay red.
//CC: TrackMouseEvent with TME_LEAVE requests exactly ONE WM_MOUSELEAVE, the next time the cursor
//CC: leaves. It is one-shot, so it has to be re-armed. That is what m_tracking is for: re-arming on
//CC: every WM_MOUSEMOVE would be wasteful, so the flag says 'already armed', and the reset on
//CC: WM_MOUSELEAVE clears it so the next move arms it again.
//CC: In this codebase: MainWindow calls SetupMouseTracking() from its WM_MOUSEMOVE handler
//CC: (mainwindow.cpp:1274) and ResetCaptionTracking() from WM_MOUSELEAVE (mainwindow.cpp:1302).
//CC: Worth noticing for the cleanup, because it is the caption problem again in a second guise:
//CC: ONE registration serves every hover in the application -- the base class's caption buttons and
//CC: the subclass's burger, admin button, nav items and grid rows alike. The base owns the flag and
//CC: the reset; the subclass owns the message handlers that drive them. And the pair is not even
//CC: named consistently: SetupMouseTracking is generic, ResetCaptionTracking is caption-specific,
//CC: and the reset does two unrelated jobs -- clear the one-shot flag, and clear the caption hover.
//CC: Mouse-leave tracking is a WINDOW concern; caption hover is a CAPTION concern; they are fused.
void ui::D2DWindow::SetupMouseTracking()
{
    if (!m_tracking)
    {
        TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, m_hwnd, 0};
        TrackMouseEvent(&tme);
        m_tracking = true;
    }
}

bool ui::D2DWindow::UpdateCaptionHover(float xDip, float yDip, float widthDip)
{
    //GK: we're selecting which caption button (minimize, maximize/restore, close) is currently hovered over
    //CC: Correct -- it records which one is hovered and returns whether that CHANGED, so the caller only
    //CC: repaints when it must. The -1 is 'none', the same magic value again.
    const int cap = CaptionButtonAt(xDip, yDip, widthDip);
    if (cap != m_capHover) { m_capHover = cap; return true; }
    return false;
}

bool ui::D2DWindow::ResetCaptionTracking()
{
    //GK: Resetting the caption tracking state.
    //CC: Correct. Called on WM_MOUSELEAVE: clears the hover and the TrackMouseEvent flag, and returns
    //CC: whether anything changed so the caller can repaint. The pair with SetupMouseTracking is the
    //CC: standard Win32 hover dance -- Windows does not send a 'mouse left' message unless you ask.
    m_tracking = false;
    const bool changed{m_capHover != -1};
    m_capHover = -1;
    return changed;
}

bool ui::D2DWindow::HandleCaptionClick(float xDip, float yDip, float widthDip)
{
    //GK: clearly should be handlers to allow derived classes to overload
    //CC: Careful -- these are not hooks, they are helpers the subclass CALLS from its own message handler.
    //CC: Virtual would be the wrong fix: nothing overrides them and nothing calls them polymorphically.
    //CC: The real problem is upstream: the base class owns the caption at all, while its only subclass
    //CC: adds its own widgets to that caption and then has to intercept WM_NCHITTEST before the base sees
    //CC: it (mainwindow.cpp:1191) to stop those widgets being treated as draggable title bar.
    const int cap = CaptionButtonAt(xDip, yDip, widthDip);
    if (cap == 0) { ShowWindow(m_hwnd, SW_MINIMIZE); return true; }
    if (cap == 1) { ShowWindow(m_hwnd, IsZoomed(m_hwnd) ? SW_RESTORE : SW_MAXIMIZE); return true; }
    if (cap == 2) { PostMessageW(m_hwnd, WM_CLOSE, 0, 0); return true; }
    return false;
}

// --- Window creation ---

bool ui::D2DWindow::CreateD2DWindow(const wchar_t* className, const wchar_t* title,
                                     int x, int y, int w, int h, int showCmd)
{
    WNDCLASSEXW wc{}; //GK: always unsure: does or does C++ not guarantee this is zero-initialized? Yes for globals, but for stack-allocated locals?
    //CC: `WNDCLASSEXW wc{}` is guaranteed zero for a local. The rule is about the INITIALISER, not the
    //CC: storage: `wc{}` is value-initialisation, and for an aggregate that zero-initialises every member,
    //CC: on the stack as much as anywhere -- precisely, this is aggregate initialisation.
    //CC: (Pedantic correction to my first answer: `WNDCLASSEXW wc;` is not itself undefined. The
    //CC: declaration is fine; it leaves the members indeterminate, and READING one is the undefined part.)
    //CC: So this idiom is correct and worth keeping -- it is why there is no memset here.
    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = StaticWndProc;
    wc.hInstance = m_hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = className;
    RegisterClassExW(&wc);

    m_hwnd = CreateWindowExW(
        0, className, title, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        x, y, w, h, nullptr, nullptr, m_hInst, this);
    if (!m_hwnd)
    {
        spdlog::error("CreateWindowExW failed");
        return false;
    }

    // Validate the restored position against available monitors -- if the window
    // is entirely off-screen (monitor removed/reconfigured), reset to default.
    if (!MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONULL))
    {
        SetWindowPos(m_hwnd, nullptr, CW_USEDEFAULT, CW_USEDEFAULT, w, h,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }

    // Re-run NCCALCSIZE so the frameless client takes effect.
    //GK: explain this comment. 
    //CC: CORRECTED -- my first answer here was wrong, and Codex caught it.
    //CC: WM_NCCALCSIZE decides how much of the window is frame versus client area, and Windows caches
    //CC: the answer. Our handler returns 0 for the wParam != FALSE form, claiming the whole window as
    //CC: client area -- that is what makes the window frameless.
    //CC: What was wrong: I said the handler is installed too late for window creation to see it. It is
    //CC: not. StaticWndProc is in the window class, registered BEFORE CreateWindowExW, so our handler
    //CC: does run during creation. SWP_FRAMECHANGED here is simply the conventional extra frame
    //CC: recalculation from Microsoft's own custom-frame sample. Keep the call; discard my reason.
    SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0,
                 SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
    ShowWindow(m_hwnd, showCmd);
    return true;
}

// --- Convenience ---

//GK: it almost looks like a LLM spelling mistake: DpiScale?
//CC: Not a typo, though it reads like one. It returns DPI/96 -- physical pixels per DIP. 'DipScale'
//CC: is the scale OF a DIP; 'DpiScale' would be the scale of a DPI, which means nothing. Both names
//CC: are bad. scale_factor(), or a Dips type that knows how to convert itself.
float ui::D2DWindow::DipScale() const
{
    return static_cast<float>(GetDpiForWindow(m_hwnd)) / 96.0f;
}

void ui::D2DWindow::Repaint(bool needed)
{
    if (needed) InvalidateRect(m_hwnd, nullptr, FALSE);
}

// --- Message dispatch ---

LRESULT ui::D2DWindow::HandleMessage(UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_NCCALCSIZE:
        if (wp)
        {
            if (IsZoomed(m_hwnd))
            {
                //GK: Why are we checking if the window is zoomed here? I mean, assume we're zoomed and want to go back, don't we need this, too?
                //CC: Only the maximized case needs it. Unmaximized, returning 0 gives exactly the client area we want.
                //CC: Maximized, Windows sizes the window to the monitor PLUS the frame width -- so a frameless window
                //CC: would hang off all four edges and cover the taskbar. The inset by SM_CXFRAME + SM_CXPADDEDBORDER
                //CC: puts those edges back. Restoring is not a separate case: the message is sent again on the way
                //CC: back and the unzoomed branch applies.
                auto* const p = reinterpret_cast<NCCALCSIZE_PARAMS*>(lp);
                const UINT dpi = GetDpiForWindow(m_hwnd);
                const int fx = GetSystemMetricsForDpi(SM_CXFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
                const int fy = GetSystemMetricsForDpi(SM_CYFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
                p->rgrc[0].left += fx;
                p->rgrc[0].right -= fx;
                p->rgrc[0].top += fy;
                p->rgrc[0].bottom -= fy;
            }
            return 0;
        }
        break;
    case WM_NCHITTEST:
    {
        //GK: Interesting. Why are we implementing this - I've really very rarely needed to do this manually myself?
        //CC: Because we removed the frame. Normally DefWindowProc answers WM_NCHITTEST -- it knows where the
        //CC: resize borders and title bar are, because it drew them. Once NCCALCSIZE claims the whole window
        //CC: as client area, none of that exists and the window is neither resizable nor draggable.
        //CC: So this hand-rolls it: 8 DIPs of resize border per edge and corner, HTCAPTION over the top strip
        //CC: so dragging and double-click-to-maximize work, HTCLIENT over the buttons so they stay clickable.
        //CC: This is the price of a custom title bar, and it is the same code in every app that has one.
        POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(m_hwnd, &pt);
        RECT rc{};
        GetClientRect(m_hwnd, &rc);
        const float scale{DipScale()};
        if (!IsZoomed(m_hwnd))
        {
            const int m = static_cast<int>(8 * scale);
            const bool left{pt.x < m}, right{pt.x >= rc.right - m};
            const bool top{pt.y < m}, bottom{pt.y >= rc.bottom - m};
            if (top && left) return HTTOPLEFT;
            if (top && right) return HTTOPRIGHT;
            if (bottom && left) return HTBOTTOMLEFT;
            if (bottom && right) return HTBOTTOMRIGHT;
            if (left) return HTLEFT;
            if (right) return HTRIGHT;
            if (top) return HTTOP;
            if (bottom) return HTBOTTOM;
        }
        const float xDip{pt.x / scale}, yDip{pt.y / scale}, wDip{rc.right / scale};
        if (yDip < kCaptionH)
            return CaptionButtonAt(xDip, yDip, wDip) >= 0 ? HTCLIENT : HTCAPTION;
        return HTCLIENT;
    }
    case WM_PAINT:
    {
        PAINTSTRUCT ps{};
        BeginPaint(m_hwnd, &ps);
        if (SUCCEEDED(EnsureRenderTarget()))
        {
            m_rt->BeginDraw();
            OnPaint(m_rt->GetSize());
            if (m_rt->EndDraw() == D2DERR_RECREATE_TARGET)
            {
                //GK: Presumably, D2DERR_RECREATE_TARGET means too-many-changes-to-draw-only-delta?
                //CC: No -- it means the render target is DEAD and everything made from it is invalid. Causes: GPU
                //CC: driver update, device reset/TDR, remote-desktop transitions, some adapter changes. Nothing to do
                //CC: with how much changed. The only remedy is to throw away every device-DEPENDENT object (render
                //CC: target, brushes) and rebuild -- which is what DiscardDeviceResources does, keeping the factories,
                //CC: which are device-INdependent. This handling is correct and complete; one of the better parts of
                //CC: this file.
                DiscardDeviceResources();
                InvalidateRect(m_hwnd, nullptr, FALSE);
            }
        }
        EndPaint(m_hwnd, &ps);
        return 0;
    }
    case WM_SIZE:
        if (m_rt)
        {
            if (m_rt->Resize(D2D1::SizeU(LOWORD(lp), HIWORD(lp))) == D2DERR_RECREATE_TARGET)
                DiscardDeviceResources();
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
        OnSize();
        return 0;
    case WM_DPICHANGED:
    {
        const RECT* const r{reinterpret_cast<RECT*>(lp)};
        SetWindowPos(m_hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        DiscardDeviceResources();
        InvalidateRect(m_hwnd, nullptr, FALSE);
        OnDpiChanged();
        return 0;
    }
    case WM_DESTROY:
        OnDestroy();
        ReleaseGraphics();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(m_hwnd, msg, wp, lp);
}
