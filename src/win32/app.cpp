#include "precomp.h"

// environ — Win32 host (Phase 2+). Pure Win32 + Direct2D + DirectWrite.
// Frameless window with custom-drawn title bar. MainWindow owns D2D device
// resources, theme, grid, and the inline EDIT editor; loads live data from
// core EnvStore and forwards input.

#include "app.h"
#include "theme.h"
#include "grid.h"
#include "AppSettings.h"
#include "EnvStore.h"
#include "EnvWriter.h"
#include "SnapshotStore.h"

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

    // Format "2026-06-18T14:22:05Z" as "2026-06-18  14:22:05"
    std::wstring FormatTimestamp(const std::string& ts)
    {
        std::wstring w;
        w.reserve(ts.size());
        for (char c : ts) w.push_back(static_cast<wchar_t>(c));
        if (w.size() >= 11 && w[10] == L'T')
        {
            w[10] = L' ';
            w.insert(10, 1, L' ');
        }
        if (!w.empty() && w.back() == L'Z') w.pop_back();
        return w;
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

    // --- Diff table builder ---
    // Builds a monospace text table comparing two sets of environment variables.
    // Only changed variables are shown. For path-list variables with any difference,
    // all segments on both sides are shown so reordering is visible.

    std::vector<std::wstring> BuildDiffTable(
        const wchar_t* leftLabel,
        const wchar_t* rightLabel,
        const std::vector<Environ::core::EnvVariable>& leftUser,
        const std::vector<Environ::core::EnvVariable>& leftMachine,
        const std::vector<Environ::core::EnvVariable>& rightUser,
        const std::vector<Environ::core::EnvVariable>& rightMachine)
    {
        using Environ::core::EnvVariable;
        using Environ::core::EnvVariableKind;

        struct TableRow { std::wstring name, left, right; };
        std::vector<TableRow> rows;

        const auto toLower = [](std::wstring s) {
            std::ranges::transform(s, s.begin(), ::towlower);
            return s;
        };

        const auto addScope = [&](const wchar_t* scope,
                                   const std::vector<EnvVariable>& lv,
                                   const std::vector<EnvVariable>& rv) {
            // Build name->variable maps.
            std::map<std::wstring, const EnvVariable*> leftMap, rightMap;
            for (const auto& v : lv) leftMap[toLower(v.name)] = &v;
            for (const auto& v : rv) rightMap[toLower(v.name)] = &v;

            // Collect all names, sorted case-insensitively.
            std::set<std::wstring> keys;
            for (const auto& [k, _] : leftMap) keys.insert(k);
            for (const auto& [k, _] : rightMap) keys.insert(k);

            for (const auto& key : keys)
            {
                auto itL{leftMap.find(key)};
                auto itR{rightMap.find(key)};
                const EnvVariable* left{itL != leftMap.end() ? itL->second : nullptr};
                const EnvVariable* right{itR != rightMap.end() ? itR->second : nullptr};

                // Skip unchanged.
                if (left && right && left->value == right->value) continue;

                const std::wstring dispName{std::wstring{scope} + L" " +
                    (right ? right->name : left->name)};

                const bool leftPath{left && left->kind == EnvVariableKind::PathList && !left->segments.empty()};
                const bool rightPath{right && right->kind == EnvVariableKind::PathList && !right->segments.empty()};

                if (leftPath || rightPath)
                {
                    // Path-list: show all segments side by side.
                    const auto& lSegs{leftPath ? left->segments : std::vector<std::wstring>{}};
                    const auto& rSegs{rightPath ? right->segments : std::vector<std::wstring>{}};
                    const size_t maxSegs{std::max(lSegs.size(), rSegs.size())};

                    rows.push_back({dispName,
                                    left ? L"" : L"(not set)",
                                    right ? L"" : L"(not set)"});
                    for (size_t s{0}; s < maxSegs; ++s)
                    {
                        rows.push_back({L"",
                                        s < lSegs.size() ? lSegs[s] : L"",
                                        s < rSegs.size() ? rSegs[s] : L""});
                    }
                }
                else
                {
                    // Scalar: one row.
                    rows.push_back({dispName,
                                    left ? left->value : L"(not set)",
                                    right ? right->value : L"(not set)"});
                }
            }
        };

        addScope(L"[User]", leftUser, rightUser);
        addScope(L"[Machine]", leftMachine, rightMachine);

        if (rows.empty())
            return {L"  No differences"};

        // Compute column widths from content.
        size_t nameW{wcslen(L"Variable")};
        size_t leftW{wcslen(leftLabel)};
        size_t rightW{wcslen(rightLabel)};
        for (const auto& r : rows)
        {
            nameW = std::max(nameW, r.name.size());
            leftW = std::max(leftW, r.left.size());
            rightW = std::max(rightW, r.right.size());
        }
        // Cap to keep things reasonable; clipping handles overflow.
        nameW = std::min(nameW, size_t{26});
        leftW = std::min(leftW, size_t{52});
        rightW = std::min(rightW, size_t{52});

        const auto pad = [](const std::wstring& s, size_t w) {
            if (s.size() >= w) return s.substr(0, w);
            return s + std::wstring(w - s.size(), L' ');
        };

        std::vector<std::wstring> lines;
        lines.reserve(rows.size() + 2);

        // Header.
        lines.push_back(L" " + pad(L"Variable", nameW) + L" \x2502 " +
                         pad(std::wstring{leftLabel}, leftW) + L" \x2502 " +
                         pad(std::wstring{rightLabel}, rightW));
        // Separator.
        lines.push_back(L" " + std::wstring(nameW, L'\x2500') + L"\x2500\x253C\x2500" +
                         std::wstring(leftW, L'\x2500') + L"\x2500\x253C\x2500" +
                         std::wstring(rightW, L'\x2500'));
        // Data rows.
        for (const auto& r : rows)
        {
            lines.push_back(L" " + pad(r.name, nameW) + L" \x2502 " +
                             pad(r.left, leftW) + L" \x2502 " +
                             pad(r.right, rightW));
        }
        return lines;
    }

    // Context menu item IDs.
    constexpr UINT kMenuCopy{1001};
    constexpr UINT kMenuInsert{1002};
    constexpr UINT kMenuRemove{1003};

    // Owner-drawn menu item data -- stored in MENUITEMINFO::dwItemData.
    struct MenuItemData
    {
        std::wstring label;
        std::wstring accel;
    };

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
}

// ---------------------------------------------------------------------------
// MainWindow
// ---------------------------------------------------------------------------

ui::MainWindow::~MainWindow()
{
    ReleaseGraphics();
}

// --- WndProc trampoline ---

LRESULT CALLBACK ui::MainWindow::StaticWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    MainWindow* self{nullptr};
    if (msg == WM_NCCREATE)
    {
        auto* cs = reinterpret_cast<CREATESTRUCT*>(lp);
        self = static_cast<MainWindow*>(cs->lpCreateParams);
        self->m_hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    else
    {
        self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->HandleMessage(msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// --- Init ---

bool ui::MainWindow::InitGraphics()
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

bool ui::MainWindow::InitSettings()
{
    try
    {
        m_settings.load();
    }
    catch (const std::exception& e)
    {
        spdlog::error("Settings load failed: {}", e.what());
        return false;
    }
    m_zoom = std::clamp(static_cast<float>(m_settings.appearance.zoom.get()) / 100.0f, 0.5f, 2.0f);

    m_theme.LoadOrDefault(ThemePathBesideExe());
    {
        const auto& savedTheme{m_settings.appearance.theme.get()};
        if (!savedTheme.empty()) m_theme.SelectByName(savedTheme);
    }
    return true;
}

bool ui::MainWindow::InitFonts()
{
    if (!CreateFonts())
    {
        spdlog::error("text format creation failed");
        return false;
    }
    return true;
}

void ui::MainWindow::InitData()
{
    LoadData();
    m_grid.SetZoom(m_zoom);

    if (!m_snapshots.open())
        spdlog::warn("Snapshot database unavailable; history disabled");
}

// --- Create (combines init + window creation) ---

bool ui::MainWindow::Create(HINSTANCE hInst, int nCmdShow)
{
    m_hInst = hInst;
    m_nCmdShow = nCmdShow;

    if (!InitGraphics() || !InitSettings() || !InitFonts())
    {
        ReleaseGraphics();
        return false;
    }

    InitData();

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS; // deliver WM_LBUTTONDBLCLK
    wc.lpfnWndProc = StaticWndProc;
    wc.hInstance = m_hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"EnvironWin32Host";
    RegisterClassExW(&wc);

    int wx{m_settings.window.x.get()};
    int wy{m_settings.window.y.get()};
    int ww{m_settings.window.width.get()};
    int wh{m_settings.window.height.get()};
    if (wx < 0 || wy < 0) { wx = CW_USEDEFAULT; wy = CW_USEDEFAULT; }
    if (ww <= 0) ww = 860;
    if (wh <= 0) wh = 620;

    m_hwnd = CreateWindowExW(
        0, wc.lpszClassName, L"environ", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        wx, wy, ww, wh, nullptr, nullptr, m_hInst, this);
    if (!m_hwnd)
    {
        spdlog::error("CreateWindowExW failed");
        ReleaseGraphics();
        return false;
    }

    ApplyTitleBar();
    const int backdrop{DWMSBT_MAINWINDOW};
    DwmSetWindowAttribute(m_hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop));

    // Validate the restored position against available monitors -- if the window
    // is entirely off-screen (monitor removed/reconfigured), reset to default.
    if (!MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONULL))
    {
        SetWindowPos(m_hwnd, nullptr, CW_USEDEFAULT, CW_USEDEFAULT, ww, wh,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }

    // Re-run NCCALCSIZE so the frameless client takes effect.
    SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0,
                 SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
    ShowWindow(m_hwnd, m_settings.window.maximized.get() ? SW_MAXIMIZE : m_nCmdShow);
    return true;
}

// --- Graphics ---

void ui::MainWindow::DiscardDeviceResources()
{
    if (m_brush) { m_brush->Release(); m_brush = nullptr; }
    if (m_rt)    { m_rt->Release();    m_rt = nullptr; }
}

void ui::MainWindow::ReleaseGraphics()
{
    DiscardDeviceResources();
    for (IDWriteTextFormat** fmt : {&m_fmtCaption, &m_fmtSub, &m_fmtName, &m_fmtValue, &m_fmtHeader, &m_fmtGlyph, &m_fmtButton, &m_fmtMono})
    {
        if (*fmt) { (*fmt)->Release(); *fmt = nullptr; }
    }
    if (m_dw)  { m_dw->Release();  m_dw = nullptr; }
    if (m_d2d) { m_d2d->Release(); m_d2d = nullptr; }
}

HRESULT ui::MainWindow::EnsureRenderTarget()
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

// --- Fonts ---

IDWriteTextFormat* ui::MainWindow::MakeFormat(const wchar_t* family, float size,
                                              DWRITE_FONT_WEIGHT weight, bool vcenter, bool hcenter)
{
    IDWriteTextFormat* fmt{nullptr};
    if (FAILED(m_dw->CreateTextFormat(family, nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
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

bool ui::MainWindow::CreateFonts()
{
    for (IDWriteTextFormat** fmt : {&m_fmtCaption, &m_fmtSub, &m_fmtName,
         &m_fmtValue, &m_fmtHeader, &m_fmtGlyph, &m_fmtButton, &m_fmtMono})
    {
        if (*fmt) { (*fmt)->Release(); *fmt = nullptr; }
    }
    const auto z = [this](float size) { return size * m_zoom; };
    m_fmtCaption = MakeFormat(L"Segoe UI Variable Text", z(13.0f), DWRITE_FONT_WEIGHT_SEMI_BOLD, true);
    m_fmtSub     = MakeFormat(L"Segoe UI Variable Text", z(12.0f), DWRITE_FONT_WEIGHT_NORMAL, false);
    m_fmtName    = MakeFormat(L"Segoe UI Variable Text", z(14.0f), DWRITE_FONT_WEIGHT_SEMI_BOLD, true);
    m_fmtValue   = MakeFormat(L"Segoe UI Variable Small", z(11.5f), DWRITE_FONT_WEIGHT_NORMAL, true);
    m_fmtHeader  = MakeFormat(L"Segoe UI Variable Small", z(11.0f), DWRITE_FONT_WEIGHT_SEMI_BOLD, true);
    m_fmtGlyph   = MakeFormat(L"Segoe Fluent Icons", z(10.0f), DWRITE_FONT_WEIGHT_NORMAL, true, true);
    m_fmtButton  = MakeFormat(L"Segoe UI Variable Text", z(13.0f), DWRITE_FONT_WEIGHT_SEMI_BOLD, true, true);
    m_fmtMono    = MakeFormat(L"Consolas", z(9.5f), DWRITE_FONT_WEIGHT_NORMAL, false);
    return m_fmtCaption && m_fmtSub && m_fmtName && m_fmtValue
        && m_fmtHeader && m_fmtGlyph && m_fmtButton && m_fmtMono;
}

void ui::MainWindow::RefreshEditBrush()
{
    if (m_editBrush) { DeleteObject(m_editBrush); m_editBrush = nullptr; }
    m_editBrush = CreateSolidBrush(ToColorRef(m_theme.Current().edit.fill));
}

void ui::MainWindow::RefreshEditFont()
{
    if (!m_edit) return;
    const float scale{DipScale(m_hwnd)};
    const auto px = [scale](float dip) { return static_cast<int>(dip * scale); };
    if (m_editFont) DeleteObject(m_editFont);
    if (m_editFontName) DeleteObject(m_editFontName);
    // Value font matches the value column (12, normal); name font matches the name
    // column (14, semibold). PositionEditor selects one per edit.
    m_editFont = CreateFontW(-px(12.0f * m_zoom), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI Variable Text");
    m_editFontName = CreateFontW(-px(14.0f * m_zoom), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI Variable Text");
}

// --- Editor ---

void ui::MainWindow::EnsureEditControl()
{
    if (m_edit) return;
    m_edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL, 0, 0, 0, 0,
                             m_hwnd, nullptr,
                             reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(m_hwnd, GWLP_HINSTANCE)), nullptr);
    if (!m_edit)
    {
        spdlog::error("inline edit control creation failed");
        return;
    }
    if (!SetWindowSubclass(m_edit, EditSubclass, 1, reinterpret_cast<DWORD_PTR>(m_hwnd)))
    {
        spdlog::error("SetWindowSubclass for edit control failed");
        DestroyWindow(m_edit);
        m_edit = nullptr;
        return;
    }
    RefreshEditFont();
    SendMessageW(m_edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);
}

void ui::MainWindow::EndEdit(bool commit)
{
    if (!m_edit || !IsWindowVisible(m_edit)) return;
    if (commit)
    {
        const int len{GetWindowTextLengthW(m_edit)};
        std::wstring text(static_cast<size_t>(len) + 1, L'\0'); // +1 writable slot for the terminator
        const int copied{GetWindowTextW(m_edit, text.data(), len + 1)};
        text.resize(static_cast<size_t>(copied));
        m_grid.CommitEdit(text);
    }
    else
    {
        m_grid.CancelEdit();
    }
    ShowWindow(m_edit, SW_HIDE);
    SetFocus(m_hwnd);
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void ui::MainWindow::PositionEditor(const Grid::EditTarget& target)
{
    const float scale{DipScale(m_hwnd)};
    const auto px = [scale](float dip) { return static_cast<int>(dip * scale); };
    const D2D1_RECT_F& c{target.cell};

    // Pick the cached font matching the field being edited (name = heavier, like its
    // display), then size the control to exactly one line and center it: a single-line
    // EDIT top-aligns its text, so a one-line-tall control lands it on the cell center.
    HFONT font{target.isName ? m_editFontName : m_editFont};
    SendMessageW(m_edit, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

    TEXTMETRICW tm{};
    if (const HDC dc{GetDC(m_edit)})
    {
        const HGDIOBJ prev{SelectObject(dc, font)};
        GetTextMetricsW(dc, &tm);
        SelectObject(dc, prev);
        ReleaseDC(m_edit, dc);
    }
    const int cellTop{px(c.top)};
    const int cellH{px(c.bottom) - cellTop};
    const int editH{tm.tmHeight > 0 ? static_cast<int>(tm.tmHeight) : px(16.0f)};
    MoveWindow(m_edit, px(c.left), cellTop + (cellH - editH) / 2,
               px(c.right) - px(c.left), editH, FALSE);

    SetWindowTextW(m_edit, target.text.c_str());
    ShowWindow(m_edit, SW_SHOW);
    SetFocus(m_edit);
    SendMessageW(m_edit, EM_SETSEL, 0, -1);
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void ui::MainWindow::BeginEditFromGrid()
{
    if (!m_grid.SelectionEditable()) return; // nothing to edit; don't even create the control
    EnsureEditControl();
    if (!m_edit) return; // creation failed; don't leave the grid stuck in editing state
    if (const auto target{m_grid.BeginEdit()}) PositionEditor(*target);
}

void ui::MainWindow::BeginEditNameFromGrid()
{
    if (!m_grid.SelectionEditable()) return;
    EnsureEditControl();
    if (!m_edit) return;
    if (const auto target{m_grid.BeginEditName()}) PositionEditor(*target);
}

void ui::MainWindow::BeginEditAt(float x, float y)
{
    EnsureEditControl();
    if (!m_edit) return;
    const auto target{m_grid.BeginEditAt(x, y)};
    InvalidateRect(m_hwnd, nullptr, FALSE); // selection may have moved even if not editable
    if (target) PositionEditor(*target);
}

// --- Title bar ---

void ui::MainWindow::ApplyTitleBar()
{
    const BOOL dark{m_theme.Current().darkTitleBar ? TRUE : FALSE};
    DwmSetWindowAttribute(m_hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
}

// --- Menu / clipboard ---

void ui::MainWindow::ShowGridContextMenu(int screenX, int screenY)
{
    const bool editable{m_grid.SelectionEditable()};
    const bool isPath{m_grid.SelectedIsPathEntry()};

    HMENU hMenu{CreatePopupMenu()};
    if (!hMenu) return;

    // Context-sensitive labels: path-list entries vs scalar variables.
    const int selCount{m_grid.SelectionCount()};
    MenuItemData dataCopy{selCount > 1 ? std::format(L"Copy {} rows", selCount) : std::wstring{L"Copy"}, L"Ctrl+C"};
    MenuItemData dataInsert{isPath ? L"Insert entry" : L"New variable", L"Ins"};
    MenuItemData dataRemove{isPath ? L"Remove entry" : L"Delete variable", L"Del"};

    const auto addItem = [&](UINT id, MenuItemData* data, bool enabled) {
        MENUITEMINFOW mi{};
        mi.cbSize = sizeof(mi);
        mi.fMask = MIIM_ID | MIIM_FTYPE | MIIM_STATE | MIIM_DATA;
        mi.fType = MFT_OWNERDRAW;
        mi.fState = enabled ? MFS_ENABLED : MFS_GRAYED;
        mi.wID = id;
        mi.dwItemData = reinterpret_cast<ULONG_PTR>(data);
        InsertMenuItemW(hMenu, id, FALSE, &mi);
    };

    addItem(kMenuCopy, &dataCopy, true);
    addItem(kMenuInsert, &dataInsert, editable);
    addItem(kMenuRemove, &dataRemove, editable);

    // Set themed menu background via MENUINFO.
    const theme::ColorScheme& s{m_theme.Current()};
    HBRUSH menuBg{CreateSolidBrush(ToColorRef(s.card.fill))};
    MENUINFO mi{};
    mi.cbSize = sizeof(mi);
    mi.fMask = MIM_BACKGROUND;
    mi.hbrBack = menuBg;
    SetMenuInfo(hMenu, &mi);

    TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON,
                   screenX, screenY, 0, m_hwnd, nullptr);
    DestroyMenu(hMenu);
    DeleteObject(menuBg);
}

void ui::MainWindow::CopyToClipboard(const std::wstring& text)
{
    if (text.empty()) return;
    if (!OpenClipboard(m_hwnd)) return;
    EmptyClipboard();
    const size_t bytes{(text.size() + 1) * sizeof(wchar_t)};
    HGLOBAL hMem{GlobalAlloc(GMEM_MOVEABLE, bytes)};
    if (hMem)
    {
        void* dst{GlobalLock(hMem)};
        if (dst)
        {
            memcpy(dst, text.c_str(), bytes);
            GlobalUnlock(hMem);
            SetClipboardData(CF_UNICODETEXT, hMem);
        }
        else
        {
            GlobalFree(hMem);
        }
    }
    CloseClipboard();
}

// --- Data / save ---

void ui::MainWindow::LoadData()
{
    using namespace Environ::core;
    m_elevated = is_elevated();
    std::vector<EnvVariable> userVars = read_variables(Scope::User);
    std::vector<EnvVariable> machineVars = read_variables(Scope::Machine);
    expand_and_validate(userVars);
    expand_and_validate(machineVars);
    detect_duplicates(userVars, machineVars);
    m_userCount = userVars.size();
    m_machineCount = machineVars.size();
    m_grid.SetData(userVars, machineVars, m_elevated);
}

void ui::MainWindow::SaveChanges()
{
    using namespace Environ::core;
    if (m_grid.IsEditing()) EndEdit(true);
    if (!m_grid.HasChanges())
    {
        MessageBoxW(m_hwnd, L"No changes to apply.", L"environ", MB_OK | MB_ICONINFORMATION);
        return;
    }

    m_reviewCurUser = m_grid.CurrentVars(Scope::User);
    m_reviewCurMachine = m_grid.CurrentVars(Scope::Machine);

    // Reject invalid renames (empty / '=' / case-insensitive duplicate) before any review
    // or write -- a rename to an existing name would otherwise delete one and overwrite the
    // other; an empty name would silently vanish on reload.
    std::wstring invalid{validate_variables(m_reviewCurUser)};
    if (invalid.empty()) invalid = validate_variables(m_reviewCurMachine);
    if (!invalid.empty())
    {
        MessageBoxW(m_hwnd, invalid.c_str(), L"environ \x2014 Cannot apply", MB_OK | MB_ICONERROR);
        return;
    }

    m_reviewUser = compute_diff(m_grid.OriginalVars(Scope::User), m_reviewCurUser);
    m_reviewMachine = compute_diff(m_grid.OriginalVars(Scope::Machine), m_reviewCurMachine);
    if (m_reviewUser.empty() && m_reviewMachine.empty())
    {
        // Edits exist per-row but net to the original values (e.g. edited back).
        MessageBoxW(m_hwnd, L"No effective changes to apply.", L"environ", MB_OK | MB_ICONINFORMATION);
        return;
    }

    m_reviewHover = -1;
    m_reviewOpen = true; // hand off to the themed review panel
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void ui::MainWindow::CancelReview()
{
    m_reviewOpen = false;
    InvalidateRect(m_hwnd, nullptr, FALSE); // edits are kept
}

void ui::MainWindow::ApplyReviewed()
{
    using namespace Environ::core;

    // Re-check at the actual write point -- the panel may have been open a while, and the
    // registry could have changed underneath us since load.
    const bool externalChange{
        !compute_diff(m_grid.OriginalVars(Scope::User), read_variables(Scope::User)).empty() ||
        !compute_diff(m_grid.OriginalVars(Scope::Machine), read_variables(Scope::Machine)).empty()};
    if (externalChange &&
        MessageBoxW(m_hwnd,
                    L"The environment changed outside environ since it was loaded. "
                    L"Applying now may overwrite those external changes.\n\nContinue?",
                    L"environ \x2014 External change detected", MB_OKCANCEL | MB_ICONWARNING) != IDOK)
        return; // keep the review panel open so the user can re-check or cancel

    // Snapshot the current registry state before overwriting it.
    {
        auto snapUser{read_variables(Scope::User)};
        auto snapMachine{read_variables(Scope::Machine)};
        auto allChanges{m_reviewUser};
        allChanges.insert(allChanges.end(), m_reviewMachine.begin(), m_reviewMachine.end());
        auto label{pnq::unicode::to_utf8(summarize_changes(allChanges))};
        m_snapshots.create_snapshot(label, snapUser, snapMachine);
    }

    m_reviewOpen = false;
    const ApplyResult result{apply_document_changes(
        m_grid.OriginalVars(Scope::User), m_reviewCurUser,
        m_grid.OriginalVars(Scope::Machine), m_reviewCurMachine, m_elevated)};

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
        MessageBoxW(m_hwnd, err.c_str(), L"environ \x2014 Apply failed", MB_OK | MB_ICONERROR);
    }
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

// --- Review layout ---

ui::MainWindow::ReviewGeom ui::MainWindow::ReviewLayout(const D2D1_SIZE_F& sz)
{
    const float cw{std::min(520.0f, sz.width - 80.0f)};
    const float pad{20.0f};
    const float titleH{36.0f};
    const float lineH{22.0f};
    const float btnRow{56.0f};
    const int lineCount{
        (m_reviewUser.empty() ? 0 : 1 + static_cast<int>(m_reviewUser.size())) +
        (m_reviewMachine.empty() ? 0 : 1 + static_cast<int>(m_reviewMachine.size()))};
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

// --- History ---

ui::MainWindow::HistoryGeom ui::MainWindow::HistoryLayout(const D2D1_SIZE_F& sz)
{
    HistoryGeom hg{};
    const float cw{std::min(960.0f, sz.width - 60.0f)};
    const float pad{20.0f};
    const float titleH{36.0f};
    const float btnRow{56.0f};
    const float maxListH{sz.height * 0.6f};
    const float contentH{HistoryListContentH(hg)};
    const float listH{std::min(contentH, maxListH)};
    const float ch{pad + titleH + std::max(listH, 60.0f) + btnRow + pad};
    const float cx{(sz.width - cw) / 2.0f};
    const float cy{(sz.height - ch) / 2.0f};

    hg.card = D2D1::RectF(cx, cy, cx + cw, cy + ch);
    hg.list = D2D1::RectF(cx + pad, cy + pad + titleH, cx + cw - pad,
                           cy + pad + titleH + std::max(listH, 60.0f));

    constexpr float bw{96.0f};
    constexpr float bh{34.0f};
    const float by{hg.card.bottom - pad - bh};
    hg.restoreBtn = D2D1::RectF(hg.card.right - pad - bw, by, hg.card.right - pad, by + bh);
    hg.closeBtn = D2D1::RectF(hg.restoreBtn.left - 12.0f - bw, by, hg.restoreBtn.left - 12.0f, by + bh);
    hg.deleteBtn = D2D1::RectF(hg.card.left + pad, by, hg.card.left + pad + bw, by + bh);
    return hg;
}

int ui::MainWindow::HistoryDetailLineCount(int idx)
{
    if (idx != m_historySelected || idx < 0) return 0;
    int count{0};
    if (!m_historyRecordedTable.empty())
        count += 1 + static_cast<int>(m_historyRecordedTable.size());
    if (!m_historyCurrentTable.empty())
        count += 1 + static_cast<int>(m_historyCurrentTable.size());
    return count;
}

float ui::MainWindow::HistoryListContentH(const HistoryGeom& hg)
{
    float h{0.0f};
    for (int i{0}; i < static_cast<int>(m_historySnapshots.size()); ++i)
    {
        h += hg.rowH;
        if (i == m_historySelected)
            h += static_cast<float>(HistoryDetailLineCount(i)) * hg.detailH;
    }
    return h;
}

void ui::MainWindow::ComputeHistoryTables()
{
    using namespace Environ::core;
    m_historyRecordedTable.clear();
    m_historyCurrentTable.clear();
    if (m_historySelected < 0 ||
        m_historySelected >= static_cast<int>(m_historySnapshots.size()))
        return;

    const auto& snap{m_historySnapshots[static_cast<size_t>(m_historySelected)]};
    auto snapVars{m_snapshots.load_snapshot(snap.id)};
    auto snapUser{reconstruct_variables(snapVars, Scope::User)};
    auto snapMachine{reconstruct_variables(snapVars, Scope::Machine)};

    // "Difference from current" table.
    m_historyCurrentTable = BuildDiffTable(L"Current", L"Snapshot",
        m_historyCurUser, m_historyCurMachine, snapUser, snapMachine);

    // "Recorded changes" table -- compare against the previous snapshot.
    const size_t nextIdx{static_cast<size_t>(m_historySelected) + 1};
    if (nextIdx < m_historySnapshots.size())
    {
        auto prevVars{m_snapshots.load_snapshot(m_historySnapshots[nextIdx].id)};
        auto prevUser{reconstruct_variables(prevVars, Scope::User)};
        auto prevMachine{reconstruct_variables(prevVars, Scope::Machine)};
        m_historyRecordedTable = BuildDiffTable(L"Before", L"After",
            prevUser, prevMachine, snapUser, snapMachine);
    }
    else
    {
        // Oldest snapshot -- no previous.
        std::vector<EnvVariable> empty;
        m_historyRecordedTable = BuildDiffTable(L"Before", L"After",
            empty, empty, snapUser, snapMachine);
    }
}

void ui::MainWindow::HistorySelect(int idx)
{
    m_historySelected = idx;
    ComputeHistoryTables();
}

void ui::MainWindow::DeleteSelectedSnapshot()
{
    if (m_historySelected < 0 || m_historySelected >= static_cast<int>(m_historySnapshots.size()))
        return;
    const auto id{m_historySnapshots[static_cast<size_t>(m_historySelected)].id};
    m_snapshots.delete_snapshot(id);
    m_historySnapshots.erase(m_historySnapshots.begin() + m_historySelected);
    if (m_historySelected >= static_cast<int>(m_historySnapshots.size()))
        m_historySelected = static_cast<int>(m_historySnapshots.size()) - 1;
    ComputeHistoryTables();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void ui::MainWindow::OpenHistory()
{
    using namespace Environ::core;
    if (m_grid.IsEditing()) EndEdit(true);
    m_historySnapshots = m_snapshots.list_snapshots();

    // Read current registry once, reused for all "vs current" comparisons.
    m_historyCurUser = read_variables(Scope::User);
    m_historyCurMachine = read_variables(Scope::Machine);
    expand_and_validate(m_historyCurUser);
    expand_and_validate(m_historyCurMachine);

    m_historyRecordedTable.clear();
    m_historyCurrentTable.clear();
    m_historySelected = -1;
    m_historyRowHover = -1;
    m_historyHover = -1;
    m_historyScroll = 0.0f;
    m_historyOpen = true;
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void ui::MainWindow::CloseHistory()
{
    m_historyOpen = false;
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void ui::MainWindow::RestoreSnapshot()
{
    using namespace Environ::core;
    if (m_historySelected < 0 || m_historySelected >= static_cast<int>(m_historySnapshots.size()))
        return;

    const auto snapId{m_historySnapshots[static_cast<size_t>(m_historySelected)].id};
    auto snapVars{m_snapshots.load_snapshot(snapId)};

    // Reconstruct classified + validated variables from the snapshot.
    auto snapUser{reconstruct_variables(snapVars, Scope::User)};
    auto snapMachine{reconstruct_variables(snapVars, Scope::Machine)};

    // Read current registry state as the originals for diff computation.
    auto curUser{read_variables(Scope::User)};
    auto curMachine{read_variables(Scope::Machine)};
    expand_and_validate(curUser);
    expand_and_validate(curMachine);
    detect_duplicates(curUser, curMachine);
    detect_duplicates(snapUser, snapMachine);

    m_grid.SetDataForRestore(curUser, curMachine, snapUser, snapMachine, m_elevated);
    m_userCount = snapUser.size();
    m_machineCount = snapMachine.size();

    m_historyOpen = false;
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

int ui::MainWindow::HistoryRowAtPoint(const HistoryGeom& hg, float x, float y)
{
    if (x < hg.list.left || x >= hg.list.right || y < hg.list.top || y >= hg.list.bottom)
        return -1;
    float ry{hg.list.top - m_historyScroll};
    for (int i{0}; i < static_cast<int>(m_historySnapshots.size()); ++i)
    {
        float rowBottom{ry + hg.rowH};
        if (i == m_historySelected)
            rowBottom += static_cast<float>(HistoryDetailLineCount(i)) * hg.detailH;
        if (y >= ry && y < rowBottom) return i;
        ry = rowBottom;
    }
    return -1;
}

void ui::MainWindow::HistoryEnsureVisible(const HistoryGeom& hg, int idx)
{
    if (idx < 0 || idx >= static_cast<int>(m_historySnapshots.size())) return;
    float top{0.0f};
    for (int i{0}; i < idx; ++i)
    {
        top += hg.rowH;
        if (i == m_historySelected)
            top += static_cast<float>(HistoryDetailLineCount(i)) * hg.detailH;
    }
    float bottom{top + hg.rowH};
    if (idx == m_historySelected)
        bottom += static_cast<float>(HistoryDetailLineCount(idx)) * hg.detailH;
    const float viewH{hg.list.bottom - hg.list.top};
    if (top < m_historyScroll) m_historyScroll = top;
    else if (bottom > m_historyScroll + viewH) m_historyScroll = bottom - viewH;
    const float maxScroll{std::max(0.0f, HistoryListContentH(hg) - viewH)};
    m_historyScroll = std::clamp(m_historyScroll, 0.0f, maxScroll);
}

// --- Painting ---

void ui::MainWindow::DrawString(const std::wstring& s, IDWriteTextFormat* fmt,
                                const D2D1_RECT_F& box, const D2D1_COLOR_F& c)
{
    m_brush->SetColor(c);
    m_rt->DrawTextW(s.c_str(), static_cast<UINT32>(s.size()), fmt, box, m_brush,
                    D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void ui::MainWindow::DrawCaption(const theme::ColorScheme& s, float widthDip)
{
    DrawString(L"environ", m_fmtCaption,
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
        m_rt->DrawTextW(&btn.glyph, 1, m_fmtGlyph, btn.rect, m_brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
}

void ui::MainWindow::DrawReviewButton(const D2D1_RECT_F& r, const wchar_t* label,
                                      bool primary, bool hover, const theme::ColorScheme& s)
{
    m_brush->SetColor(primary ? s.accent : (hover ? s.rowSelected.fill : s.rowHover.fill));
    m_rt->FillRoundedRectangle(D2D1::RoundedRect(r, 6.0f, 6.0f), m_brush);
    if (!primary)
    {
        m_brush->SetColor(s.card.border);
        m_rt->DrawRoundedRectangle(D2D1::RoundedRect(r, 6.0f, 6.0f), m_brush, 1.0f);
    }
    m_brush->SetColor(primary ? s.accentText : s.headerText);
    m_rt->DrawTextW(label, static_cast<UINT32>(wcslen(label)), m_fmtButton, r, m_brush,
                    D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void ui::MainWindow::PaintReview(const theme::ColorScheme& s, const D2D1_SIZE_F& sz)
{
    const ReviewGeom g{ReviewLayout(sz)};

    m_brush->SetColor(s.scrim);
    m_rt->FillRectangle(D2D1::RectF(0.0f, 0.0f, sz.width, sz.height), m_brush);

    m_brush->SetColor(s.card.fill);
    m_rt->FillRoundedRectangle(D2D1::RoundedRect(g.card, 10.0f, 10.0f), m_brush);
    m_brush->SetColor(s.card.border);
    m_rt->DrawRoundedRectangle(D2D1::RoundedRect(g.card, 10.0f, 10.0f), m_brush, 1.0f);

    DrawString(L"Apply changes?", m_fmtName,
               D2D1::RectF(g.card.left + 20.0f, g.card.top + 12.0f, g.card.right - 20.0f, g.card.top + 44.0f),
               s.headerText);

    m_rt->PushAxisAlignedClip(g.list, D2D1_ANTIALIAS_MODE_ALIASED);
    float y{g.list.top};
    const auto group = [&](const wchar_t* label, const std::vector<Environ::core::EnvChange>& changes) {
        if (changes.empty()) return;
        DrawString(label, m_fmtHeader, D2D1::RectF(g.list.left, y, g.list.right, y + 22.0f), s.headerSubtext);
        y += 22.0f;
        for (const Environ::core::EnvChange& c : changes)
        {
            DrawString(L"    " + c.describe(), m_fmtValue,
                       D2D1::RectF(g.list.left, y, g.list.right, y + 22.0f), s.headerText);
            y += 22.0f;
        }
    };
    group(L"USER", m_reviewUser);
    group(L"MACHINE", m_reviewMachine);
    m_rt->PopAxisAlignedClip();

    DrawReviewButton(g.cancelBtn, L"Cancel", false, m_reviewHover == 0, s);
    DrawReviewButton(g.applyBtn, L"Apply", true, m_reviewHover == 1, s);
}

void ui::MainWindow::PaintHistory(const theme::ColorScheme& s, const D2D1_SIZE_F& sz)
{
    const HistoryGeom hg{HistoryLayout(sz)};

    // Scrim
    m_brush->SetColor(s.scrim);
    m_rt->FillRectangle(D2D1::RectF(0.0f, 0.0f, sz.width, sz.height), m_brush);

    // Card
    m_brush->SetColor(s.card.fill);
    m_rt->FillRoundedRectangle(D2D1::RoundedRect(hg.card, 10.0f, 10.0f), m_brush);
    m_brush->SetColor(s.card.border);
    m_rt->DrawRoundedRectangle(D2D1::RoundedRect(hg.card, 10.0f, 10.0f), m_brush, 1.0f);

    // Title
    DrawString(L"History", m_fmtName,
               D2D1::RectF(hg.card.left + 20.0f, hg.card.top + 12.0f,
                           hg.card.right - 20.0f, hg.card.top + 44.0f),
               s.headerText);

    // Snapshot list
    m_rt->PushAxisAlignedClip(hg.list, D2D1_ANTIALIAS_MODE_ALIASED);

    if (m_historySnapshots.empty())
    {
        DrawString(L"No snapshots yet. Snapshots are created when you apply changes.",
                   m_fmtValue,
                   D2D1::RectF(hg.list.left + 8.0f, hg.list.top, hg.list.right, hg.list.top + 28.0f),
                   s.headerSubtext);
    }
    else
    {
        float y{hg.list.top - m_historyScroll};
        for (int i{0}; i < static_cast<int>(m_historySnapshots.size()); ++i)
        {
            const auto& snap{m_historySnapshots[static_cast<size_t>(i)]};
            const bool selected{i == m_historySelected};
            const bool hovered{i == m_historyRowHover && !selected};

            const D2D1_RECT_F rowRect{D2D1::RectF(hg.list.left, y, hg.list.right, y + hg.rowH)};

            // Row background
            if (selected)
            {
                m_brush->SetColor(s.accent);
                m_rt->FillRectangle(rowRect, m_brush);
            }
            else if (hovered)
            {
                m_brush->SetColor(s.rowHover.fill);
                m_rt->FillRectangle(rowRect, m_brush);
            }

            // Timestamp + label
            auto ts{FormatTimestamp(snap.timestamp)};
            auto label{pnq::unicode::to_utf16(snap.label)};
            auto text{ts + L"  \x2014  " + label};
            DrawString(text, m_fmtValue,
                       D2D1::RectF(hg.list.left + 8.0f, y, hg.list.right - 8.0f, y + hg.rowH),
                       selected ? s.accentText : (hovered ? s.rowHover.text : s.headerText));

            y += hg.rowH;

            // Detail: monospace diff tables for selected snapshot.
            if (selected)
            {
                const auto paintTable = [&](const wchar_t* header,
                                            const std::vector<std::wstring>& table) {
                    if (table.empty()) return;
                    DrawString(header, m_fmtHeader,
                               D2D1::RectF(hg.list.left + 12.0f, y, hg.list.right - 8.0f, y + hg.detailH),
                               s.headerSubtext);
                    y += hg.detailH;
                    for (const auto& line : table)
                    {
                        DrawString(line, m_fmtMono,
                                   D2D1::RectF(hg.list.left + 4.0f, y, hg.list.right, y + hg.detailH),
                                   s.headerSubtext);
                        y += hg.detailH;
                    }
                };
                paintTable(L"Recorded changes:", m_historyRecordedTable);
                paintTable(L"Difference from current:", m_historyCurrentTable);
            }
        }
    }

    m_rt->PopAxisAlignedClip();

    // Buttons
    const bool hasSelection{m_historySelected >= 0
                            && m_historySelected < static_cast<int>(m_historySnapshots.size())};
    DrawReviewButton(hg.deleteBtn, L"Delete", false, m_historyHover == 2 && hasSelection, s);
    DrawReviewButton(hg.closeBtn, L"Close", false, m_historyHover == 0, s);
    DrawReviewButton(hg.restoreBtn, L"Restore", hasSelection, m_historyHover == 1 && hasSelection, s);

    // If no selection, dim the Restore + Delete buttons
    if (!hasSelection)
    {
        m_brush->SetColor(D2D1::ColorF(s.card.fill.r, s.card.fill.g, s.card.fill.b, 0.5f));
        m_rt->FillRoundedRectangle(D2D1::RoundedRect(hg.restoreBtn, 6.0f, 6.0f), m_brush);
        m_rt->FillRoundedRectangle(D2D1::RoundedRect(hg.deleteBtn, 6.0f, 6.0f), m_brush);
    }
}

void ui::MainWindow::Paint()
{
    if (FAILED(EnsureRenderTarget())) return;
    const theme::ColorScheme& s = m_theme.Current();

    m_rt->BeginDraw();
    m_rt->Clear(s.windowBg);

    const D2D1_SIZE_F sz{m_rt->GetSize()};
    const float pad{16.0f};

    DrawCaption(s, sz.width);

    m_grid.Paint(m_rt, m_brush, ui::GridFonts{m_fmtName, m_fmtValue, m_fmtHeader}, s,
                 D2D1::RectF(pad, kCaptionH + 8.0f, sz.width - pad, sz.height - 56.0f));

    // Detail strip: expanded path / validity / duplicate info for the selected row.
    if (const auto detail{m_grid.GetSelectionDetail()})
    {
        std::wstring text;
        D2D1_COLOR_F color{s.headerSubtext};

        if (!detail->valid)
        {
            text = L"\x2717 " + detail->expandedPath + L" \x2014 path not found";
            color = s.rowInvalid.text;
        }
        else if (!detail->duplicateDesc.empty())
        {
            text = L"\x2192 " + detail->expandedPath + L" \x2014 " + detail->duplicateDesc;
            color = s.rowDuplicate.text;
        }
        else if (detail->expandedPath != detail->displayPath)
        {
            text = L"\x2192 " + detail->expandedPath;
        }
        // else: expanded == display -> nothing to show

        if (!text.empty())
        {
            DrawString(text, m_fmtSub,
                       D2D1::RectF(pad, sz.height - 52.0f, sz.width - pad, sz.height - 32.0f),
                       color);
        }
    }

    std::wstring footer = std::format(
        L"{}   \x2022   {} user, {} machine{}   \x2022   Ins/Del/Alt+\x2191\x2193 entries   \x2022   Ctrl+C copy   \x2022   Ctrl+S apply   \x2022   Ctrl+H history   \x2022   F1/F2/F3 theme",
        std::wstring(s.name.begin(), s.name.end()), m_userCount, m_machineCount,
        m_elevated ? L"" : L"  (machine read-only)");
    if (m_zoom != 1.0f)
        footer += std::format(L"   \x2022   {}%", static_cast<int>(m_zoom * 100));
    DrawString(footer, m_fmtSub,
             D2D1::RectF(pad, sz.height - 28.0f, sz.width - pad, sz.height - 8.0f), s.headerSubtext);

    if (m_reviewOpen) PaintReview(s, sz);
    if (m_historyOpen) PaintHistory(s, sz);

    if (m_rt->EndDraw() == D2DERR_RECREATE_TARGET)
    {
        DiscardDeviceResources();
        InvalidateRect(m_hwnd, nullptr, FALSE); // re-paint will recreate the target
    }
}

// --- HandleMessage ---

LRESULT ui::MainWindow::HandleMessage(UINT msg, WPARAM wp, LPARAM lp)
{
    // History modal owns input when open (mutually exclusive with review).
    if (m_historyOpen)
    {
        switch (msg)
        {
        case WM_KEYDOWN:
            if (wp == VK_ESCAPE) { CloseHistory(); return 0; }
            if (wp == VK_RETURN && m_historySelected >= 0) { RestoreSnapshot(); return 0; }
            if (wp == VK_DELETE) { DeleteSelectedSnapshot(); return 0; }
            if (wp == VK_UP || wp == VK_DOWN)
            {
                const int n{static_cast<int>(m_historySnapshots.size())};
                if (n > 0)
                {
                    int newSel;
                    if (wp == VK_UP)
                        newSel = (m_historySelected <= 0) ? 0 : m_historySelected - 1;
                    else
                        newSel = (m_historySelected < 0) ? 0 : std::min(m_historySelected + 1, n - 1);
                    HistorySelect(newSel);
                    if (m_rt)
                    {
                        const HistoryGeom hg{HistoryLayout(m_rt->GetSize())};
                        HistoryEnsureVisible(hg, m_historySelected);
                    }
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                }
                return 0;
            }
            return 0;
        case WM_MOUSEMOVE:
            if (m_rt)
            {
                const float scale{DipScale(m_hwnd)};
                const float x{GET_X_LPARAM(lp) / scale}, my{GET_Y_LPARAM(lp) / scale};
                const HistoryGeom hg{HistoryLayout(m_rt->GetSize())};
                bool need{false};
                const int btnHover{Contains(hg.closeBtn, x, my) ? 0
                    : (Contains(hg.restoreBtn, x, my) ? 1
                    : (Contains(hg.deleteBtn, x, my) ? 2 : -1))};
                if (btnHover != m_historyHover) { m_historyHover = btnHover; need = true; }
                const int rowHover{HistoryRowAtPoint(hg, x, my)};
                if (rowHover != m_historyRowHover) { m_historyRowHover = rowHover; need = true; }
                if (need) InvalidateRect(m_hwnd, nullptr, FALSE);
            }
            return 0;
        case WM_LBUTTONDOWN:
            if (m_rt)
            {
                const float scale{DipScale(m_hwnd)};
                const float x{GET_X_LPARAM(lp) / scale}, my{GET_Y_LPARAM(lp) / scale};
                const HistoryGeom hg{HistoryLayout(m_rt->GetSize())};
                if (Contains(hg.closeBtn, x, my))
                    { m_eatNextDblClk = true; CloseHistory(); }
                else if (Contains(hg.restoreBtn, x, my) && m_historySelected >= 0)
                    { m_eatNextDblClk = true; RestoreSnapshot(); }
                else if (Contains(hg.deleteBtn, x, my) && m_historySelected >= 0)
                    { m_eatNextDblClk = true; DeleteSelectedSnapshot(); }
                else if (!Contains(hg.card, x, my))
                    { m_eatNextDblClk = true; CloseHistory(); }
                else
                {
                    const int row{HistoryRowAtPoint(hg, x, my)};
                    if (row >= 0 && row != m_historySelected)
                    {
                        HistorySelect(row);
                        InvalidateRect(m_hwnd, nullptr, FALSE);
                    }
                    else if (row >= 0 && row == m_historySelected)
                    {
                        // Click on already-selected: deselect
                        HistorySelect(-1);
                        InvalidateRect(m_hwnd, nullptr, FALSE);
                    }
                }
            }
            return 0;
        case WM_MOUSEWHEEL:
            if (m_rt)
            {
                const HistoryGeom hg{HistoryLayout(m_rt->GetSize())};
                const float viewH{hg.list.bottom - hg.list.top};
                const float maxScroll{std::max(0.0f, HistoryListContentH(hg) - viewH)};
                m_historyScroll -= (static_cast<float>(GET_WHEEL_DELTA_WPARAM(wp)) / WHEEL_DELTA) * 3.0f * hg.rowH;
                m_historyScroll = std::clamp(m_historyScroll, 0.0f, maxScroll);
                InvalidateRect(m_hwnd, nullptr, FALSE);
            }
            return 0;
        case WM_LBUTTONUP:
        case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU:
            return 0;
        default:
            break;
        }
    }
    // While the apply-review modal is open it owns input; paint/size/etc. fall through.
    if (m_reviewOpen)
    {
        switch (msg)
        {
        case WM_KEYDOWN:
            if (wp == VK_RETURN) ApplyReviewed();
            else if (wp == VK_ESCAPE) CancelReview();
            return 0;
        case WM_MOUSEWHEEL:
            return 0;
        case WM_MOUSEMOVE:
            if (m_rt)
            {
                const float scale{DipScale(m_hwnd)};
                const float x{GET_X_LPARAM(lp) / scale}, my{GET_Y_LPARAM(lp) / scale};
                const ReviewGeom g{ReviewLayout(m_rt->GetSize())};
                const int hover{Contains(g.cancelBtn, x, my) ? 0 : (Contains(g.applyBtn, x, my) ? 1 : -1)};
                if (hover != m_reviewHover) { m_reviewHover = hover; InvalidateRect(m_hwnd, nullptr, FALSE); }
            }
            return 0;
        case WM_LBUTTONDOWN:
            if (m_rt)
            {
                const float scale{DipScale(m_hwnd)};
                const float x{GET_X_LPARAM(lp) / scale}, my{GET_Y_LPARAM(lp) / scale};
                const ReviewGeom g{ReviewLayout(m_rt->GetSize())};
                if (Contains(g.applyBtn, x, my)) { m_eatNextDblClk = true; ApplyReviewed(); }
                else if (Contains(g.cancelBtn, x, my)) { m_eatNextDblClk = true; CancelReview(); }
                else if (!Contains(g.card, x, my)) { m_eatNextDblClk = true; CancelReview(); } // scrim = cancel
            }
            return 0;
        case WM_LBUTTONUP:
            return 0;
        case WM_LBUTTONDBLCLK:
            return 0; // never let a double-click reach the grid while modal
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU:
            return 0; // suppress context menu while modal
        default:
            break;
        }
    }
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
        const float scale{DipScale(m_hwnd)};
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
    case WM_SYSKEYDOWN:
        // Alt+Up / Alt+Down move the selected path entry (Alt combos arrive here).
        if ((wp == VK_UP || wp == VK_DOWN) && (lp & (1 << 29)))
        {
            if (m_grid.MoveEntry(wp == VK_UP ? -1 : 1)) InvalidateRect(m_hwnd, nullptr, FALSE);
            return 0;
        }
        break; // DefWindowProc handles other system keys (Alt+F4, Alt+Space, ...)
    case WM_KEYDOWN:
    {
        if (wp == 'S' && (GetKeyState(VK_CONTROL) & 0x8000)) { SaveChanges(); return 0; }
        if (wp == 'C' && (GetKeyState(VK_CONTROL) & 0x8000)) { CopyToClipboard(m_grid.CopyText()); return 0; }
        if (wp == 'H' && (GetKeyState(VK_CONTROL) & 0x8000)) { OpenHistory(); return 0; }
        if (wp == '0' && (GetKeyState(VK_CONTROL) & 0x8000))
        {
            if (m_grid.IsEditing()) EndEdit(true);
            m_zoom = 1.0f;
            CreateFonts();
            m_grid.SetZoom(m_zoom);
            RefreshEditFont();
            m_settings.appearance.zoom.set(static_cast<int32_t>(m_zoom * 100.0f));
            m_settings.save();
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return 0;
        }
        if (wp == VK_RETURN) { BeginEditFromGrid(); return 0; }
        if (wp == VK_INSERT)
        {
            if (m_grid.SelectedIsPathEntry())
            {
                if (m_grid.AddEntry()) { InvalidateRect(m_hwnd, nullptr, FALSE); BeginEditFromGrid(); }
            }
            else if (m_grid.SelectionEditable() || !m_grid.HasSelection())
            {
                if (m_grid.AddVariable()) { InvalidateRect(m_hwnd, nullptr, FALSE); BeginEditNameFromGrid(); }
            }
            return 0;
        }
        if (wp == VK_DELETE)
        {
            if (m_grid.SelectedIsPathEntry())
                Repaint(m_hwnd, m_grid.RemoveEntry());
            else
                Repaint(m_hwnd, m_grid.RemoveVariable());
            return 0;
        }
        const char* want{nullptr};
        if (wp == VK_F1) want = "dark";
        else if (wp == VK_F2) want = "light";
        else if (wp == VK_F3) want = "blue";
        if (want)
        {
            if (m_grid.IsEditing()) EndEdit(true); // defensive: never switch theme mid-edit
            if (m_theme.SelectByName(want))
            {
                ApplyTitleBar();
                if (m_editBrush) RefreshEditBrush(); // re-tint the cached editor brush
                m_settings.appearance.theme.set(m_theme.Current().name);
                m_settings.save();
                InvalidateRect(m_hwnd, nullptr, FALSE);
            }
        }
        else
        {
            Repaint(m_hwnd, m_grid.OnKey(static_cast<int>(wp)));
        }
        return 0;
    }
    case WM_MOUSEMOVE:
    {
        if (!m_tracking)
        {
            TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, m_hwnd, 0};
            TrackMouseEvent(&tme);
            m_tracking = true;
        }
        const float scale{DipScale(m_hwnd)};
        RECT rc{};
        GetClientRect(m_hwnd, &rc);
        const float xDip{GET_X_LPARAM(lp) / scale}, yDip{GET_Y_LPARAM(lp) / scale};
        const int cap = CaptionButtonAt(xDip, yDip, rc.right / scale);
        bool need{false};
        if (cap != m_capHover) { m_capHover = cap; need = true; }
        need |= m_grid.OnMouseMove(xDip, yDip);
        Repaint(m_hwnd, need);
        return 0;
    }
    case WM_MOUSELEAVE:
    {
        m_tracking = false;
        bool need{m_capHover != -1};
        m_capHover = -1;
        need |= m_grid.OnMouseLeave();
        Repaint(m_hwnd, need);
        return 0;
    }
    case WM_LBUTTONDOWN:
    {
        m_eatNextDblClk = false; // a fresh press cancels any pending dblclk-swallow
        const float scale{DipScale(m_hwnd)};
        RECT rc{};
        GetClientRect(m_hwnd, &rc);
        const float xDip{GET_X_LPARAM(lp) / scale}, yDip{GET_Y_LPARAM(lp) / scale};
        const int cap = CaptionButtonAt(xDip, yDip, rc.right / scale);
        if (cap == 0) { ShowWindow(m_hwnd, SW_MINIMIZE); return 0; }
        if (cap == 1) { ShowWindow(m_hwnd, IsZoomed(m_hwnd) ? SW_RESTORE : SW_MAXIMIZE); return 0; }
        if (cap == 2) { PostMessageW(m_hwnd, WM_CLOSE, 0, 0); return 0; }
        SetFocus(m_hwnd);
        SetCapture(m_hwnd);
        const bool shift{(GetKeyState(VK_SHIFT) & 0x8000) != 0};
        const bool ctrl{(GetKeyState(VK_CONTROL) & 0x8000) != 0};
        Repaint(m_hwnd, m_grid.OnLButtonDown(xDip, yDip, shift, ctrl));
        return 0;
    }
    case WM_LBUTTONUP:
        ReleaseCapture();
        Repaint(m_hwnd, m_grid.OnLButtonUp());
        return 0;
    case WM_LBUTTONDBLCLK:
    {
        if (m_eatNextDblClk) { m_eatNextDblClk = false; return 0; } // trailing dblclk of a modal close
        const float scale{DipScale(m_hwnd)};
        BeginEditAt(GET_X_LPARAM(lp) / scale, GET_Y_LPARAM(lp) / scale);
        return 0;
    }
    case WM_RBUTTONDOWN:
    {
        const float scale{DipScale(m_hwnd)};
        const float xDip{GET_X_LPARAM(lp) / scale}, yDip{GET_Y_LPARAM(lp) / scale};
        const bool shift{(GetKeyState(VK_SHIFT) & 0x8000) != 0};
        const bool ctrl{(GetKeyState(VK_CONTROL) & 0x8000) != 0};
        Repaint(m_hwnd, m_grid.OnRButtonDown(xDip, yDip, shift, ctrl));
        return 0;
    }
    case WM_CONTEXTMENU:
    {
        if (m_grid.IsEditing()) EndEdit(true);
        if (!m_grid.HasSelection()) return 0;

        int screenX{}, screenY{};
        if (GET_X_LPARAM(lp) == -1 && GET_Y_LPARAM(lp) == -1)
        {
            // Keyboard-triggered (Apps key / Shift+F10): position at window center.
            RECT rc{};
            GetClientRect(m_hwnd, &rc);
            POINT center{(rc.left + rc.right) / 2, (rc.top + rc.bottom) / 2};
            ClientToScreen(m_hwnd, &center);
            screenX = center.x;
            screenY = center.y;
        }
        else
        {
            screenX = GET_X_LPARAM(lp);
            screenY = GET_Y_LPARAM(lp);
        }
        ShowGridContextMenu(screenX, screenY);
        return 0;
    }
    case WM_MOUSEWHEEL:
        if (GetKeyState(VK_CONTROL) & 0x8000)
        {
            if (m_grid.IsEditing()) EndEdit(true);
            const float delta{(GET_WHEEL_DELTA_WPARAM(wp) > 0) ? 0.1f : -0.1f};
            m_zoom = std::clamp(m_zoom + delta, 0.5f, 2.0f);
            CreateFonts();
            m_grid.SetZoom(m_zoom);
            RefreshEditFont();
            m_settings.appearance.zoom.set(static_cast<int32_t>(m_zoom * 100.0f));
            m_settings.save();
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return 0;
        }
        if (m_grid.IsEditing()) EndEdit(true);
        Repaint(m_hwnd, m_grid.OnWheel(GET_WHEEL_DELTA_WPARAM(wp)));
        return 0;
    case WM_MEASUREITEM:
    {
        auto* mis = reinterpret_cast<MEASUREITEMSTRUCT*>(lp);
        if (mis->CtlType == ODT_MENU)
        {
            const float scale{DipScale(m_hwnd)};
            mis->itemWidth = static_cast<UINT>(200.0f * scale);
            mis->itemHeight = static_cast<UINT>(32.0f * scale);
        }
        return TRUE;
    }
    case WM_DRAWITEM:
    {
        auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lp);
        if (dis->CtlType == ODT_MENU)
        {
            const theme::ColorScheme& s{m_theme.Current()};
            const bool selected{(dis->itemState & ODS_SELECTED) != 0};
            const bool grayed{(dis->itemState & ODS_GRAYED) != 0};

            // Background: card.fill normally, rowHover.fill when highlighted (and not disabled).
            const COLORREF bg{ToColorRef((selected && !grayed) ? s.rowHover.fill : s.card.fill)};
            HBRUSH hbr{CreateSolidBrush(bg)};
            FillRect(dis->hDC, &dis->rcItem, hbr);
            DeleteObject(hbr);

            // Font: use the cached GDI edit font if available, else system default.
            HGDIOBJ prevFont{nullptr};
            if (m_editFont) prevFont = SelectObject(dis->hDC, m_editFont);

            SetBkMode(dis->hDC, TRANSPARENT);

            const auto* data = reinterpret_cast<const MenuItemData*>(dis->itemData);
            if (data)
            {
                const int pad{static_cast<int>(8.0f * DipScale(m_hwnd))};
                RECT rcLabel{dis->rcItem};
                rcLabel.left += pad;
                rcLabel.right -= pad;

                // Label (left-aligned): headerText or headerSubtext if disabled.
                SetTextColor(dis->hDC, ToColorRef(grayed ? s.headerSubtext : (selected ? s.rowHover.text : s.headerText)));
                DrawTextW(dis->hDC, data->label.c_str(), static_cast<int>(data->label.size()),
                          &rcLabel, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);

                // Accelerator hint (right-aligned, always headerSubtext).
                if (!data->accel.empty())
                {
                    SetTextColor(dis->hDC, ToColorRef(s.headerSubtext));
                    DrawTextW(dis->hDC, data->accel.c_str(), static_cast<int>(data->accel.size()),
                              &rcLabel, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
                }
            }

            if (prevFont) SelectObject(dis->hDC, prevFont);
        }
        return TRUE;
    }
    case WM_CTLCOLOREDIT:
        if (reinterpret_cast<HWND>(lp) == m_edit)
        {
            const theme::ColorScheme& s{m_theme.Current()};
            SetTextColor(reinterpret_cast<HDC>(wp), ToColorRef(s.edit.text));
            SetBkColor(reinterpret_cast<HDC>(wp), ToColorRef(s.edit.fill));
            if (!m_editBrush) RefreshEditBrush();
            return reinterpret_cast<LRESULT>(m_editBrush);
        }
        break;
    case WM_COMMAND:
        if (lp == 0) // menu command
        {
            switch (LOWORD(wp))
            {
            case kMenuCopy:
                CopyToClipboard(m_grid.CopyText());
                return 0;
            case kMenuInsert:
                if (m_grid.SelectedIsPathEntry())
                {
                    if (m_grid.AddEntry()) { InvalidateRect(m_hwnd, nullptr, FALSE); BeginEditFromGrid(); }
                }
                else
                {
                    if (m_grid.AddVariable()) { InvalidateRect(m_hwnd, nullptr, FALSE); BeginEditNameFromGrid(); }
                }
                return 0;
            case kMenuRemove:
                if (m_grid.SelectedIsPathEntry())
                    Repaint(m_hwnd, m_grid.RemoveEntry());
                else
                    Repaint(m_hwnd, m_grid.RemoveVariable());
                return 0;
            }
        }
        if (reinterpret_cast<HWND>(lp) == m_edit && HIWORD(wp) == EN_KILLFOCUS)
        {
            EndEdit(true);
            return 0;
        }
        break;
    case WM_APP_EDIT_END:
    {
        const bool wasName{m_grid.IsEditingName()};
        EndEdit(wp != 0);
        if (wp == 2) // Tab
        {
            if (wasName && lp == 0) // forward Tab from name -> edit value of same row
                BeginEditFromGrid();
            else if (m_grid.SelectNextEditable(lp != 0 ? -1 : 1))
                BeginEditFromGrid();
        }
        return 0;
    }
    case WM_PAINT:
    {
        PAINTSTRUCT ps{};
        BeginPaint(m_hwnd, &ps);
        Paint();
        EndPaint(m_hwnd, &ps);
        return 0;
    }
    case WM_SIZE:
        if (m_grid.IsEditing()) EndEdit(true);
        if (m_rt)
        {
            if (m_rt->Resize(D2D1::SizeU(LOWORD(lp), HIWORD(lp))) == D2DERR_RECREATE_TARGET)
                DiscardDeviceResources();
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_DPICHANGED:
    {
        if (m_grid.IsEditing()) EndEdit(true);
        const RECT* const r{reinterpret_cast<RECT*>(lp)};
        SetWindowPos(m_hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        RefreshEditFont(); // re-create the editor font at the new DPI for next time
        DiscardDeviceResources();
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_DESTROY:
    {
        WINDOWPLACEMENT wpl{};
        wpl.length = sizeof(wpl);
        GetWindowPlacement(m_hwnd, &wpl);
        m_settings.window.x.set(static_cast<int32_t>(wpl.rcNormalPosition.left));
        m_settings.window.y.set(static_cast<int32_t>(wpl.rcNormalPosition.top));
        m_settings.window.width.set(static_cast<int32_t>(wpl.rcNormalPosition.right - wpl.rcNormalPosition.left));
        m_settings.window.height.set(static_cast<int32_t>(wpl.rcNormalPosition.bottom - wpl.rcNormalPosition.top));
        m_settings.window.maximized.set(wpl.showCmd == SW_MAXIMIZE);
        m_settings.save();
        if (m_editBrush) { DeleteObject(m_editBrush); m_editBrush = nullptr; }
        if (m_editFont)     { DeleteObject(m_editFont);     m_editFont = nullptr; }
        if (m_editFontName) { DeleteObject(m_editFontName); m_editFontName = nullptr; }
        ReleaseGraphics();
        PostQuitMessage(0);
        return 0;
    }
    }
    return DefWindowProcW(m_hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------------------
// App class -- application lifecycle
// ---------------------------------------------------------------------------

bool ui::App::InitLogging()
{
    wchar_t* appdata{nullptr};
    if (SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &appdata) != S_OK)
    {
        CoTaskMemFree(appdata);
        spdlog::error("SHGetKnownFolderPath failed; cannot set up logging");
        return false;
    }
    std::filesystem::path logPath{appdata};
    CoTaskMemFree(appdata);
    logPath /= L"environ";
    std::filesystem::create_directories(logPath);
    logPath /= L"environ.log";
    auto fileSink{std::make_shared<spdlog::sinks::basic_file_sink_mt>(logPath.string(), true)};
    auto logger{std::make_shared<spdlog::logger>("environ", fileSink)};
    logger->set_level(spdlog::level::info);
    logger->flush_on(spdlog::level::info);
    spdlog::set_default_logger(logger);
    return true;
}

int ui::App::MessageLoop()
{
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

int ui::App::Run(HINSTANCE hInst, int nCmdShow)
{
    m_hInst = hInst;
    m_nCmdShow = nCmdShow;

    if (!InitLogging()) return 1;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    MainWindow wnd;
    if (!wnd.Create(hInst, nCmdShow))
        return 1;

    return MessageLoop();
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int nCmdShow)
{
    ui::App app;
    return app.Run(hInst, nCmdShow);
}
