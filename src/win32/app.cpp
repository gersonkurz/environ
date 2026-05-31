// environ — Win32 host entry point (Phase 1, read-only).
// Pure Win32 + Direct2D + DirectWrite. Frameless window with a custom-drawn title bar.
// Owns the window, D2D device resources, theme, and grid; loads live data from core
// EnvStore and forwards input.

#include <windows.h>
#include <windowsx.h>
#include <d2d1.h>
#include <d2d1helper.h>
#include <dwrite.h>
#include <dwmapi.h>
#include <format>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

#include "theme.h"
#include "grid.h"
#include "EnvStore.h"

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_SYSTEMBACKDROP_TYPE
#define DWMWA_SYSTEMBACKDROP_TYPE 38
#endif
#ifndef DWMSBT_MAINWINDOW
#define DWMSBT_MAINWINDOW 2
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

    HWND g_hwnd{nullptr};

    ID2D1Factory* g_d2d{nullptr};
    IDWriteFactory* g_dw{nullptr};
    ID2D1HwndRenderTarget* g_rt{nullptr};
    ID2D1SolidColorBrush* g_brush{nullptr};

    IDWriteTextFormat* g_fmtCaption{nullptr};
    IDWriteTextFormat* g_fmtSub{nullptr};
    IDWriteTextFormat* g_fmtName{nullptr};
    IDWriteTextFormat* g_fmtValue{nullptr};
    IDWriteTextFormat* g_fmtHeader{nullptr};
    IDWriteTextFormat* g_fmtGlyph{nullptr};

    theme::ThemeSet g_theme;
    ui::Grid g_grid;
    size_t g_userCount{0};
    size_t g_machineCount{0};
    bool g_elevated{false};
    bool g_tracking{false};
    int g_capHover{-1}; // 0 = min, 1 = max, 2 = close, -1 = none

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

    float DipScale(HWND hwnd) { return static_cast<float>(GetDpiForWindow(hwnd)) / 96.0f; }
    void Repaint(HWND hwnd, bool needed) { if (needed) InvalidateRect(hwnd, nullptr, FALSE); }

    void DiscardDeviceResources()
    {
        if (g_brush) { g_brush->Release(); g_brush = nullptr; }
        if (g_rt)    { g_rt->Release();    g_rt = nullptr; }
    }

    // Full teardown at shutdown: device resources, all text formats, then the factories.
    void ReleaseGraphics()
    {
        DiscardDeviceResources();
        for (IDWriteTextFormat** fmt : {&g_fmtCaption, &g_fmtSub, &g_fmtName, &g_fmtValue, &g_fmtHeader, &g_fmtGlyph})
        {
            if (*fmt) { (*fmt)->Release(); *fmt = nullptr; }
        }
        if (g_dw)  { g_dw->Release();  g_dw = nullptr; }
        if (g_d2d) { g_d2d->Release(); g_d2d = nullptr; }
    }

    HRESULT EnsureRenderTarget(HWND hwnd)
    {
        if (g_rt) return S_OK;
        RECT rc{};
        GetClientRect(hwnd, &rc);
        const HRESULT hr = g_d2d->CreateHwndRenderTarget(
            D2D1::RenderTargetProperties(),
            D2D1::HwndRenderTargetProperties(hwnd, D2D1::SizeU(
                static_cast<UINT32>(rc.right - rc.left),
                static_cast<UINT32>(rc.bottom - rc.top))),
            &g_rt);
        if (FAILED(hr)) return hr;
        const UINT dpi = GetDpiForWindow(hwnd);
        g_rt->SetDpi(static_cast<float>(dpi), static_cast<float>(dpi));
        return g_rt->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), &g_brush);
    }

    void DrawString(const std::wstring& s, IDWriteTextFormat* fmt,
                  const D2D1_RECT_F& box, const D2D1_COLOR_F& c)
    {
        g_brush->SetColor(c);
        g_rt->DrawTextW(s.c_str(), static_cast<UINT32>(s.size()), fmt, box, g_brush,
                        D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }

    void DrawCaption(const theme::ColorScheme& s, float widthDip)
    {
        DrawString(L"environ", g_fmtCaption,
                 D2D1::RectF(14.0f, 0.0f, widthDip - 3 * kBtnW - 8.0f, kCaptionH), s.headerText);

        const CaptionButtons b = CaptionButtonRects(widthDip);
        const bool zoomed = IsZoomed(g_hwnd) != 0;
        const struct { D2D1_RECT_F rect; int idx; wchar_t glyph; } buttons[]{
            {b.min, 0, kGlyphMin},
            {b.max, 1, zoomed ? kGlyphRestore : kGlyphMax},
            {b.close, 2, kGlyphClose}};

        for (const auto& btn : buttons)
        {
            const bool hover = (g_capHover == btn.idx);
            const bool isClose = (btn.idx == 2);
            if (hover)
            {
                g_brush->SetColor(isClose ? s.captionCloseHover.fill : s.rowHover.fill);
                g_rt->FillRectangle(btn.rect, g_brush);
            }
            const D2D1_COLOR_F glyphColor = (hover && isClose) ? s.captionCloseHover.text : s.headerText;
            g_brush->SetColor(glyphColor);
            g_rt->DrawTextW(&btn.glyph, 1, g_fmtGlyph, btn.rect, g_brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
    }

    void Paint(HWND hwnd)
    {
        if (FAILED(EnsureRenderTarget(hwnd))) return;
        const theme::ColorScheme& s = g_theme.Current();

        g_rt->BeginDraw();
        g_rt->Clear(s.windowBg);

        const D2D1_SIZE_F sz = g_rt->GetSize();
        const float pad = 16.0f;

        DrawCaption(s, sz.width);

        g_grid.Paint(g_rt, g_brush, ui::GridFonts{g_fmtName, g_fmtValue, g_fmtHeader}, s,
                     D2D1::RectF(pad, kCaptionH + 8.0f, sz.width - pad, sz.height - 34.0f));

        const std::wstring footer = std::format(
            L"{}   \x2022   {} user, {} machine{}   \x2022   F1 dark  F2 light  F3 blue",
            std::wstring(s.name.begin(), s.name.end()), g_userCount, g_machineCount,
            g_elevated ? L"" : L"  (machine read-only)");
        DrawString(footer, g_fmtSub,
                 D2D1::RectF(pad, sz.height - 28.0f, sz.width - pad, sz.height - 8.0f), s.headerSubtext);

        if (g_rt->EndDraw() == D2DERR_RECREATE_TARGET)
        {
            DiscardDeviceResources();
            InvalidateRect(hwnd, nullptr, FALSE); // re-paint will recreate the target
        }
    }

    void ApplyTitleBar(HWND hwnd)
    {
        const BOOL dark{g_theme.Current().darkTitleBar ? TRUE : FALSE};
        DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    }

    IDWriteTextFormat* MakeFormat(const wchar_t* family, float size, DWRITE_FONT_WEIGHT weight,
                                  bool vcenter, bool hcenter = false)
    {
        IDWriteTextFormat* fmt{nullptr};
        if (FAILED(g_dw->CreateTextFormat(family, nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
                                          DWRITE_FONT_STRETCH_NORMAL, size, L"en-us", &fmt)))
        {
            std::string narrow;
            for (const wchar_t* c = family; *c; ++c) narrow.push_back(static_cast<char>(*c));
            spdlog::error("CreateTextFormat failed for '{}'", narrow); // family names are ASCII
            return nullptr;
        }
        fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        fmt->SetParagraphAlignment(vcenter ? DWRITE_PARAGRAPH_ALIGNMENT_CENTER
                                           : DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        if (hcenter) fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        return fmt;
    }

    LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
    {
        switch (msg)
        {
        case WM_NCCALCSIZE:
            if (wp)
            {
                if (IsZoomed(hwnd))
                {
                    auto* const p = reinterpret_cast<NCCALCSIZE_PARAMS*>(lp);
                    const UINT dpi = GetDpiForWindow(hwnd);
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
            ScreenToClient(hwnd, &pt);
            RECT rc{};
            GetClientRect(hwnd, &rc);
            const float scale = DipScale(hwnd);
            if (!IsZoomed(hwnd))
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
        case WM_KEYDOWN:
        {
            const char* want{nullptr};
            if (wp == VK_F1) want = "dark";
            else if (wp == VK_F2) want = "light";
            else if (wp == VK_F3) want = "blue";
            if (want)
            {
                if (g_theme.SelectByName(want)) { ApplyTitleBar(hwnd); InvalidateRect(hwnd, nullptr, FALSE); }
            }
            else
            {
                Repaint(hwnd, g_grid.OnKey(static_cast<int>(wp)));
            }
            return 0;
        }
        case WM_MOUSEMOVE:
        {
            if (!g_tracking)
            {
                TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
                TrackMouseEvent(&tme);
                g_tracking = true;
            }
            const float scale = DipScale(hwnd);
            RECT rc{};
            GetClientRect(hwnd, &rc);
            const float xDip{GET_X_LPARAM(lp) / scale}, yDip{GET_Y_LPARAM(lp) / scale};
            const int cap = CaptionButtonAt(xDip, yDip, rc.right / scale);
            bool need{false};
            if (cap != g_capHover) { g_capHover = cap; need = true; }
            need |= g_grid.OnMouseMove(xDip, yDip);
            Repaint(hwnd, need);
            return 0;
        }
        case WM_MOUSELEAVE:
        {
            g_tracking = false;
            bool need{g_capHover != -1};
            g_capHover = -1;
            need |= g_grid.OnMouseLeave();
            Repaint(hwnd, need);
            return 0;
        }
        case WM_LBUTTONDOWN:
        {
            const float scale = DipScale(hwnd);
            RECT rc{};
            GetClientRect(hwnd, &rc);
            const float xDip{GET_X_LPARAM(lp) / scale}, yDip{GET_Y_LPARAM(lp) / scale};
            const int cap = CaptionButtonAt(xDip, yDip, rc.right / scale);
            if (cap == 0) { ShowWindow(hwnd, SW_MINIMIZE); return 0; }
            if (cap == 1) { ShowWindow(hwnd, IsZoomed(hwnd) ? SW_RESTORE : SW_MAXIMIZE); return 0; }
            if (cap == 2) { PostMessageW(hwnd, WM_CLOSE, 0, 0); return 0; }
            SetFocus(hwnd);
            SetCapture(hwnd);
            Repaint(hwnd, g_grid.OnLButtonDown(xDip, yDip));
            return 0;
        }
        case WM_LBUTTONUP:
            ReleaseCapture();
            Repaint(hwnd, g_grid.OnLButtonUp());
            return 0;
        case WM_MOUSEWHEEL:
            Repaint(hwnd, g_grid.OnWheel(GET_WHEEL_DELTA_WPARAM(wp)));
            return 0;
        case WM_PAINT:
        {
            PAINTSTRUCT ps{};
            BeginPaint(hwnd, &ps);
            Paint(hwnd);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_SIZE:
            if (g_rt)
            {
                if (g_rt->Resize(D2D1::SizeU(LOWORD(lp), HIWORD(lp))) == D2DERR_RECREATE_TARGET)
                    DiscardDeviceResources();
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        case WM_DPICHANGED:
        {
            const RECT* const r = reinterpret_cast<RECT*>(lp);
            SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            DiscardDeviceResources();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_DESTROY:
            ReleaseGraphics();
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    std::wstring ThemePathBesideExe()
    {
        wchar_t path[MAX_PATH]{};
        const DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
        std::wstring dir{path, n};
        const size_t slash = dir.find_last_of(L"\\/");
        if (slash != std::wstring::npos) dir.resize(slash);
        return dir + L"\\theme.toml";
    }

    void LoadData()
    {
        using namespace Environ::core;
        g_elevated = is_elevated();
        std::vector<EnvVariable> userVars = read_variables(Scope::User);
        std::vector<EnvVariable> machineVars = read_variables(Scope::Machine);
        expand_and_validate(userVars);
        expand_and_validate(machineVars);
        detect_duplicates(userVars, machineVars);
        g_userCount = userVars.size();
        g_machineCount = machineVars.size();
        g_grid.SetData(userVars, machineVars, g_elevated);
    }
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int nCmdShow)
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &g_d2d)))
    {
        spdlog::error("D2D1CreateFactory failed");
        return 1;
    }
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(&g_dw))))
    {
        spdlog::error("DWriteCreateFactory failed");
        return 1;
    }

    g_theme.LoadOrDefault(ThemePathBesideExe());

    g_fmtCaption = MakeFormat(L"Segoe UI Variable Text", 13.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, true);
    g_fmtSub     = MakeFormat(L"Segoe UI Variable Text", 12.0f, DWRITE_FONT_WEIGHT_NORMAL, false);
    g_fmtName    = MakeFormat(L"Segoe UI Variable Text", 14.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, true);
    g_fmtValue   = MakeFormat(L"Segoe UI Variable Small", 12.0f, DWRITE_FONT_WEIGHT_NORMAL, true);
    g_fmtHeader  = MakeFormat(L"Segoe UI Variable Small", 11.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, true);
    g_fmtGlyph   = MakeFormat(L"Segoe Fluent Icons", 10.0f, DWRITE_FONT_WEIGHT_NORMAL, true, true);
    if (!g_fmtCaption || !g_fmtSub || !g_fmtName || !g_fmtValue || !g_fmtHeader || !g_fmtGlyph)
    {
        spdlog::error("text format creation failed");
        return 1;
    }

    LoadData();

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"EnvironWin32Host";
    RegisterClassExW(&wc);

    g_hwnd = CreateWindowExW(
        0, wc.lpszClassName, L"environ", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 860, 620, nullptr, nullptr, hInst, nullptr);
    if (!g_hwnd)
    {
        spdlog::error("CreateWindowExW failed");
        return 1;
    }

    ApplyTitleBar(g_hwnd);
    const int backdrop{DWMSBT_MAINWINDOW};
    DwmSetWindowAttribute(g_hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop));

    // Re-run NCCALCSIZE so the frameless client takes effect.
    SetWindowPos(g_hwnd, nullptr, 0, 0, 0, 0,
                 SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
    ShowWindow(g_hwnd, nCmdShow);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}
