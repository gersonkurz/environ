#pragma once

#include "theme.h"

namespace ui {

// Move-only RAII wrapper for IDWriteTextFormat*.  Releases the COM object on
// destruction or reassignment.  Non-owning call sites (GridFonts, DrawString
// params) keep using raw IDWriteTextFormat* via .get().
/*

GK: that was a really typical unhelpful comment. 
//CC: IDWriteTextFormat describes HOW text is drawn: family, size, weight, style, stretch and locale,
//CC: all fixed at creation -- plus paragraph settings (wrapping, alignment) which DO have setters,
//CC: and which MakeFormat calls. So 'immutable', as I first wrote, is only half true.
//CC: It is not the text, and not the laid-out result. It is the 'font + alignment' handle you pass
//CC: to DrawText. Its sibling IDWriteTextLayout holds a specific string with per-range overrides,
//CC: which is what DrawCaption reaches for when it wants the subtitle lighter than the title.
//CC: On shape: agreed, not one header per class. The meaningful unit is TYPOGRAPHY -- the app's set
//CC: of formats (caption, name, value, header, button, mono, glyph), how they are built, how they
//CC: scale with zoom and font scale, and how they are rebuilt on DPI change. That is one header and
//CC: a real ancusi concept; a wrapper class on its own is not.

- What is IDWriteTextFormat, and why are we even using it? 
- Obviously it should be in a separate header; the question is what shape that header should have.
  I mean, you know by now the depth of my hate for Java patterns, and we don't want
  to create a header for each class simply because that's what Java does. We need to understand
  the meaning behind this class.

*/
class TextFormat //GK: should be declared `final`, no?
//CC: Yes.
{
public:
    TextFormat() = default;
    ~TextFormat() { reset(); }

    TextFormat(const TextFormat&) = delete;
    TextFormat& operator=(const TextFormat&) = delete;

    /*GK: we can declare move & copy assignment, but why do we do it - just because we can?
    because, if I see right, nowhere in the code are we using any of this functionality?
    */
    //CC: Copy is deleted, which is right -- two owners would double-release. Move is dead code: verified,
    //CC: there is no std::move of a TextFormat and no container of them anywhere. All eight instances
    //CC: (two here, six in MainWindow) are plain members assigned from MakeFormat.
    //CC: Note the connection to your next comment: if MakeFormat RETURNED a TextFormat by value instead
    //CC: of a raw pointer, the move constructor would stop being dead and the raw-pointer assignment
    //CC: operator could disappear. One change fixes both.
    TextFormat(TextFormat&& o) noexcept : m_ptr{o.m_ptr} { o.m_ptr = nullptr; }
    TextFormat& operator=(TextFormat&& o) noexcept
    {
        if (this != &o) { reset(); m_ptr = o.m_ptr; o.m_ptr = nullptr; }
        return *this;
    }

    // Take ownership from MakeFormat's raw return.
    /*GK: I look at places where this is used, like 
    
    m_fmtCaption = MakeFormat(ui, z(13.0f), DWRITE_FONT_WEIGHT_SEMI_BOLD, true);
    
    and I wonder why this is not hidden in a make_format() method. Do we forsee
    anyone really needing to pass a pointer constructed out of nowhere?    
    */
    //CC: Agreed. `m_fmtCaption = MakeFormat(...)` leaks the raw-pointer handoff into every call site, and
    //CC: the only thing keeping it safe is that nobody currently writes anything else. A make_format()
    //CC: returning TextFormat by value removes this operator, removes the raw pointer from the API, and
    //CC: makes the move constructor earn its place. See the note above.
    TextFormat& operator=(IDWriteTextFormat* p) noexcept
    {
        reset(); m_ptr = p; return *this;
    }

    explicit operator bool() const noexcept { return m_ptr != nullptr; }
    IDWriteTextFormat* get() const noexcept { return m_ptr; }

    //GK: is this in use?
    //CC: Yes, eight call sites -- but I had the second group wrong. Two are here in ReleaseGraphics; the
    //CC: other six are in MainWindow::OnDestroy, not CreateFonts. CreateFonts does not reset first: it
    //CC: assigns, and the assignment operator releases the old format.
    //CC: Also: these formats are NOT rebuilt on a DPI change. They are sized in DIPs, so they do not
    //CC: need to be -- which is also why they can be created before the window exists.
    void reset() noexcept { if (m_ptr) { m_ptr->Release(); m_ptr = nullptr; } }

private:
    IDWriteTextFormat* m_ptr{nullptr};
};

class D2DWindow
{
    PNQ_DECLARE_NON_COPYABLE(D2DWindow)

public:
    D2DWindow() = default;
    virtual ~D2DWindow();

    //GK: general comment: its a base class, but "everything is protected". 
    //GK: my gut instinct would be: make everything private, to isolate what really needs to be exposed to subclasses.
    //GK: better yet: make it composable, but a man can dream, can't he?
    //CC: Agreed, and worth noting how far it goes: the entire PUBLIC surface of this class is its
    //CC: destructor. Even CreateD2DWindow is protected, with MainWindow::Create as the public wrapper.
    //CC: So 'protected' here means 'no access control at all, between these two classes'.
    //CC: On composition: that is the right dream, and the caption is the argument for it. A caption is not
    //CC: a property of 'a window'; it is something a window can contain. Nothing here needs inheritance
    //CC: except the paint/size/dpi/destroy hooks, and those are four callbacks.
protected:
    // Window creation
    // GK: ugly naming. We're creating a window, not "D2DWindow". It's already in the class name, isn't it? Also, no sensible defaults for any of these?
    //CC: Agreed -- create() inside the class. On defaults: className and title have to come from the app,
    //CC: but x/y/w/h have a natural default in CW_USEDEFAULT and showCmd in SW_SHOWDEFAULT. Today all
    //CC: seven are passed explicitly by the single caller.
    bool CreateD2DWindow(const wchar_t* className, const wchar_t* title,
                         int x, int y, int w, int h, int showCmd);

    // D2D lifecycle
    /*GK: these are lifecycle methods, but apparently no clear convention
    for how to treat errors. bool, void, and HRESULT. I think going forward
    we should have a clear policy - not right now, but for our library */
    //CC: Confirmed: four functions, three conventions -- bool, void, HRESULT. CLAUDE.md says we are
    //CC: exception-free and may use HRESULT, return codes or std::optional, but never says which when, so
    //CC: this is not even a violation -- it is an unwritten rule. Worth writing down for ancusi: HRESULT
    //CC: when a COM call's code is the useful information, bool when the only question is 'did it work',
    //CC: void only when failure is impossible.
    bool    InitGraphics();
    void    DiscardDeviceResources();
    void    ReleaseGraphics();
    HRESULT EnsureRenderTarget();

    //GK: said that above: this is the wrong shape for a library. For now, it creates 
    //CC: Yes -- concretely: it makes a format, which is family + size + weight + alignment; it hardcodes
    //CC: style to NORMAL so italic is unreachable; and it returns a raw pointer the caller must remember
    //CC: to hand to a TextFormat. See the typography note at the top of this file.
    // the - format? Font & style, I guess?
    IDWriteTextFormat* MakeFormat(const wchar_t* family, float size,
                                  DWRITE_FONT_WEIGHT weight, bool vcenter,
                                  bool hcenter = false);
        
    void DrawString(const std::wstring& s, IDWriteTextFormat* fmt,
                    const D2D1_RECT_F& box, const D2D1_COLOR_F& c);

    // Caption. If `subtitle` is non-null it is appended after `title` in normal weight
    // (the title stays at the format's bolder weight), so the two read as distinct.
    //GK: this *screams* for a higher-level "rich text" abstraction
    //CC: It does, and the implementation proves it: DrawCaption builds an IDWriteTextLayout by hand, sets
    //CC: a weight range on the subtitle, and falls back to plain DrawString if that fails. That is a
    //CC: rich-text abstraction, hand-rolled once, inline, with a silent fallback.
    void DrawCaption(const theme::ColorScheme& s, float widthDip,
                     const wchar_t* title, float titleLeftInset = 0.0f,
                     const wchar_t* subtitle = nullptr);
    void ApplyDarkTitleBar(bool dark);

    // Caption interaction helpers (for subclass message handlers)
    //GK: I find that comment strange - if its for a subclass, why is it not virtual?
    //CC: Because they are helpers the subclass calls, not hooks it implements -- the comment is what is
    //CC: wrong, not the absence of virtual. See the longer note at HandleCaptionClick in the .cpp: the
    //CC: real problem is that the base owns the caption while the subclass puts its own buttons in it.
    void SetupMouseTracking();
    bool UpdateCaptionHover(float xDip, float yDip, float widthDip);
    bool ResetCaptionTracking();
    bool HandleCaptionClick(float xDip, float yDip, float widthDip);

    // Convenience
    float DipScale() const;
    void  Repaint(bool needed);

    // Message dispatch
    static LRESULT CALLBACK StaticWndProc(HWND, UINT, WPARAM, LPARAM);
    virtual LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp);

    // Virtual hooks
    virtual void OnPaint(const D2D1_SIZE_F& sz) = 0;
    virtual void OnSize() {} //GK: Why no params?
    //CC: No reason, and it is inconsistent: OnPaint receives the size, OnSize does not, so every override
    //CC: has to go and ask (GetClientRect, or m_rt->GetSize()). Same for OnDpiChanged, which does not
    //CC: receive the new DPI that WM_DPICHANGED carried.
    virtual void OnDpiChanged() {}
    virtual void OnDestroy() {}

    // === Members ===
    HINSTANCE m_hInst{nullptr};
    int       m_nCmdShow{SW_SHOWDEFAULT};
    HWND      m_hwnd{nullptr};

    ID2D1Factory*          m_d2d{nullptr};
    IDWriteFactory*        m_dw{nullptr};
    ID2D1HwndRenderTarget* m_rt{nullptr};
    ID2D1SolidColorBrush*  m_brush{nullptr};

    TextFormat m_fmtCaption;
    TextFormat m_fmtGlyph;

    bool m_tracking{false};
    int  m_capHover{-1};
};

/*GK: LESSONS LEARNT

Next to the usual things (like logging, error handling etc):

- Rich-Text render abstraction will be a desirable. Ideally markdown, but open to alternative suggestions. 
- Isolate out the title-bar drawing logic into a separate class
- More consistency when naming things (one of the three hard problems, I know)

*/

} // namespace ui
