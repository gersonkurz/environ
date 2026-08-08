#include "precomp.h"

// FontSelectionView — UI font family picker.  The host rebuilds every text format on each
// selection, so the picker paints itself in the candidate font: the live preview costs no
// per-row text formats.

#include "fontselectionview.h"

#include <algorithm>

namespace
{
    constexpr float kPad{20.0f};

    // The family name in the user's locale if the font carries one, else the first entry.
    std::wstring PreferredFamilyName(IDWriteLocalizedStrings* names)
    {
        UINT32 index{0};
        BOOL exists{FALSE};
        wchar_t locale[LOCALE_NAME_MAX_LENGTH]{};
        if (GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH) != 0)
            names->FindLocaleName(locale, &index, &exists);
        if (!exists)
            names->FindLocaleName(L"en-us", &index, &exists);
        if (!exists)
            index = 0;

        UINT32 len{0};
        if (FAILED(names->GetStringLength(index, &len)) || len == 0) return {};
        std::wstring out(static_cast<size_t>(len) + 1, L'\0');
        if (FAILED(names->GetString(index, out.data(), len + 1))) return {};
        out.resize(len);
        return out;
    }
}

// ---------------------------------------------------------------------------
// View overrides
// ---------------------------------------------------------------------------

void ui::FontSelectionView::BuildFamilies(const ViewContext& ctx)
{
    m_families.clear();
    if (!ctx.dwrite) return;

    IDWriteFontCollection* collection{nullptr};
    if (FAILED(ctx.dwrite->GetSystemFontCollection(&collection, FALSE)) || !collection)
    {
        spdlog::error("GetSystemFontCollection failed; font list will be empty");
        return;
    }

    const UINT32 count{collection->GetFontFamilyCount()};
    m_families.reserve(count);
    for (UINT32 i{0}; i < count; ++i)
    {
        IDWriteFontFamily* family{nullptr};
        if (FAILED(collection->GetFontFamily(i, &family)) || !family) continue;

        IDWriteLocalizedStrings* names{nullptr};
        if (SUCCEEDED(family->GetFamilyNames(&names)) && names)
        {
            if (std::wstring name{PreferredFamilyName(names)}; !name.empty())
                m_families.push_back(std::move(name));
            names->Release();
        }
        family->Release();
    }
    collection->Release();

    std::ranges::sort(m_families, [](const std::wstring& a, const std::wstring& b) {
        return _wcsicmp(a.c_str(), b.c_str()) < 0;
    });
    spdlog::info("Font picker listed {} families", m_families.size());
}

void ui::FontSelectionView::Activate(const ViewContext& ctx)
{
    BuildFamilies(ctx);
    m_openingFamily = ctx.uiFontFamily ? ctx.uiFontFamily : L"";
    m_selected = m_openingFamily;

    // Select the row holding the active family (absent if it isn't installed — the host
    // falls back to a DirectWrite substitute, so leave the list unselected in that case).
    m_sel = -1;
    for (int i{0}; i < static_cast<int>(m_families.size()); ++i)
    {
        if (_wcsicmp(m_families[static_cast<size_t>(i)].c_str(), m_openingFamily.c_str()) == 0)
        {
            m_sel = i;
            break;
        }
    }

    m_hov = -1;
    m_btnHover = -1;
    m_scroll = 0.0f;
    m_needScrollToSelected = true; // defer to first paint (real bounds known)
    m_pendingAction = Action::None;
    m_fontChanged = false;
}

void ui::FontSelectionView::Deactivate()
{
    m_families.clear();
    m_families.shrink_to_fit();
}

void ui::FontSelectionView::Preview(int row)
{
    if (row < 0 || row >= static_cast<int>(m_families.size())) return;
    m_sel = row;
    m_selected = m_families[static_cast<size_t>(row)];
    m_fontChanged = true; // MainWindow rebuilds its text formats (not persisted until Apply)
}

void ui::FontSelectionView::Paint(const ViewContext& ctx, const D2D1_RECT_F& bounds)
{
    m_lastBounds = bounds;
    const theme::ColorScheme& s{*ctx.scheme};
    const Geom g{ComputeLayout(bounds)};

    if (m_needScrollToSelected)
    {
        EnsureVisible(g, m_sel);
        m_needScrollToSelected = false;
    }
    ClampScroll(g);

    // Card
    ctx.brush->SetColor(s.card.fill);
    ctx.rt->FillRoundedRectangle(D2D1::RoundedRect(g.card, 10.0f, 10.0f), ctx.brush);
    ctx.brush->SetColor(s.card.border);
    ctx.rt->DrawRoundedRectangle(D2D1::RoundedRect(g.card, 10.0f, 10.0f), ctx.brush, 1.0f);

    // Title
    ui::DrawString(ctx, L"Font", ctx.fmtName,
                   D2D1::RectF(g.card.left + kPad, g.card.top + 12.0f,
                               g.card.right - kPad, g.card.top + 44.0f),
                   s.headerText);

    // List
    ctx.rt->PushAxisAlignedClip(g.list, D2D1_ANTIALIAS_MODE_ALIASED);
    float y{g.list.top - m_scroll};
    for (int i{0}; i < static_cast<int>(m_families.size()); ++i, y += g.rowH)
    {
        if (y + g.rowH < g.list.top || y > g.list.bottom) continue; // off-screen
        const D2D1_RECT_F r{D2D1::RectF(g.list.left, y, g.list.right, y + g.rowH)};
        const bool selected{i == m_sel};
        const bool hovered{i == m_hov && !selected};
        if (selected)     { ctx.brush->SetColor(s.accent);        ctx.rt->FillRectangle(r, ctx.brush); }
        else if (hovered) { ctx.brush->SetColor(s.rowHover.fill); ctx.rt->FillRectangle(r, ctx.brush); }
        ui::DrawString(ctx, m_families[static_cast<size_t>(i)], ctx.fmtValue,
                       D2D1::RectF(r.left + 8.0f, r.top, r.right - 8.0f, r.bottom),
                       selected ? s.accentText : s.headerText);
    }
    ctx.rt->PopAxisAlignedClip();

    // Sample: drawn with the host's formats, which already carry the previewed family.
    // A pangram for the letterforms, a path for the digits and punctuation that matter here.
    ctx.brush->SetColor(s.card.border);
    ctx.rt->DrawLine(D2D1::Point2F(g.sample.left, g.sample.top),
                     D2D1::Point2F(g.sample.right, g.sample.top), ctx.brush, 1.0f);
    const float half{(g.sample.bottom - g.sample.top) / 2.0f};
    ui::DrawString(ctx, L"The quick brown fox jumps over the lazy dog", ctx.fmtName,
                   D2D1::RectF(g.sample.left, g.sample.top, g.sample.right, g.sample.top + half),
                   s.headerText);
    ui::DrawString(ctx, L"PATH   C:\\Program Files\\dotnet;%JAVA_HOME%\\bin;%USERPROFILE%\\go\\bin",
                   ctx.fmtValue,
                   D2D1::RectF(g.sample.left, g.sample.top + half, g.sample.right, g.sample.bottom),
                   s.headerSubtext);

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

bool ui::FontSelectionView::OnMouseMove(const ViewContext& /*ctx*/, float x, float y)
{
    const Geom g{ComputeLayout(m_lastBounds)};
    bool need{false};

    const int btnHover{Contains(g.okBtn, x, y) ? 0 : (Contains(g.cancelBtn, x, y) ? 1 : -1)};
    if (btnHover != m_btnHover) { m_btnHover = btnHover; need = true; }

    const int row{RowAtPoint(g, x, y)};
    if (row != m_hov) { m_hov = row; need = true; }
    return need;
}

bool ui::FontSelectionView::OnMouseLeave()
{
    bool need{false};
    if (m_btnHover != -1) { m_btnHover = -1; need = true; }
    if (m_hov != -1)      { m_hov = -1;      need = true; }
    return need;
}

bool ui::FontSelectionView::OnLButtonDown(const ViewContext& /*ctx*/, float x, float y,
                                          bool /*shift*/, bool /*ctrl*/)
{
    const Geom g{ComputeLayout(m_lastBounds)};

    if (Contains(g.okBtn, x, y)) { m_pendingAction = Action::Apply; return true; }
    if (Contains(g.cancelBtn, x, y) || !Contains(g.card, x, y))
    {
        m_selected = m_openingFamily;
        m_fontChanged = true; // restore before closing
        m_pendingAction = Action::Cancel;
        return true;
    }

    if (const int row{RowAtPoint(g, x, y)}; row >= 0 && row != m_sel)
    {
        Preview(row);
        return true;
    }
    return false;
}

bool ui::FontSelectionView::OnWheel(const ViewContext& /*ctx*/, int delta)
{
    const Geom g{ComputeLayout(m_lastBounds)};
    m_scroll -= (static_cast<float>(delta) / WHEEL_DELTA) * 3.0f * g.rowH;
    ClampScroll(g);
    return true;
}

bool ui::FontSelectionView::OnKey(const ViewContext& /*ctx*/, int vk)
{
    if (vk == VK_ESCAPE)
    {
        m_selected = m_openingFamily;
        m_fontChanged = true;
        m_pendingAction = Action::Cancel;
        return true;
    }
    if (vk == VK_RETURN) { m_pendingAction = Action::Apply; return true; }
    if (m_families.empty()) return false;

    const int last{static_cast<int>(m_families.size()) - 1};
    const Geom g{ComputeLayout(m_lastBounds)};
    const int page{std::max(1, static_cast<int>((g.list.bottom - g.list.top) / g.rowH) - 1)};

    int target{-1};
    switch (vk)
    {
    case VK_UP:    target = (m_sel < 0) ? last : std::max(0, m_sel - 1);       break;
    case VK_DOWN:  target = (m_sel < 0) ? 0    : std::min(last, m_sel + 1);    break;
    case VK_PRIOR: target = (m_sel < 0) ? 0    : std::max(0, m_sel - page);    break;
    case VK_NEXT:  target = (m_sel < 0) ? 0    : std::min(last, m_sel + page); break;
    case VK_HOME:  target = 0;    break;
    case VK_END:   target = last; break;
    default:
        // Type-to-jump: with several hundred families, arrow keys alone are unusable.
        // Repeating a letter cycles through the families starting with it.
        if (vk >= 'A' && vk <= 'Z')
        {
            const auto initial{static_cast<wchar_t>(vk)};
            target = FirstMatching(initial, m_sel + 1);
            if (target < 0) target = FirstMatching(initial, 0); // wrap
        }
        break;
    }
    if (target < 0) return false;

    if (target != m_sel) Preview(target);
    EnsureVisible(g, m_sel);
    return true;
}

std::wstring ui::FontSelectionView::GetStatusText(const ViewContext& ctx) const
{
    std::wstring footer{std::format(
        L"{} font{}   \x2022   \x2191\x2193 navigate   \x2022   A\x2013Z jump   \x2022   Enter apply   \x2022   Esc cancel",
        m_families.size(), m_families.size() == 1 ? L"" : L"s")};
    if (ctx.zoom != 1.0f)
        footer += std::format(L"   \x2022   {}%", static_cast<int>(ctx.zoom * 100));
    return footer;
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

ui::FontSelectionView::Geom ui::FontSelectionView::ComputeLayout(const D2D1_RECT_F& bounds) const
{
    Geom g{};
    constexpr float titleH{36.0f};
    constexpr float btnRow{56.0f};
    constexpr float sampleH{72.0f};

    g.card = bounds;
    const float listTop{bounds.top + kPad + titleH};
    const float sampleTop{bounds.bottom - btnRow - sampleH};
    g.list = D2D1::RectF(bounds.left + kPad, listTop, bounds.right - kPad, sampleTop - 8.0f);
    g.sample = D2D1::RectF(bounds.left + kPad, sampleTop, bounds.right - kPad,
                           bounds.bottom - btnRow);

    constexpr float bw{96.0f};
    constexpr float bh{34.0f};
    const float by{g.card.bottom - kPad - bh};
    g.cancelBtn = D2D1::RectF(g.card.right - kPad - bw, by, g.card.right - kPad, by + bh);
    g.okBtn = D2D1::RectF(g.cancelBtn.left - 12.0f - bw, by, g.cancelBtn.left - 12.0f, by + bh);
    return g;
}

int ui::FontSelectionView::RowAtPoint(const Geom& g, float x, float y) const
{
    if (!Contains(g.list, x, y)) return -1;
    const int i{static_cast<int>((y - g.list.top + m_scroll) / g.rowH)};
    return (i >= 0 && i < static_cast<int>(m_families.size())) ? i : -1;
}

int ui::FontSelectionView::FirstMatching(wchar_t initial, int from) const
{
    const int n{static_cast<int>(m_families.size())};
    for (int i{std::max(0, from)}; i < n; ++i)
    {
        const std::wstring& f{m_families[static_cast<size_t>(i)]};
        if (!f.empty() && towupper(f.front()) == towupper(initial)) return i;
    }
    return -1;
}

void ui::FontSelectionView::ClampScroll(const Geom& g)
{
    const float viewH{g.list.bottom - g.list.top};
    const float contentH{static_cast<float>(m_families.size()) * g.rowH};
    m_scroll = std::clamp(m_scroll, 0.0f, std::max(0.0f, contentH - viewH));
}

void ui::FontSelectionView::EnsureVisible(const Geom& g, int row)
{
    if (row < 0) return;
    const float top{static_cast<float>(row) * g.rowH};
    const float bottom{top + g.rowH};
    const float viewH{g.list.bottom - g.list.top};
    if (top < m_scroll) m_scroll = top;
    else if (bottom > m_scroll + viewH) m_scroll = bottom - viewH;
    ClampScroll(g);
}
