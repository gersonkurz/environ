#include "precomp.h"

// ThemeSelectionView — theme picker extracted as a standalone view.

#include "themeselectionview.h"

namespace
{
    bool Contains(const D2D1_RECT_F& r, float x, float y)
    {
        return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
    }
}

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

void ui::ThemeSelectionView::Activate(const ViewContext& /*ctx*/)
{
    m_names = m_themes.Names();

    // Find the index of the currently active theme.
    const auto& cur{m_themes.Current().name};
    m_selected = -1;
    for (int i{0}; i < static_cast<int>(m_names.size()); ++i)
    {
        if (m_names[static_cast<size_t>(i)] == cur)
        {
            m_selected = i;
            break;
        }
    }

    m_rowHover = -1;
    m_btnHover = -1;
    m_scroll = 0.0f;
    m_pendingAction = Action::None;
    m_themeChanged = false;

    // Scroll so the current theme is visible on open.
    if (m_selected >= 0)
    {
        const Geom g{ComputeLayout(m_lastBounds)};
        EnsureVisible(g, m_selected);
    }
}

void ui::ThemeSelectionView::Deactivate()
{
    m_names.clear();
}

void ui::ThemeSelectionView::Paint(const ViewContext& ctx, const D2D1_RECT_F& bounds)
{
    m_lastBounds = bounds;
    const theme::ColorScheme& s{*ctx.scheme};
    const Geom g{ComputeLayout(bounds)};

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

    // Theme list
    ctx.rt->PushAxisAlignedClip(g.list, D2D1_ANTIALIAS_MODE_ALIASED);

    if (m_names.empty())
    {
        ui::DrawString(ctx, L"No themes found.", ctx.fmtValue,
                       D2D1::RectF(g.list.left + 8.0f, g.list.top,
                                   g.list.right, g.list.top + g.rowH),
                       s.headerSubtext);
    }
    else
    {
        float y{g.list.top - m_scroll};
        for (int i{0}; i < static_cast<int>(m_names.size()); ++i)
        {
            const bool selected{i == m_selected};
            const bool hovered{i == m_rowHover && !selected};

            const D2D1_RECT_F rowRect{D2D1::RectF(g.list.left, y, g.list.right, y + g.rowH)};

            // Row background
            if (selected)
            {
                ctx.brush->SetColor(s.accent);
                ctx.rt->FillRectangle(rowRect, ctx.brush);
            }
            else if (hovered)
            {
                ctx.brush->SetColor(s.rowHover.fill);
                ctx.rt->FillRectangle(rowRect, ctx.brush);
            }

            // Theme name
            const auto& name{m_names[static_cast<size_t>(i)]};
            std::wstring wname{name.begin(), name.end()};
            ui::DrawString(ctx, wname, ctx.fmtValue,
                           D2D1::RectF(g.list.left + 8.0f, y,
                                       g.list.right - 8.0f, y + g.rowH),
                           selected ? s.accentText
                                    : (hovered ? s.rowHover.text : s.headerText));

            y += g.rowH;
        }
    }

    ctx.rt->PopAxisAlignedClip();

    // Close button
    DrawButton(ctx, g.closeBtn, L"Close", false, m_btnHover == 0);
}

bool ui::ThemeSelectionView::OnMouseMove(const ViewContext& /*ctx*/, float x, float y)
{
    const Geom g{ComputeLayout(m_lastBounds)};
    bool need{false};
    const int btnHover{Contains(g.closeBtn, x, y) ? 0 : -1};
    if (btnHover != m_btnHover) { m_btnHover = btnHover; need = true; }
    const int rowHover{RowAtPoint(g, x, y)};
    if (rowHover != m_rowHover) { m_rowHover = rowHover; need = true; }
    return need;
}

bool ui::ThemeSelectionView::OnMouseLeave()
{
    bool need{false};
    if (m_btnHover != -1) { m_btnHover = -1; need = true; }
    if (m_rowHover != -1) { m_rowHover = -1; need = true; }
    return need;
}

bool ui::ThemeSelectionView::OnLButtonDown(const ViewContext& /*ctx*/, float x, float y,
                                            bool /*shift*/, bool /*ctrl*/)
{
    const Geom g{ComputeLayout(m_lastBounds)};

    if (Contains(g.closeBtn, x, y))
    {
        m_pendingAction = Action::Close;
        return true;
    }
    if (!Contains(g.card, x, y))
    {
        // Click outside card = close
        m_pendingAction = Action::Close;
        return true;
    }

    const int row{RowAtPoint(g, x, y)};
    if (row >= 0 && row != m_selected)
    {
        m_selected = row;
        m_themes.SelectByName(m_names[static_cast<size_t>(row)]);
        m_themeChanged = true;
        return true;
    }
    return false;
}

bool ui::ThemeSelectionView::OnWheel(const ViewContext& /*ctx*/, int delta)
{
    const Geom g{ComputeLayout(m_lastBounds)};
    const float viewH{g.list.bottom - g.list.top};
    const float contentH{static_cast<float>(m_names.size()) * g.rowH};
    const float maxScroll{std::max(0.0f, contentH - viewH)};
    m_scroll -= (static_cast<float>(delta) / WHEEL_DELTA) * 3.0f * g.rowH;
    m_scroll = std::clamp(m_scroll, 0.0f, maxScroll);
    return true;
}

bool ui::ThemeSelectionView::OnKey(const ViewContext& /*ctx*/, int vk)
{
    if (vk == VK_ESCAPE || vk == VK_RETURN)
    {
        m_pendingAction = Action::Close;
        return true;
    }
    if (vk == VK_UP || vk == VK_DOWN)
    {
        const int n{static_cast<int>(m_names.size())};
        if (n > 0)
        {
            int newSel;
            if (vk == VK_UP)
                newSel = (m_selected <= 0) ? 0 : m_selected - 1;
            else
                newSel = (m_selected < 0) ? 0 : std::min(m_selected + 1, n - 1);
            if (newSel != m_selected)
            {
                m_selected = newSel;
                m_themes.SelectByName(m_names[static_cast<size_t>(newSel)]);
                m_themeChanged = true;
            }
            const Geom g{ComputeLayout(m_lastBounds)};
            EnsureVisible(g, m_selected);
        }
        return true;
    }
    return false;
}

std::wstring ui::ThemeSelectionView::GetStatusText(const ViewContext& ctx) const
{
    const theme::ColorScheme& s{*ctx.scheme};
    std::wstring footer{std::format(
        L"{}   \x2022   \x2191\x2193 navigate   \x2022   Enter close   \x2022   Esc close",
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
    const float boundsW{bounds.right - bounds.left};
    const float boundsH{bounds.bottom - bounds.top};
    const float cw{boundsW};
    const float ch{boundsH};
    const float pad{20.0f};
    const float titleH{36.0f};
    const float btnRow{56.0f};

    g.card = D2D1::RectF(bounds.left, bounds.top, bounds.left + cw, bounds.top + ch);

    // List fills everything between title and button row.
    const float listTop{bounds.top + pad + titleH};
    const float listBot{bounds.top + ch - btnRow};
    g.list = D2D1::RectF(bounds.left + pad, listTop, bounds.left + cw - pad, listBot);

    constexpr float bw{96.0f};
    constexpr float bh{34.0f};
    const float by{g.card.bottom - pad - bh};
    g.closeBtn = D2D1::RectF(g.card.right - pad - bw, by, g.card.right - pad, by + bh);
    return g;
}

int ui::ThemeSelectionView::RowAtPoint(const Geom& g, float x, float y) const
{
    if (x < g.list.left || x >= g.list.right || y < g.list.top || y >= g.list.bottom)
        return -1;
    const float ry{y - g.list.top + m_scroll};
    const int row{static_cast<int>(ry / g.rowH)};
    if (row < 0 || row >= static_cast<int>(m_names.size())) return -1;
    return row;
}

void ui::ThemeSelectionView::EnsureVisible(const Geom& g, int idx)
{
    if (idx < 0 || idx >= static_cast<int>(m_names.size())) return;
    const float top{static_cast<float>(idx) * g.rowH};
    const float bottom{top + g.rowH};
    const float viewH{g.list.bottom - g.list.top};
    if (top < m_scroll) m_scroll = top;
    else if (bottom > m_scroll + viewH) m_scroll = bottom - viewH;
    const float maxScroll{std::max(0.0f,
        static_cast<float>(m_names.size()) * g.rowH - viewH)};
    m_scroll = std::clamp(m_scroll, 0.0f, maxScroll);
}

// ---------------------------------------------------------------------------
// Drawing helpers
// ---------------------------------------------------------------------------

void ui::ThemeSelectionView::DrawButton(const ViewContext& ctx, const D2D1_RECT_F& r,
                                         const wchar_t* label, bool primary,
                                         bool hover) const
{
    const theme::ColorScheme& s{*ctx.scheme};
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
}
