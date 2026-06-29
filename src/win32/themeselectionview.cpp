#include "precomp.h"

// ThemeSelectionView — two-column (Dark | Light) theme picker with per-row
// 16-color swatches.  Selection live-previews; Apply commits, Cancel restores.

#include "themeselectionview.h"

namespace { constexpr float kColGap{16.0f}; }

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

ui::ThemeSelectionView::ThemeSelectionView(theme::ThemeSet& themes)
    : m_themes{themes}
{
}

// ---------------------------------------------------------------------------
// View overrides
// ---------------------------------------------------------------------------

void ui::ThemeSelectionView::BuildColumns()
{
    m_col[0].clear();
    m_col[1].clear();
    const auto themes{m_themes.Themes()};

    const auto fill = [&](int col, const wchar_t* title, bool dark) {
        bool headerAdded{false};
        for (const auto& t : themes)
        {
            if (t.dark != dark) continue;
            if (!headerAdded)
            {
                m_col[col].push_back(Entry{true, title, {}, {}});
                headerAdded = true;
            }
            m_col[col].push_back(
                Entry{false, std::wstring{t.name.begin(), t.name.end()}, t.name, t.base16});
        }
    };
    fill(0, L"Dark", true);
    fill(1, L"Light", false);
}

void ui::ThemeSelectionView::Activate(const ViewContext& /*ctx*/)
{
    BuildColumns();
    m_openingTheme = m_themes.Current().name;

    // Select the row for the currently active theme.
    m_selCol = m_selRow = -1;
    for (int c{0}; c < 2 && m_selCol < 0; ++c)
        for (int i{0}; i < static_cast<int>(m_col[c].size()); ++i)
        {
            const Entry& e{m_col[c][static_cast<size_t>(i)]};
            if (!e.header && e.themeName == m_openingTheme) { m_selCol = c; m_selRow = i; break; }
        }

    m_hovCol = m_hovRow = -1;
    m_btnHover = -1;
    m_scroll = 0.0f;
    m_needScrollToSelected = true; // defer to first paint (real bounds known)
    m_pendingAction = Action::None;
    m_themeChanged = false;
}

void ui::ThemeSelectionView::Deactivate()
{
    m_col[0].clear();
    m_col[1].clear();
}

void ui::ThemeSelectionView::Preview(const std::string& name)
{
    if (m_themes.SelectByName(name))
        m_themeChanged = true; // MainWindow repaints in the new theme (not persisted)
}

void ui::ThemeSelectionView::Paint(const ViewContext& ctx, const D2D1_RECT_F& bounds)
{
    m_lastBounds = bounds;
    const theme::ColorScheme& s{*ctx.scheme};
    const Geom g{ComputeLayout(bounds)};

    if (m_needScrollToSelected)
    {
        if (m_selRow > 0) EnsureVisible(g, m_selRow);
        m_needScrollToSelected = false;
    }
    ClampScroll(g);

    // Card
    ctx.brush->SetColor(s.card.fill);
    ctx.rt->FillRoundedRectangle(D2D1::RoundedRect(g.card, 10.0f, 10.0f), ctx.brush);
    ctx.brush->SetColor(s.card.border);
    ctx.rt->DrawRoundedRectangle(D2D1::RoundedRect(g.card, 10.0f, 10.0f), ctx.brush, 1.0f);

    // Title
    ui::DrawString(ctx, L"Themes", ctx.fmtName,
                   D2D1::RectF(g.card.left + 20.0f, g.card.top + 12.0f,
                               g.card.right - 20.0f, g.card.top + 44.0f),
                   s.headerText);

    // Columns
    for (int c{0}; c < 2; ++c)
    {
        ctx.rt->PushAxisAlignedClip(g.col[c], D2D1_ANTIALIAS_MODE_ALIASED);
        float y{g.col[c].top - m_scroll};
        for (int i{0}; i < static_cast<int>(m_col[c].size()); ++i)
        {
            const Entry& e{m_col[c][static_cast<size_t>(i)]};
            const D2D1_RECT_F r{D2D1::RectF(g.col[c].left, y, g.col[c].right, y + g.rowH)};
            if (e.header)
            {
                ui::DrawString(ctx, e.label, ctx.fmtSub,
                               D2D1::RectF(r.left + 4.0f, y, r.right - 4.0f, y + g.rowH),
                               s.headerSubtext);
            }
            else
            {
                const bool selected{c == m_selCol && i == m_selRow};
                const bool hovered{c == m_hovCol && i == m_hovRow && !selected};
                DrawRow(ctx, e, r, selected, hovered);
            }
            y += g.rowH;
        }
        ctx.rt->PopAxisAlignedClip();
    }

    // Buttons
    const auto drawBtn = [&](const D2D1_RECT_F& r, const wchar_t* label, bool primary, bool hover) {
        ctx.brush->SetColor(primary ? s.accent : (hover ? s.rowSelected.fill : s.rowHover.fill));
        ctx.rt->FillRoundedRectangle(D2D1::RoundedRect(r, 6.0f, 6.0f), ctx.brush);
        if (!primary)
        {
            ctx.brush->SetColor(s.card.border);
            ctx.rt->DrawRoundedRectangle(D2D1::RoundedRect(r, 6.0f, 6.0f), ctx.brush, 1.0f);
        }
        ctx.brush->SetColor(primary ? s.accentText : s.headerText);
        ctx.rt->DrawTextW(label, static_cast<UINT32>(wcslen(label)), ctx.fmtButton, r, ctx.brush,
                          D2D1_DRAW_TEXT_OPTIONS_CLIP);
    };
    drawBtn(g.okBtn, L"Apply", true, m_btnHover == 0);
    drawBtn(g.cancelBtn, L"Cancel", false, m_btnHover == 1);
}

void ui::ThemeSelectionView::DrawRow(const ViewContext& ctx, const Entry& e,
                                     const D2D1_RECT_F& r, bool selected, bool hovered) const
{
    const theme::ColorScheme& s{*ctx.scheme};
    if (selected)      { ctx.brush->SetColor(s.accent);        ctx.rt->FillRectangle(r, ctx.brush); }
    else if (hovered)  { ctx.brush->SetColor(s.rowHover.fill); ctx.rt->FillRectangle(r, ctx.brush); }

    // Name (top of the row)
    ui::DrawString(ctx, e.label, ctx.fmtValue,
                   D2D1::RectF(r.left + 8.0f, r.top + 4.0f, r.right - 8.0f, r.top + 26.0f),
                   selected ? s.accentText : (hovered ? s.rowHover.text : s.headerText));

    // 16-color swatch strip (bottom of the row)
    const float sx0{r.left + 8.0f};
    const float sx1{r.right - 8.0f};
    const float sy0{r.bottom - 16.0f};
    const float sy1{r.bottom - 6.0f};
    const float w{(sx1 - sx0) / 16.0f};
    for (int k{0}; k < 16; ++k)
    {
        const D2D1_RECT_F cell{D2D1::RectF(sx0 + k * w, sy0, sx0 + (k + 1) * w, sy1)};
        ctx.brush->SetColor(e.base16[static_cast<size_t>(k)]);
        ctx.rt->FillRectangle(cell, ctx.brush);
    }
}

bool ui::ThemeSelectionView::OnMouseMove(const ViewContext& /*ctx*/, float x, float y)
{
    const Geom g{ComputeLayout(m_lastBounds)};
    bool need{false};

    const int btnHover{Contains(g.okBtn, x, y) ? 0 : (Contains(g.cancelBtn, x, y) ? 1 : -1)};
    if (btnHover != m_btnHover) { m_btnHover = btnHover; need = true; }

    int col{-1}, row{-1};
    RowAtPoint(g, x, y, col, row);
    if (col != m_hovCol || row != m_hovRow) { m_hovCol = col; m_hovRow = row; need = true; }
    return need;
}

bool ui::ThemeSelectionView::OnMouseLeave()
{
    bool need{false};
    if (m_btnHover != -1) { m_btnHover = -1; need = true; }
    if (m_hovCol != -1 || m_hovRow != -1) { m_hovCol = m_hovRow = -1; need = true; }
    return need;
}

bool ui::ThemeSelectionView::OnLButtonDown(const ViewContext& /*ctx*/, float x, float y,
                                            bool /*shift*/, bool /*ctrl*/)
{
    const Geom g{ComputeLayout(m_lastBounds)};

    if (Contains(g.okBtn, x, y))     { m_pendingAction = Action::Apply; return true; }
    if (Contains(g.cancelBtn, x, y)) { Preview(m_openingTheme); m_pendingAction = Action::Cancel; return true; }
    if (!Contains(g.card, x, y))     { Preview(m_openingTheme); m_pendingAction = Action::Cancel; return true; }

    int col{-1}, row{-1};
    if (RowAtPoint(g, x, y, col, row) && (col != m_selCol || row != m_selRow))
    {
        m_selCol = col; m_selRow = row;
        Preview(m_col[col][static_cast<size_t>(row)].themeName);
        return true;
    }
    return false;
}

bool ui::ThemeSelectionView::OnWheel(const ViewContext& /*ctx*/, int delta)
{
    const Geom g{ComputeLayout(m_lastBounds)};
    m_scroll -= (static_cast<float>(delta) / WHEEL_DELTA) * 3.0f * g.rowH;
    ClampScroll(g);
    return true;
}

bool ui::ThemeSelectionView::OnKey(const ViewContext& /*ctx*/, int vk)
{
    if (vk == VK_ESCAPE) { Preview(m_openingTheme); m_pendingAction = Action::Cancel; return true; }
    if (vk == VK_RETURN) { m_pendingAction = Action::Apply; return true; }

    if (vk == VK_UP || vk == VK_DOWN)
    {
        const int col{m_selCol < 0 ? 0 : m_selCol};
        const int dir{vk == VK_UP ? -1 : 1};
        const int from{m_selRow < 0 ? (dir > 0 ? -1 : static_cast<int>(m_col[col].size())) : m_selRow};
        const int next{NextThemeRow(col, from, dir)};
        if (next >= 0 && (col != m_selCol || next != m_selRow))
        {
            m_selCol = col; m_selRow = next;
            Preview(m_col[col][static_cast<size_t>(next)].themeName);
            const Geom g{ComputeLayout(m_lastBounds)};
            EnsureVisible(g, m_selRow);
        }
        return true;
    }
    if (vk == VK_LEFT || vk == VK_RIGHT)
    {
        const int target{vk == VK_LEFT ? 0 : 1};
        if (target != m_selCol && !m_col[target].empty())
        {
            // Keep a similar row, snapping to the nearest theme (skip the header).
            const int want{m_selRow < 0 ? 0 : m_selRow};
            int row{NextThemeRow(target, want - 1, 1)};
            if (row < 0) row = NextThemeRow(target, static_cast<int>(m_col[target].size()), -1);
            if (row >= 0)
            {
                m_selCol = target; m_selRow = row;
                Preview(m_col[target][static_cast<size_t>(row)].themeName);
                const Geom g{ComputeLayout(m_lastBounds)};
                EnsureVisible(g, m_selRow);
            }
        }
        return true;
    }
    return false;
}

std::wstring ui::ThemeSelectionView::GetStatusText(const ViewContext& ctx) const
{
    const theme::ColorScheme& s{*ctx.scheme};
    std::wstring footer{std::format(
        L"{}   \x2022   \x2191\x2193 navigate   \x2022   \x2190\x2192 column   \x2022   Enter apply   \x2022   Esc cancel",
        std::wstring(s.name.begin(), s.name.end()))};
    if (ctx.zoom != 1.0f)
        footer += std::format(L"   \x2022   {}%", static_cast<int>(ctx.zoom * 100));
    return footer;
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

ui::ThemeSelectionView::Geom ui::ThemeSelectionView::ComputeLayout(
    const D2D1_RECT_F& bounds) const
{
    Geom g{};
    const float pad{20.0f};
    const float titleH{36.0f};
    const float btnRow{56.0f};

    g.card = bounds;

    const float listTop{bounds.top + pad + titleH};
    const float listBot{bounds.bottom - btnRow};
    const float listLeft{bounds.left + pad};
    const float listRight{bounds.right - pad};
    const float colW{(listRight - listLeft - kColGap) / 2.0f};
    g.col[0] = D2D1::RectF(listLeft, listTop, listLeft + colW, listBot);
    g.col[1] = D2D1::RectF(listLeft + colW + kColGap, listTop, listRight, listBot);

    constexpr float bw{96.0f};
    constexpr float bh{34.0f};
    const float by{g.card.bottom - pad - bh};
    g.cancelBtn = D2D1::RectF(g.card.right - pad - bw, by, g.card.right - pad, by + bh);
    g.okBtn = D2D1::RectF(g.cancelBtn.left - 12.0f - bw, by, g.cancelBtn.left - 12.0f, by + bh);
    return g;
}

bool ui::ThemeSelectionView::RowAtPoint(const Geom& g, float x, float y, int& col, int& row) const
{
    col = row = -1;
    for (int c{0}; c < 2; ++c)
    {
        if (x < g.col[c].left || x >= g.col[c].right || y < g.col[c].top || y >= g.col[c].bottom)
            continue;
        const int i{static_cast<int>((y - g.col[c].top + m_scroll) / g.rowH)};
        if (i < 0 || i >= static_cast<int>(m_col[c].size())) return false;
        if (m_col[c][static_cast<size_t>(i)].header) return false; // headers aren't selectable
        col = c; row = i;
        return true;
    }
    return false;
}

int ui::ThemeSelectionView::NextThemeRow(int col, int from, int dir) const
{
    const int n{static_cast<int>(m_col[col].size())};
    for (int i{from + dir}; i >= 0 && i < n; i += dir)
        if (!m_col[col][static_cast<size_t>(i)].header) return i;
    return -1;
}

int ui::ThemeSelectionView::MaxRows() const
{
    return static_cast<int>(std::max(m_col[0].size(), m_col[1].size()));
}

void ui::ThemeSelectionView::ClampScroll(const Geom& g)
{
    const float viewH{g.col[0].bottom - g.col[0].top};
    const float contentH{static_cast<float>(MaxRows()) * g.rowH};
    m_scroll = std::clamp(m_scroll, 0.0f, std::max(0.0f, contentH - viewH));
}

void ui::ThemeSelectionView::EnsureVisible(const Geom& g, int row)
{
    if (row < 0) return;
    const float top{static_cast<float>(row) * g.rowH};
    const float bottom{top + g.rowH};
    const float viewH{g.col[0].bottom - g.col[0].top};
    if (top < m_scroll) m_scroll = top;
    else if (bottom > m_scroll + viewH) m_scroll = bottom - viewH;
    ClampScroll(g);
}
