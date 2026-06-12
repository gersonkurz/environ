// environ — Win32 host entry point (Phase 2: read + inline editing, no registry write yet).
// Pure Win32 + Direct2D + DirectWrite. Frameless window with a custom-drawn title bar.
// Owns the window, D2D device resources, theme, grid, and the inline EDIT editor; loads
// live data from core EnvStore and forwards input.

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <d2d1.h>
#include <d2d1helper.h>
#include <dwrite.h>
#include <dwmapi.h>
#include <algorithm>
#include <format>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

#include "theme.h"
#include "grid.h"
#include "EnvStore.h"
#include "EnvWriter.h"

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

    // Posted by the editor subclass back to the window. wParam: 0 = cancel, 1 = commit,
    // 2 = commit + move (Tab); lParam (Tab only): 1 = Shift (move up), 0 = move down.
    constexpr UINT WM_APP_EDIT_END{WM_APP + 1};

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
    IDWriteTextFormat* g_fmtButton{nullptr}; // centered button label

    theme::ThemeSet g_theme;
    ui::Grid g_grid;
    size_t g_userCount{0};
    size_t g_machineCount{0};
    bool g_elevated{false};
    bool g_tracking{false};
    int g_capHover{-1}; // 0 = min, 1 = max, 2 = close, -1 = none

    HWND g_edit{nullptr};       // inline cell editor (skinned standard EDIT)
    HFONT g_editFont{nullptr};
    HBRUSH g_editBrush{nullptr}; // for WM_CTLCOLOREDIT

    // Apply-review modal state (Phase 3B). When open, the grid is inert and the panel owns input.
    bool g_reviewOpen{false};
    int g_reviewHover{-1}; // 0 = Cancel, 1 = Apply, -1 = none
    bool g_eatNextDblClk{false}; // swallow the trailing dblclk of a modal-closing click
    std::vector<Environ::core::EnvChange> g_reviewUser;
    std::vector<Environ::core::EnvChange> g_reviewMachine;
    std::vector<Environ::core::EnvVariable> g_reviewCurUser;
    std::vector<Environ::core::EnvVariable> g_reviewCurMachine;

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

    COLORREF ToColorRef(const D2D1_COLOR_F& c)
    {
        return RGB(static_cast<BYTE>(c.r * 255.0f), static_cast<BYTE>(c.g * 255.0f), static_cast<BYTE>(c.b * 255.0f));
    }

    // The editor brush only changes with the theme; the font only with DPI. Cache both and
    // refresh on those events rather than rebuilding per WM_CTLCOLOREDIT / per edit.
    void RefreshEditBrush()
    {
        if (g_editBrush) { DeleteObject(g_editBrush); g_editBrush = nullptr; }
        g_editBrush = CreateSolidBrush(ToColorRef(g_theme.Current().edit.fill));
    }

    void RefreshEditFont(HWND hwnd)
    {
        if (!g_edit) return;
        if (g_editFont) DeleteObject(g_editFont);
        const int h{static_cast<int>(12.0f * DipScale(hwnd))};
        g_editFont = CreateFontW(-h, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI Variable Text");
        SendMessageW(g_edit, WM_SETFONT, reinterpret_cast<WPARAM>(g_editFont), TRUE);
    }

    void DiscardDeviceResources()
    {
        if (g_brush) { g_brush->Release(); g_brush = nullptr; }
        if (g_rt)    { g_rt->Release();    g_rt = nullptr; }
    }

    // Full teardown at shutdown: device resources, all text formats, then the factories.
    void ReleaseGraphics()
    {
        DiscardDeviceResources();
        for (IDWriteTextFormat** fmt : {&g_fmtCaption, &g_fmtSub, &g_fmtName, &g_fmtValue, &g_fmtHeader, &g_fmtGlyph, &g_fmtButton})
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

    struct ReviewGeom
    {
        D2D1_RECT_F card;
        D2D1_RECT_F list;
        D2D1_RECT_F cancelBtn;
        D2D1_RECT_F applyBtn;
    };

    ReviewGeom ReviewLayout(const D2D1_SIZE_F& sz)
    {
        const float cw{std::min(520.0f, sz.width - 80.0f)};
        const float pad{20.0f};
        const float titleH{36.0f};
        const float lineH{22.0f};
        const float btnRow{56.0f};
        const int lineCount{
            (g_reviewUser.empty() ? 0 : 1 + static_cast<int>(g_reviewUser.size())) +
            (g_reviewMachine.empty() ? 0 : 1 + static_cast<int>(g_reviewMachine.size()))};
        const float listH{std::min(static_cast<float>(lineCount) * lineH, sz.height * 0.5f)};
        const float ch{pad + titleH + listH + btnRow + pad};
        const float cx{(sz.width - cw) / 2.0f};
        const float cy{(sz.height - ch) / 2.0f};

        ReviewGeom g{};
        g.card = D2D1::RectF(cx, cy, cx + cw, cy + ch);
        g.list = D2D1::RectF(cx + pad, cy + pad + titleH, cx + cw - pad, cy + pad + titleH + listH);
        constexpr float bw{96.0f};
        constexpr float bh{34.0f};
        const float by{g.card.bottom - pad - bh};
        g.applyBtn = D2D1::RectF(g.card.right - pad - bw, by, g.card.right - pad, by + bh);
        g.cancelBtn = D2D1::RectF(g.applyBtn.left - 12.0f - bw, by, g.applyBtn.left - 12.0f, by + bh);
        return g;
    }

    void DrawReviewButton(const D2D1_RECT_F& r, const wchar_t* label, bool primary, bool hover,
                          const theme::ColorScheme& s)
    {
        g_brush->SetColor(primary ? s.accent : (hover ? s.rowSelected.fill : s.rowHover.fill));
        g_rt->FillRoundedRectangle(D2D1::RoundedRect(r, 6.0f, 6.0f), g_brush);
        if (!primary)
        {
            g_brush->SetColor(s.card.border);
            g_rt->DrawRoundedRectangle(D2D1::RoundedRect(r, 6.0f, 6.0f), g_brush, 1.0f);
        }
        g_brush->SetColor(primary ? s.accentText : s.headerText);
        g_rt->DrawTextW(label, static_cast<UINT32>(wcslen(label)), g_fmtButton, r, g_brush,
                        D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }

    void PaintReview(const theme::ColorScheme& s, const D2D1_SIZE_F& sz)
    {
        const ReviewGeom g{ReviewLayout(sz)};

        g_brush->SetColor(s.scrim);
        g_rt->FillRectangle(D2D1::RectF(0.0f, 0.0f, sz.width, sz.height), g_brush);

        g_brush->SetColor(s.card.fill);
        g_rt->FillRoundedRectangle(D2D1::RoundedRect(g.card, 10.0f, 10.0f), g_brush);
        g_brush->SetColor(s.card.border);
        g_rt->DrawRoundedRectangle(D2D1::RoundedRect(g.card, 10.0f, 10.0f), g_brush, 1.0f);

        DrawString(L"Apply changes?", g_fmtName,
                   D2D1::RectF(g.card.left + 20.0f, g.card.top + 12.0f, g.card.right - 20.0f, g.card.top + 44.0f),
                   s.headerText);

        g_rt->PushAxisAlignedClip(g.list, D2D1_ANTIALIAS_MODE_ALIASED);
        float y{g.list.top};
        const auto group = [&](const wchar_t* label, const std::vector<Environ::core::EnvChange>& changes) {
            if (changes.empty()) return;
            DrawString(label, g_fmtHeader, D2D1::RectF(g.list.left, y, g.list.right, y + 22.0f), s.headerSubtext);
            y += 22.0f;
            for (const Environ::core::EnvChange& c : changes)
            {
                DrawString(L"    " + c.describe(), g_fmtValue,
                           D2D1::RectF(g.list.left, y, g.list.right, y + 22.0f), s.headerText);
                y += 22.0f;
            }
        };
        group(L"USER", g_reviewUser);
        group(L"MACHINE", g_reviewMachine);
        g_rt->PopAxisAlignedClip();

        DrawReviewButton(g.cancelBtn, L"Cancel", false, g_reviewHover == 0, s);
        DrawReviewButton(g.applyBtn, L"Apply", true, g_reviewHover == 1, s);
    }

    void Paint(HWND hwnd)
    {
        if (FAILED(EnsureRenderTarget(hwnd))) return;
        const theme::ColorScheme& s = g_theme.Current();

        g_rt->BeginDraw();
        g_rt->Clear(s.windowBg);

        const D2D1_SIZE_F sz{g_rt->GetSize()};
        const float pad{16.0f};

        DrawCaption(s, sz.width);

        g_grid.Paint(g_rt, g_brush, ui::GridFonts{g_fmtName, g_fmtValue, g_fmtHeader}, s,
                     D2D1::RectF(pad, kCaptionH + 8.0f, sz.width - pad, sz.height - 34.0f));

        const std::wstring footer = std::format(
            L"{}   \x2022   {} user, {} machine{}   \x2022   Ctrl+S apply   \x2022   F1 dark  F2 light  F3 blue",
            std::wstring(s.name.begin(), s.name.end()), g_userCount, g_machineCount,
            g_elevated ? L"" : L"  (machine read-only)");
        DrawString(footer, g_fmtSub,
                 D2D1::RectF(pad, sz.height - 28.0f, sz.width - pad, sz.height - 8.0f), s.headerSubtext);

        if (g_reviewOpen) PaintReview(s, sz);

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

    // Subclass for the inline EDIT: intercept Enter/Esc/Tab (which a bare EDIT ignores or
    // beeps on) and hand control back to the window via a posted message.
    LRESULT CALLBACK EditSubclass(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR ref)
    {
        HWND parent{reinterpret_cast<HWND>(ref)};
        if (msg == WM_KEYDOWN)
        {
            if (wp == VK_RETURN) { PostMessageW(parent, WM_APP_EDIT_END, 1, 0); return 0; }
            if (wp == VK_ESCAPE) { PostMessageW(parent, WM_APP_EDIT_END, 0, 0); return 0; }
            if (wp == VK_TAB)    { PostMessageW(parent, WM_APP_EDIT_END, 2, (GetKeyState(VK_SHIFT) < 0) ? 1 : 0); return 0; }
            if (wp == VK_F1 || wp == VK_F2 || wp == VK_F3)
            {
                PostMessageW(parent, WM_APP_EDIT_END, 1, 0); // commit, then let the parent switch theme
                PostMessageW(parent, WM_KEYDOWN, wp, lp);
                return 0;
            }
        }
        else if (msg == WM_CHAR && (wp == VK_RETURN || wp == VK_ESCAPE || wp == VK_TAB))
        {
            return 0; // swallow to avoid the EDIT beep
        }
        return DefSubclassProc(h, msg, wp, lp);
    }

    void EnsureEditControl(HWND hwnd)
    {
        if (g_edit) return;
        g_edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL, 0, 0, 0, 0,
                                 hwnd, nullptr,
                                 reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE)), nullptr);
        if (!g_edit)
        {
            spdlog::error("inline edit control creation failed");
            return;
        }
        if (!SetWindowSubclass(g_edit, EditSubclass, 1, reinterpret_cast<DWORD_PTR>(hwnd)))
        {
            spdlog::error("SetWindowSubclass for edit control failed");
            DestroyWindow(g_edit);
            g_edit = nullptr;
            return;
        }
        RefreshEditFont(hwnd);
        SendMessageW(g_edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);
    }

    void EndEdit(HWND hwnd, bool commit)
    {
        if (!g_edit || !IsWindowVisible(g_edit)) return;
        if (commit)
        {
            const int len{GetWindowTextLengthW(g_edit)};
            std::wstring text(static_cast<size_t>(len) + 1, L'\0'); // +1 writable slot for the terminator
            const int copied{GetWindowTextW(g_edit, text.data(), len + 1)};
            text.resize(static_cast<size_t>(copied));
            g_grid.CommitEdit(text);
        }
        else
        {
            g_grid.CancelEdit();
        }
        ShowWindow(g_edit, SW_HIDE);
        SetFocus(hwnd);
        InvalidateRect(hwnd, nullptr, FALSE);
    }

    // Place the editor over the target cell: one line tall, centered, themed font.
    void PositionEditor(HWND hwnd, const ui::Grid::EditTarget& target)
    {
        const float scale{DipScale(hwnd)};
        const auto px = [scale](float dip) { return static_cast<int>(dip * scale); };
        const D2D1_RECT_F& c{target.cell};

        // Font + margins are set once in EnsureEditControl (and on DPI change). Here we only
        // size and place the control: a single-line EDIT top-aligns its text, so make it
        // exactly one line tall (from the font metrics) and center that in the cell.
        TEXTMETRICW tm{};
        if (const HDC dc{GetDC(g_edit)})
        {
            const HGDIOBJ prev{SelectObject(dc, g_editFont)};
            GetTextMetricsW(dc, &tm);
            SelectObject(dc, prev);
            ReleaseDC(g_edit, dc);
        }
        const int cellTop{px(c.top)};
        const int cellH{px(c.bottom) - cellTop};
        const int editH{tm.tmHeight > 0 ? static_cast<int>(tm.tmHeight) : px(16.0f)};
        MoveWindow(g_edit, px(c.left), cellTop + (cellH - editH) / 2,
                   px(c.right) - px(c.left), editH, FALSE);

        SetWindowTextW(g_edit, target.text.c_str());
        ShowWindow(g_edit, SW_SHOW);
        SetFocus(g_edit);
        SendMessageW(g_edit, EM_SETSEL, 0, -1);
        InvalidateRect(hwnd, nullptr, FALSE);
    }

    void BeginEditFromGrid(HWND hwnd)
    {
        if (!g_grid.SelectionEditable()) return; // nothing to edit; don't even create the control
        EnsureEditControl(hwnd);
        if (!g_edit) return; // creation failed; don't leave the grid stuck in editing state
        if (const auto target{g_grid.BeginEdit()}) PositionEditor(hwnd, *target);
    }

    void BeginEditAt(HWND hwnd, float x, float y)
    {
        EnsureEditControl(hwnd);
        if (!g_edit) return;
        const auto target{g_grid.BeginEditAt(x, y)};
        InvalidateRect(hwnd, nullptr, FALSE); // selection may have moved even if not editable
        if (target) PositionEditor(hwnd, *target);
    }

    void LoadData(); // defined below; re-reads the registry into the grid

    // Phase 3: preview the pending edits, apply on confirm (HKCU always; HKLM only when
    // elevated — handled by the core), then reload so the grid shows the saved state.
    void SaveChanges(HWND hwnd)
    {
        using namespace Environ::core;
        if (g_grid.IsEditing()) EndEdit(hwnd, true);
        if (!g_grid.HasChanges())
        {
            MessageBoxW(hwnd, L"No changes to apply.", L"environ", MB_OK | MB_ICONINFORMATION);
            return;
        }

        g_reviewCurUser = g_grid.CurrentVars(Scope::User);
        g_reviewCurMachine = g_grid.CurrentVars(Scope::Machine);
        g_reviewUser = compute_diff(g_grid.OriginalVars(Scope::User), g_reviewCurUser);
        g_reviewMachine = compute_diff(g_grid.OriginalVars(Scope::Machine), g_reviewCurMachine);
        if (g_reviewUser.empty() && g_reviewMachine.empty())
        {
            // Edits exist per-row but net to the original values (e.g. edited back).
            MessageBoxW(hwnd, L"No effective changes to apply.", L"environ", MB_OK | MB_ICONINFORMATION);
            return;
        }

        g_reviewHover = -1;
        g_reviewOpen = true; // hand off to the themed review panel
        InvalidateRect(hwnd, nullptr, FALSE);
    }

    void CancelReview(HWND hwnd)
    {
        g_reviewOpen = false;
        InvalidateRect(hwnd, nullptr, FALSE); // edits are kept
    }

    void ApplyReviewed(HWND hwnd)
    {
        using namespace Environ::core;

        // Re-check at the actual write point — the panel may have been open a while, and the
        // registry could have changed underneath us since load.
        const bool externalChange{
            !compute_diff(g_grid.OriginalVars(Scope::User), read_variables(Scope::User)).empty() ||
            !compute_diff(g_grid.OriginalVars(Scope::Machine), read_variables(Scope::Machine)).empty()};
        if (externalChange &&
            MessageBoxW(hwnd,
                        L"The environment changed outside environ since it was loaded. "
                        L"Applying now may overwrite those external changes.\n\nContinue?",
                        L"environ \x2014 External change detected", MB_OKCANCEL | MB_ICONWARNING) != IDOK)
            return; // keep the review panel open so the user can re-check or cancel

        g_reviewOpen = false;
        const ApplyResult result{apply_document_changes(
            g_grid.OriginalVars(Scope::User), g_reviewCurUser,
            g_grid.OriginalVars(Scope::Machine), g_reviewCurMachine, g_elevated)};

        if (result.succeeded())
        {
            LoadData(); // re-read the registry: fresh originals, dirty cleared
        }
        else
        {
            // Keep the in-memory edits on failure so the user can correct and retry.
            std::wstring err{L"Some changes could not be applied; your edits are kept so you can retry.\n\n"};
            if (!result.user.error.empty()) err += L"User: " + result.user.error + L'\n';
            if (!result.machine.error.empty()) err += L"Machine: " + result.machine.error + L'\n';
            MessageBoxW(hwnd, err.c_str(), L"environ \x2014 Apply failed", MB_OK | MB_ICONERROR);
        }
        InvalidateRect(hwnd, nullptr, FALSE);
    }

    LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
    {
        // While the apply-review modal is open it owns input; paint/size/etc. fall through.
        if (g_reviewOpen)
        {
            switch (msg)
            {
            case WM_KEYDOWN:
                if (wp == VK_RETURN) ApplyReviewed(hwnd);
                else if (wp == VK_ESCAPE) CancelReview(hwnd);
                return 0;
            case WM_MOUSEWHEEL:
                return 0;
            case WM_MOUSEMOVE:
                if (g_rt)
                {
                    const float scale{DipScale(hwnd)};
                    const float x{GET_X_LPARAM(lp) / scale}, my{GET_Y_LPARAM(lp) / scale};
                    const ReviewGeom g{ReviewLayout(g_rt->GetSize())};
                    const int hover{Contains(g.cancelBtn, x, my) ? 0 : (Contains(g.applyBtn, x, my) ? 1 : -1)};
                    if (hover != g_reviewHover) { g_reviewHover = hover; InvalidateRect(hwnd, nullptr, FALSE); }
                }
                return 0;
            case WM_LBUTTONDOWN:
                if (g_rt)
                {
                    const float scale{DipScale(hwnd)};
                    const float x{GET_X_LPARAM(lp) / scale}, my{GET_Y_LPARAM(lp) / scale};
                    const ReviewGeom g{ReviewLayout(g_rt->GetSize())};
                    if (Contains(g.applyBtn, x, my)) { g_eatNextDblClk = true; ApplyReviewed(hwnd); }
                    else if (Contains(g.cancelBtn, x, my)) { g_eatNextDblClk = true; CancelReview(hwnd); }
                    else if (!Contains(g.card, x, my)) { g_eatNextDblClk = true; CancelReview(hwnd); } // scrim = cancel
                }
                return 0;
            case WM_LBUTTONUP:
                return 0;
            case WM_LBUTTONDBLCLK:
                return 0; // never let a double-click reach the grid while modal
            default:
                break;
            }
        }
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
            const float scale{DipScale(hwnd)};
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
            if (wp == 'S' && (GetKeyState(VK_CONTROL) & 0x8000)) { SaveChanges(hwnd); return 0; }
            if (wp == VK_RETURN) { BeginEditFromGrid(hwnd); return 0; }
            const char* want{nullptr};
            if (wp == VK_F1) want = "dark";
            else if (wp == VK_F2) want = "light";
            else if (wp == VK_F3) want = "blue";
            if (want)
            {
                if (g_grid.IsEditing()) EndEdit(hwnd, true); // defensive: never switch theme mid-edit
                if (g_theme.SelectByName(want))
                {
                    ApplyTitleBar(hwnd);
                    if (g_editBrush) RefreshEditBrush(); // re-tint the cached editor brush
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
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
            const float scale{DipScale(hwnd)};
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
            g_eatNextDblClk = false; // a fresh press cancels any pending dblclk-swallow
            const float scale{DipScale(hwnd)};
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
        case WM_LBUTTONDBLCLK:
        {
            if (g_eatNextDblClk) { g_eatNextDblClk = false; return 0; } // trailing dblclk of a modal close
            const float scale{DipScale(hwnd)};
            BeginEditAt(hwnd, GET_X_LPARAM(lp) / scale, GET_Y_LPARAM(lp) / scale);
            return 0;
        }
        case WM_MOUSEWHEEL:
            if (g_grid.IsEditing()) EndEdit(hwnd, true);
            Repaint(hwnd, g_grid.OnWheel(GET_WHEEL_DELTA_WPARAM(wp)));
            return 0;
        case WM_CTLCOLOREDIT:
            if (reinterpret_cast<HWND>(lp) == g_edit)
            {
                const theme::ColorScheme& s{g_theme.Current()};
                SetTextColor(reinterpret_cast<HDC>(wp), ToColorRef(s.edit.text));
                SetBkColor(reinterpret_cast<HDC>(wp), ToColorRef(s.edit.fill));
                if (!g_editBrush) RefreshEditBrush();
                return reinterpret_cast<LRESULT>(g_editBrush);
            }
            break;
        case WM_COMMAND:
            if (reinterpret_cast<HWND>(lp) == g_edit && HIWORD(wp) == EN_KILLFOCUS)
            {
                EndEdit(hwnd, true);
                return 0;
            }
            break;
        case WM_APP_EDIT_END:
            EndEdit(hwnd, wp != 0);
            if (wp == 2 && g_grid.SelectNextEditable(lp != 0 ? -1 : 1)) // Tab → next editable row
                BeginEditFromGrid(hwnd);
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
            if (g_grid.IsEditing()) EndEdit(hwnd, true);
            if (g_rt)
            {
                if (g_rt->Resize(D2D1::SizeU(LOWORD(lp), HIWORD(lp))) == D2DERR_RECREATE_TARGET)
                    DiscardDeviceResources();
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        case WM_DPICHANGED:
        {
            if (g_grid.IsEditing()) EndEdit(hwnd, true);
            const RECT* const r{reinterpret_cast<RECT*>(lp)};
            SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            RefreshEditFont(hwnd); // re-create the editor font at the new DPI for next time
            DiscardDeviceResources();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_DESTROY:
            if (g_editBrush) { DeleteObject(g_editBrush); g_editBrush = nullptr; }
            if (g_editFont)  { DeleteObject(g_editFont);  g_editFont = nullptr; }
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
        ReleaseGraphics();
        return 1;
    }
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(&g_dw))))
    {
        spdlog::error("DWriteCreateFactory failed");
        ReleaseGraphics();
        return 1;
    }

    g_theme.LoadOrDefault(ThemePathBesideExe());

    g_fmtCaption = MakeFormat(L"Segoe UI Variable Text", 13.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, true);
    g_fmtSub     = MakeFormat(L"Segoe UI Variable Text", 12.0f, DWRITE_FONT_WEIGHT_NORMAL, false);
    g_fmtName    = MakeFormat(L"Segoe UI Variable Text", 14.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, true);
    g_fmtValue   = MakeFormat(L"Segoe UI Variable Small", 11.5f, DWRITE_FONT_WEIGHT_NORMAL, true);
    g_fmtHeader  = MakeFormat(L"Segoe UI Variable Small", 11.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, true);
    g_fmtGlyph   = MakeFormat(L"Segoe Fluent Icons", 10.0f, DWRITE_FONT_WEIGHT_NORMAL, true, true);
    g_fmtButton  = MakeFormat(L"Segoe UI Variable Text", 13.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, true, true);
    if (!g_fmtCaption || !g_fmtSub || !g_fmtName || !g_fmtValue || !g_fmtHeader || !g_fmtGlyph || !g_fmtButton)
    {
        spdlog::error("text format creation failed");
        ReleaseGraphics();
        return 1;
    }

    LoadData();

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS; // deliver WM_LBUTTONDBLCLK
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"EnvironWin32Host";
    RegisterClassExW(&wc);

    g_hwnd = CreateWindowExW(
        0, wc.lpszClassName, L"environ", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 860, 620, nullptr, nullptr, hInst, nullptr);
    if (!g_hwnd)
    {
        spdlog::error("CreateWindowExW failed");
        ReleaseGraphics();
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
