#include "precomp.h"

// D2DWindow — reusable frameless Win32 + Direct2D/DirectWrite window base class.
// Handles WndProc trampoline, D2D lifecycle, NCCALCSIZE/NCHITTEST for frameless
// window behavior, custom-drawn caption buttons, DPI, and device-loss recovery.

#include "d2dwindow.h"

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

namespace
{
    constexpr float kCaptionH{40.0f};
    constexpr float kBtnW{46.0f};

    // Segoe Fluent Icons glyphs (PUA codepoints, numeric to avoid source-encoding issues).
    constexpr wchar_t kGlyphMin{0xE921};
    constexpr wchar_t kGlyphMax{0xE922};
    constexpr wchar_t kGlyphRestore{0xE923};
    constexpr wchar_t kGlyphClose{0xE8BB};

    bool Contains(const D2D1_RECT_F& r, float x, float y)
    {
        return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
    }

    struct CaptionButtons
    {
        D2D1_RECT_F min, max, close;
    };

    CaptionButtons CaptionButtonRects(float widthDip)
    {
        return CaptionButtons{
            D2D1::RectF(widthDip - 3 * kBtnW, 0.0f, widthDip - 2 * kBtnW, kCaptionH),
            D2D1::RectF(widthDip - 2 * kBtnW, 0.0f, widthDip - kBtnW, kCaptionH),
            D2D1::RectF(widthDip - kBtnW, 0.0f, widthDip, kCaptionH)};
    }

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
    if (self) return self->HandleMessage(msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// --- D2D lifecycle ---

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
    m_fmtCaption.reset();
    m_fmtGlyph.reset();
    if (m_dw)  { m_dw->Release();  m_dw = nullptr; }
    if (m_d2d) { m_d2d->Release(); m_d2d = nullptr; }
}

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

IDWriteTextFormat* ui::D2DWindow::MakeFormat(const wchar_t* family, float size,
                                              DWRITE_FONT_WEIGHT weight, bool vcenter, bool hcenter)
{
    IDWriteTextFormat* fmt{nullptr};
    if (FAILED(m_dw->CreateTextFormat(family, nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
                                      DWRITE_FONT_STRETCH_NORMAL, size, L"en-us", &fmt)))
    {
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

void ui::D2DWindow::DrawString(const std::wstring& s, IDWriteTextFormat* fmt,
                                const D2D1_RECT_F& box, const D2D1_COLOR_F& c)
{
    m_brush->SetColor(c);
    m_rt->DrawTextW(s.c_str(), static_cast<UINT32>(s.size()), fmt, box, m_brush,
                    D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

// --- Caption ---

void ui::D2DWindow::DrawCaption(const theme::ColorScheme& s, float widthDip,
                                 const wchar_t* title)
{
    DrawString(title, m_fmtCaption.get(),
             D2D1::RectF(14.0f, 0.0f, widthDip - 3 * kBtnW - 8.0f, kCaptionH), s.headerText);

    const CaptionButtons b = CaptionButtonRects(widthDip);
    const bool zoomed = IsZoomed(m_hwnd) != 0;
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
    const BOOL dwmDark{dark ? TRUE : FALSE};
    DwmSetWindowAttribute(m_hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dwmDark, sizeof(dwmDark));
}

// --- Caption interaction helpers ---

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
    const int cap = CaptionButtonAt(xDip, yDip, widthDip);
    if (cap != m_capHover) { m_capHover = cap; return true; }
    return false;
}

bool ui::D2DWindow::ResetCaptionTracking()
{
    m_tracking = false;
    const bool changed{m_capHover != -1};
    m_capHover = -1;
    return changed;
}

bool ui::D2DWindow::HandleCaptionClick(float xDip, float yDip, float widthDip)
{
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
    WNDCLASSEXW wc{};
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
    SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0,
                 SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
    ShowWindow(m_hwnd, showCmd);
    return true;
}

// --- Convenience ---

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
