#include "precomp.h"

// MainWindow — environ's main application window.
// Inherits D2DWindow (frameless-window boilerplate) and acts as a shell
// (title bar, footer, modal overlays) that hosts an active View.
// The GridView is the default view; Ctrl+H switches to HistoryView.

#include "mainwindow.h"
#include "EnvStore.h"
#include "EnvWriter.h"
#include "SnapshotStore.h"

#ifndef DWMWA_SYSTEMBACKDROP_TYPE
#define DWMWA_SYSTEMBACKDROP_TYPE 38
#endif
#ifndef DWMSBT_MAINWINDOW
#define DWMSBT_MAINWINDOW 2
#endif

namespace
{
    // Posted by the editor subclass back to the window. wParam: 0 = cancel, 1 = commit,
    // 2 = commit + move (Tab); lParam (Tab only): 1 = Shift (move up), 0 = move down.
    constexpr UINT WM_APP_EDIT_END{WM_APP + 1};

    bool Contains(const D2D1_RECT_F& r, float x, float y)
    {
        return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
    }

    COLORREF ToColorRef(const D2D1_COLOR_F& c)
    {
        return RGB(static_cast<BYTE>(c.r * 255.0f), static_cast<BYTE>(c.g * 255.0f), static_cast<BYTE>(c.b * 255.0f));
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

    // Nav panel constants.
    constexpr float kBurgerW{46.0f};
    constexpr float kBurgerH{40.0f};
    constexpr float kNavWidth{220.0f};
    constexpr float kNavItemH{36.0f};
    constexpr wchar_t kGlyphBurger{0xE700}; // Segoe Fluent Icons "GlobalNavigationButton"

    // Context menu item IDs (used by WM_COMMAND for owner-draw menu dispatching).
    constexpr UINT kMenuCopy{1001};
    constexpr UINT kMenuInsert{1002};
    constexpr UINT kMenuRemove{1003};

    // Owner-drawn menu item data -- stored in MENUITEMINFO::dwItemData.
    struct MenuItemData
    {
        std::wstring label;
        std::wstring accel;
    };
}

// ---------------------------------------------------------------------------
// MainWindow
// ---------------------------------------------------------------------------

// --- Init ---

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

    int wx{m_settings.window.x.get()};
    int wy{m_settings.window.y.get()};
    int ww{m_settings.window.width.get()};
    int wh{m_settings.window.height.get()};
    if (wx < 0 || wy < 0) { wx = CW_USEDEFAULT; wy = CW_USEDEFAULT; }
    if (ww <= 0) ww = 860;
    if (wh <= 0) wh = 620;

    if (!CreateD2DWindow(L"EnvironWin32Host", L"environ", wx, wy, ww, wh,
                         m_settings.window.maximized.get() ? SW_MAXIMIZE : m_nCmdShow))
    {
        ReleaseGraphics();
        return false;
    }

    ApplyTitleBar();
    const int backdrop{DWMSBT_MAINWINDOW};
    DwmSetWindowAttribute(m_hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop));

    return true;
}

// --- Fonts ---

bool ui::MainWindow::CreateFonts()
{
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

// --- Title bar ---

void ui::MainWindow::ApplyTitleBar()
{
    ApplyDarkTitleBar(m_theme.Current().darkTitleBar);
}

// --- View plumbing ---

ui::ViewContext ui::MainWindow::MakeContext() const
{
    return ViewContext{
        m_rt,
        m_brush,
        &m_theme.Current(),
        m_hwnd,
        m_zoom,
        DipScale(),
        m_fmtSub.get(),
        m_fmtName.get(),
        m_fmtValue.get(),
        m_fmtHeader.get(),
        m_fmtButton.get(),
        m_fmtMono.get(),
        m_fmtGlyph.get(),
    };
}

D2D1_RECT_F ui::MainWindow::ViewBounds(const D2D1_SIZE_F& sz) const
{
    const float pad{16.0f};
    const float left{m_navOpen ? kNavWidth : pad};
    // View sits between caption (48 DIP) and footer (bottom 28 DIP),
    // with a 4 DIP gap above the footer for spacing.
    return D2D1::RectF(left, 40.0f + 8.0f, sz.width - pad, sz.height - 32.0f);
}

void ui::MainWindow::SwitchToView(View* view)
{
    if (m_activeView == view) return;
    m_activeView->Deactivate();
    m_activeView = view;
    const auto ctx{MakeContext()};
    m_activeView->Activate(ctx);
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void ui::MainWindow::CheckHistoryAction()
{
    const auto action{m_historyView.PendingAction()};
    if (action == HistoryView::Action::None) return;
    m_historyView.ClearPendingAction();
    m_eatNextDblClk = true;

    if (action == HistoryView::Action::Restore)
        ApplyHistoryRestore();

    SwitchToView(&m_gridView);
}

void ui::MainWindow::ApplyHistoryRestore()
{
    auto data{m_historyView.TakeRestoreData()};
    m_grid.SetDataForRestore(data.curUser, data.curMachine,
                             data.snapUser, data.snapMachine,
                             Environ::core::is_elevated());
    m_gridView.SetCounts(data.snapUser.size(), data.snapMachine.size());
}

// --- Data / save ---

void ui::MainWindow::LoadData()
{
    m_gridView.LoadData(Environ::core::is_elevated());
}

void ui::MainWindow::SaveChanges()
{
    using namespace Environ::core;
    if (m_grid.IsEditing())
    {
        const auto ctx{MakeContext()};
        m_gridView.OnEditEnd(ctx, true, false, false);
    }
    if (!m_gridView.HasChanges())
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
        m_grid.OriginalVars(Scope::Machine), m_reviewCurMachine,
        Environ::core::is_elevated())};

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

// --- Painting ---

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
    m_rt->DrawTextW(label, static_cast<UINT32>(wcslen(label)), m_fmtButton.get(), r, m_brush,
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

    DrawString(L"Apply changes?", m_fmtName.get(),
               D2D1::RectF(g.card.left + 20.0f, g.card.top + 12.0f, g.card.right - 20.0f, g.card.top + 44.0f),
               s.headerText);

    m_rt->PushAxisAlignedClip(g.list, D2D1_ANTIALIAS_MODE_ALIASED);
    float y{g.list.top};
    const auto group = [&](const wchar_t* label, const std::vector<Environ::core::EnvChange>& changes) {
        if (changes.empty()) return;
        DrawString(label, m_fmtHeader.get(), D2D1::RectF(g.list.left, y, g.list.right, y + 22.0f), s.headerSubtext);
        y += 22.0f;
        for (const Environ::core::EnvChange& c : changes)
        {
            DrawString(L"    " + c.describe(), m_fmtValue.get(),
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

// --- Nav panel ---

int ui::MainWindow::NavItemAt(float x, float y, const D2D1_SIZE_F& sz) const
{
    if (!m_navOpen) return -1;
    // Panel rect: (0, 48, kNavWidth, sz.height - 32)
    if (x < 0.0f || x >= kNavWidth || y < 48.0f || y >= sz.height - 32.0f)
        return -1;

    // Items start at panel top (48) + 8 padding.
    // Section header "THEME" (not clickable): 36 DIP
    // Items 0,1,2 (Dark/Light/Blue): 36 DIP each
    // Gap: 12 DIP
    // Items 3,4 (History/Save): 36 DIP each
    const float startY{48.0f + 8.0f};
    const float afterHeader{startY + kNavItemH}; // after "THEME" header
    if (y >= afterHeader && y < afterHeader + 3 * kNavItemH)
    {
        return static_cast<int>((y - afterHeader) / kNavItemH); // 0,1,2
    }
    const float afterGap{afterHeader + 3 * kNavItemH + 12.0f};
    if (y >= afterGap && y < afterGap + 2 * kNavItemH)
    {
        return 3 + static_cast<int>((y - afterGap) / kNavItemH); // 3,4
    }
    return -1;
}

void ui::MainWindow::HandleNavClick(int item)
{
    const auto ctx{MakeContext()};
    switch (item)
    {
    case 0: case 1: case 2:
    {
        const char* names[]{"dark", "light", "blue"};
        if (m_grid.IsEditing()) m_gridView.OnEditEnd(ctx, true, false, false);
        if (m_theme.SelectByName(names[item]))
        {
            ApplyTitleBar();
            m_gridView.RefreshEditBrush();
            m_settings.appearance.theme.set(m_theme.Current().name);
            m_settings.save();
        }
        break;
    }
    case 3: // History
        if (m_activeView == &m_historyView)
            SwitchToView(&m_gridView);
        else
        {
            if (m_grid.IsEditing()) m_gridView.OnEditEnd(ctx, true, false, false);
            SwitchToView(&m_historyView);
        }
        break;
    case 4: // Save
        SaveChanges();
        break;
    }
}

void ui::MainWindow::PaintNav(const theme::ColorScheme& s, const D2D1_SIZE_F& sz)
{
    // Panel background
    const D2D1_RECT_F panel{D2D1::RectF(0.0f, 48.0f, kNavWidth, sz.height - 32.0f)};
    m_brush->SetColor(s.card.fill);
    m_rt->FillRectangle(panel, m_brush);

    // Right edge border
    m_brush->SetColor(s.card.border);
    m_rt->DrawLine(D2D1::Point2F(kNavWidth, 48.0f),
                   D2D1::Point2F(kNavWidth, sz.height - 32.0f), m_brush, 1.0f);

    const float startY{48.0f + 8.0f};
    const float padLeft{16.0f};
    const float padRight{kNavWidth - 12.0f};

    // Section header: "THEME"
    DrawString(L"THEME", m_fmtHeader.get(),
               D2D1::RectF(padLeft, startY, padRight, startY + kNavItemH), s.headerSubtext);

    // Theme items
    const wchar_t* themeLabels[]{L"Dark", L"Light", L"Blue"};
    const wchar_t* themeHints[]{L"F1", L"F2", L"F3"};
    const auto& curName{m_theme.Current().name};
    const std::string itemNames[]{"dark", "light", "blue"};

    for (int i{0}; i < 3; ++i)
    {
        const float iy{startY + kNavItemH + i * kNavItemH};
        const D2D1_RECT_F itemRect{D2D1::RectF(0.0f, iy, kNavWidth, iy + kNavItemH)};
        const bool hovered{m_navHover == i};
        const bool active{curName == itemNames[i]};

        if (hovered)
        {
            m_brush->SetColor(s.rowHover.fill);
            m_rt->FillRectangle(itemRect, m_brush);
        }

        // Label
        const D2D1_COLOR_F labelColor{hovered ? s.rowHover.text : (active ? s.accent : s.headerText)};
        std::wstring label{themeLabels[i]};
        if (active) label = L"\x2022  " + label; // bullet for active theme
        DrawString(label, m_fmtValue.get(),
                   D2D1::RectF(padLeft, iy, padRight - 40.0f, iy + kNavItemH), labelColor);

        // Right-aligned hint
        DrawString(themeHints[i], m_fmtValue.get(),
                   D2D1::RectF(padRight - 40.0f, iy, padRight, iy + kNavItemH), s.headerSubtext);
    }

    // Gap (12 DIP), then History + Save
    const float groupY{startY + kNavItemH + 3 * kNavItemH + 12.0f};
    const wchar_t* actionLabels[]{L"History", L"Save"};
    const wchar_t* actionHints[]{L"Ctrl+H", L"Ctrl+S"};

    for (int i{0}; i < 2; ++i)
    {
        const int idx{3 + i};
        const float iy{groupY + i * kNavItemH};
        const D2D1_RECT_F itemRect{D2D1::RectF(0.0f, iy, kNavWidth, iy + kNavItemH)};
        const bool hovered{m_navHover == idx};

        if (hovered)
        {
            m_brush->SetColor(s.rowHover.fill);
            m_rt->FillRectangle(itemRect, m_brush);
        }

        const D2D1_COLOR_F labelColor{hovered ? s.rowHover.text : s.headerText};
        DrawString(actionLabels[i], m_fmtValue.get(),
                   D2D1::RectF(padLeft, iy, padRight - 60.0f, iy + kNavItemH), labelColor);

        DrawString(actionHints[i], m_fmtValue.get(),
                   D2D1::RectF(padRight - 60.0f, iy, padRight, iy + kNavItemH), s.headerSubtext);
    }
}

void ui::MainWindow::OnPaint(const D2D1_SIZE_F& sz)
{
    const theme::ColorScheme& s{m_theme.Current()};
    m_rt->Clear(s.windowBg);

    DrawCaption(s, sz.width, L"environ", kBurgerW);

    // Burger button (painted over caption area)
    {
        const D2D1_RECT_F burgerRect{D2D1::RectF(0.0f, 0.0f, kBurgerW, kBurgerH)};
        if (m_navBurgerHover)
        {
            m_brush->SetColor(s.rowHover.fill);
            m_rt->FillRectangle(burgerRect, m_brush);
        }
        m_brush->SetColor(s.headerText);
        m_rt->DrawTextW(&kGlyphBurger, 1, m_fmtGlyph.get(), burgerRect, m_brush,
                        D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }

    // Nav panel (below caption, above footer)
    if (m_navOpen) PaintNav(s, sz);

    const auto ctx{MakeContext()};
    const auto bounds{ViewBounds(sz)};
    m_activeView->Paint(ctx, bounds);

    // Footer: active view provides the text.
    const float footerLeft{m_navOpen ? kNavWidth : 16.0f};
    const std::wstring footer{m_activeView->GetStatusText(ctx)};
    DrawString(footer, m_fmtSub.get(),
               D2D1::RectF(footerLeft, sz.height - 28.0f, sz.width - 16.0f, sz.height - 8.0f), s.headerSubtext);

    if (m_reviewOpen) PaintReview(s, sz);
}

// --- Virtual hooks ---

void ui::MainWindow::OnSize()
{
    const auto ctx{MakeContext()};
    m_activeView->OnSize(ctx);
}

void ui::MainWindow::OnDpiChanged()
{
    const auto ctx{MakeContext()};
    m_activeView->OnDpiChanged(ctx);
}

void ui::MainWindow::OnDestroy()
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

    // Release subclass text formats before base-class ReleaseGraphics.
    m_fmtSub.reset();
    m_fmtName.reset();
    m_fmtValue.reset();
    m_fmtHeader.reset();
    m_fmtButton.reset();
    m_fmtMono.reset();
}

// --- HandleMessage ---

LRESULT ui::MainWindow::HandleMessage(UINT msg, WPARAM wp, LPARAM lp)
{
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
                const float scale{DipScale()};
                const float x{GET_X_LPARAM(lp) / scale}, my{GET_Y_LPARAM(lp) / scale};
                const ReviewGeom g{ReviewLayout(m_rt->GetSize())};
                const int hover{Contains(g.cancelBtn, x, my) ? 0 : (Contains(g.applyBtn, x, my) ? 1 : -1)};
                if (hover != m_reviewHover) { m_reviewHover = hover; InvalidateRect(m_hwnd, nullptr, FALSE); }
            }
            return 0;
        case WM_LBUTTONDOWN:
            if (m_rt)
            {
                const float scale{DipScale()};
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

    // Burger button must return HTCLIENT so it's clickable, not draggable.
    if (msg == WM_NCHITTEST)
    {
        POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(m_hwnd, &pt);
        const float scale{DipScale()};
        const float xDip{pt.x / scale}, yDip{pt.y / scale};
        if (xDip >= 0.0f && xDip < kBurgerW && yDip >= 0.0f && yDip < kBurgerH)
            return HTCLIENT;
        return D2DWindow::HandleMessage(msg, wp, lp);
    }

    const auto ctx{MakeContext()};
    switch (msg)
    {
    case WM_SYSKEYDOWN:
        if (m_activeView->OnSysKey(ctx, static_cast<int>(wp), lp))
        {
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return 0;
        }
        break; // DefWindowProc handles other system keys (Alt+F4, Alt+Space, ...)
    case WM_KEYDOWN:
    {
        // Esc closes nav panel before anything else.
        if (m_navOpen && wp == VK_ESCAPE)
        {
            m_navOpen = false;
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return 0;
        }
        // Grid-only shortcuts: Ctrl+S, Ctrl+C.
        if (m_activeView == &m_gridView)
        {
            if (wp == 'S' && (GetKeyState(VK_CONTROL) & 0x8000)) { SaveChanges(); return 0; }
            if (wp == 'C' && (GetKeyState(VK_CONTROL) & 0x8000))
            {
                m_gridView.CopyToClipboard(m_grid.CopyText());
                return 0;
            }
        }
        // Ctrl+H toggles history view.
        if (wp == 'H' && (GetKeyState(VK_CONTROL) & 0x8000))
        {
            if (m_activeView == &m_historyView)
                SwitchToView(&m_gridView);
            else
            {
                if (m_grid.IsEditing()) m_gridView.OnEditEnd(ctx, true, false, false);
                SwitchToView(&m_historyView);
            }
            return 0;
        }
        if (wp == '0' && (GetKeyState(VK_CONTROL) & 0x8000))
        {
            if (m_grid.IsEditing()) m_gridView.OnEditEnd(ctx, true, false, false);
            m_zoom = 1.0f;
            CreateFonts();
            m_grid.SetZoom(m_zoom);
            m_gridView.RefreshEditFont(MakeContext());
            m_settings.appearance.zoom.set(static_cast<int32_t>(m_zoom * 100.0f));
            m_settings.save();
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return 0;
        }
        // Theme switching.
        const char* want{nullptr};
        if (wp == VK_F1) want = "dark";
        else if (wp == VK_F2) want = "light";
        else if (wp == VK_F3) want = "blue";
        if (want)
        {
            if (m_grid.IsEditing()) m_gridView.OnEditEnd(ctx, true, false, false);
            if (m_theme.SelectByName(want))
            {
                ApplyTitleBar();
                m_gridView.RefreshEditBrush();
                m_settings.appearance.theme.set(m_theme.Current().name);
                m_settings.save();
                InvalidateRect(m_hwnd, nullptr, FALSE);
            }
            return 0;
        }
        // Delegate to active view.
        Repaint(m_activeView->OnKey(ctx, static_cast<int>(wp)));
        CheckHistoryAction();
        return 0;
    }
    case WM_MOUSEMOVE:
    {
        SetupMouseTracking();
        const float scale{DipScale()};
        RECT rc{};
        GetClientRect(m_hwnd, &rc);
        const float xDip{GET_X_LPARAM(lp) / scale}, yDip{GET_Y_LPARAM(lp) / scale};
        const float wDip{rc.right / scale};

        // Burger button hover
        const bool burgerHover{xDip >= 0.0f && xDip < kBurgerW && yDip >= 0.0f && yDip < kBurgerH};
        bool need{false};
        if (burgerHover != m_navBurgerHover) { m_navBurgerHover = burgerHover; need = true; }

        // Nav item hover
        if (m_navOpen && m_rt)
        {
            const int navItem{NavItemAt(xDip, yDip, m_rt->GetSize())};
            if (navItem != m_navHover) { m_navHover = navItem; need = true; }
        }

        need |= UpdateCaptionHover(xDip, yDip, wDip);
        need |= m_activeView->OnMouseMove(ctx, xDip, yDip);
        Repaint(need);
        return 0;
    }
    case WM_MOUSELEAVE:
    {
        bool need{ResetCaptionTracking()};
        if (m_navBurgerHover) { m_navBurgerHover = false; need = true; }
        if (m_navHover != -1) { m_navHover = -1; need = true; }
        need |= m_activeView->OnMouseLeave();
        Repaint(need);
        return 0;
    }
    case WM_LBUTTONDOWN:
    {
        m_eatNextDblClk = false; // a fresh press cancels any pending dblclk-swallow
        const float scale{DipScale()};
        RECT rc{};
        GetClientRect(m_hwnd, &rc);
        const float xDip{GET_X_LPARAM(lp) / scale}, yDip{GET_Y_LPARAM(lp) / scale};

        // Burger button: toggle nav panel
        if (xDip >= 0.0f && xDip < kBurgerW && yDip >= 0.0f && yDip < kBurgerH)
        {
            m_navOpen = !m_navOpen;
            m_navHover = -1;
            m_eatNextDblClk = true;
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return 0;
        }

        // Nav panel click: dispatch item or close
        if (m_navOpen)
        {
            if (m_rt)
            {
                const int item{NavItemAt(xDip, yDip, m_rt->GetSize())};
                if (item >= 0)
                {
                    HandleNavClick(item);
                    m_navOpen = false;
                    m_navHover = -1;
                    m_eatNextDblClk = true;
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                    return 0;
                }
            }
            // Click outside nav panel area: close nav, fall through to normal dispatch
            m_navOpen = false;
            m_navHover = -1;
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }

        if (HandleCaptionClick(xDip, yDip, rc.right / scale)) return 0;
        SetFocus(m_hwnd);
        SetCapture(m_hwnd);
        const bool shift{(GetKeyState(VK_SHIFT) & 0x8000) != 0};
        const bool ctrl{(GetKeyState(VK_CONTROL) & 0x8000) != 0};
        Repaint(m_activeView->OnLButtonDown(ctx, xDip, yDip, shift, ctrl));
        CheckHistoryAction();
        return 0;
    }
    case WM_LBUTTONUP:
        ReleaseCapture();
        Repaint(m_activeView->OnLButtonUp());
        return 0;
    case WM_LBUTTONDBLCLK:
    {
        if (m_eatNextDblClk) { m_eatNextDblClk = false; return 0; } // trailing dblclk of a modal close
        const float scale{DipScale()};
        m_activeView->OnLButtonDblClk(ctx, GET_X_LPARAM(lp) / scale, GET_Y_LPARAM(lp) / scale);
        return 0;
    }
    case WM_RBUTTONDOWN:
    {
        const float scale{DipScale()};
        const float xDip{GET_X_LPARAM(lp) / scale}, yDip{GET_Y_LPARAM(lp) / scale};
        const bool shift{(GetKeyState(VK_SHIFT) & 0x8000) != 0};
        const bool ctrl{(GetKeyState(VK_CONTROL) & 0x8000) != 0};
        Repaint(m_activeView->OnRButtonDown(ctx, xDip, yDip, shift, ctrl));
        return 0;
    }
    case WM_CONTEXTMENU:
    {
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
        m_activeView->OnContextMenu(ctx, screenX, screenY);
        return 0;
    }
    case WM_MOUSEWHEEL:
        if (GetKeyState(VK_CONTROL) & 0x8000)
        {
            if (m_grid.IsEditing()) m_gridView.OnEditEnd(ctx, true, false, false);
            const float delta{(GET_WHEEL_DELTA_WPARAM(wp) > 0) ? 0.1f : -0.1f};
            m_zoom = std::clamp(m_zoom + delta, 0.5f, 2.0f);
            CreateFonts();
            m_grid.SetZoom(m_zoom);
            m_gridView.RefreshEditFont(MakeContext());
            m_settings.appearance.zoom.set(static_cast<int32_t>(m_zoom * 100.0f));
            m_settings.save();
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return 0;
        }
        Repaint(m_activeView->OnWheel(ctx, GET_WHEEL_DELTA_WPARAM(wp)));
        return 0;
    case WM_MEASUREITEM:
    {
        auto* mis = reinterpret_cast<MEASUREITEMSTRUCT*>(lp);
        if (mis->CtlType == ODT_MENU)
        {
            const float scale{DipScale()};
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

            // Font: use the cached GDI edit font from GridView if available.
            HGDIOBJ prevFont{nullptr};
            HFONT menuFont{m_gridView.EditFont()};
            if (menuFont) prevFont = SelectObject(dis->hDC, menuFont);

            SetBkMode(dis->hDC, TRANSPARENT);

            const auto* data = reinterpret_cast<const MenuItemData*>(dis->itemData);
            if (data)
            {
                const int pad{static_cast<int>(8.0f * DipScale())};
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
        if (m_gridView.IsEditControl(reinterpret_cast<HWND>(lp)))
            return m_gridView.OnCtlColorEdit(ctx, wp);
        break;
    case WM_COMMAND:
        if (lp == 0) // menu command
        {
            switch (LOWORD(wp))
            {
            case kMenuCopy:
                m_gridView.CopyToClipboard(m_grid.CopyText());
                return 0;
            case kMenuInsert:
                if (m_grid.SelectedIsPathEntry())
                {
                    if (m_grid.AddEntry()) { InvalidateRect(m_hwnd, nullptr, FALSE); m_gridView.BeginEditFromGrid(ctx); }
                }
                else
                {
                    if (m_grid.AddVariable()) { InvalidateRect(m_hwnd, nullptr, FALSE); m_gridView.BeginEditNameFromGrid(ctx); }
                }
                return 0;
            case kMenuRemove:
                if (m_grid.SelectedIsPathEntry())
                    Repaint(m_grid.RemoveEntry());
                else
                    Repaint(m_grid.RemoveVariable());
                return 0;
            }
        }
        if (HIWORD(wp) == EN_KILLFOCUS && m_gridView.IsEditControl(reinterpret_cast<HWND>(lp)))
        {
            m_gridView.OnEditEnd(ctx, true, false, false);
            return 0;
        }
        break;
    case WM_APP_EDIT_END:
    {
        m_gridView.OnEditEnd(ctx, wp != 0, wp == 2, lp != 0);
        return 0;
    }
    }
    return D2DWindow::HandleMessage(msg, wp, lp);
}
