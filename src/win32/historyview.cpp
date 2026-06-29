#include "precomp.h"

// HistoryView — snapshot history browser extracted from MainWindow.

#include "historyview.h"
#include "EnvWriter.h"

namespace
{
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
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

ui::HistoryView::HistoryView(Environ::core::SnapshotStore& snapshots)
    : m_snapshots{snapshots}
{
}

// ---------------------------------------------------------------------------
// View overrides
// ---------------------------------------------------------------------------

void ui::HistoryView::Activate(const ViewContext& /*ctx*/)
{
    using namespace Environ::core;

    m_snapshotList = m_snapshots.list_snapshots();

    // Read current registry once, reused for all "vs current" comparisons.
    m_curUser = read_variables(Scope::User);
    m_curMachine = read_variables(Scope::Machine);
    expand_and_validate(m_curUser);
    expand_and_validate(m_curMachine);

    m_recordedTable.clear();
    m_currentTable.clear();
    m_selected = -1;
    m_rowHover = -1;
    m_btnHover = -1;
    m_scroll = 0.0f;
    m_pendingAction = Action::None;
    m_restoreData = {};
}

void ui::HistoryView::Deactivate()
{
    m_snapshotList.clear();
    m_recordedTable.clear();
    m_currentTable.clear();
    m_curUser.clear();
    m_curMachine.clear();
    m_restoreData = {};
}

void ui::HistoryView::Paint(const ViewContext& ctx, const D2D1_RECT_F& bounds)
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
    ui::DrawString(ctx, L"History", ctx.fmtName,
                   D2D1::RectF(g.card.left + 20.0f, g.card.top + 12.0f,
                               g.card.right - 20.0f, g.card.top + 44.0f),
                   s.headerText);

    // Snapshot list
    ctx.rt->PushAxisAlignedClip(g.list, D2D1_ANTIALIAS_MODE_ALIASED);

    if (m_snapshotList.empty())
    {
        ui::DrawString(ctx, L"No snapshots yet. Snapshots are created when you apply changes.",
                       ctx.fmtValue,
                       D2D1::RectF(g.list.left + 8.0f, g.list.top, g.list.right, g.list.top + 28.0f),
                       s.headerSubtext);
    }
    else
    {
        float y{g.list.top - m_scroll};
        for (int i{0}; i < static_cast<int>(m_snapshotList.size()); ++i)
        {
            const auto& snap{m_snapshotList[static_cast<size_t>(i)]};
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

            // Timestamp + label
            auto ts{FormatTimestamp(snap.timestamp)};
            auto label{pnq::unicode::to_utf16(snap.label)};
            auto text{ts + L"  \x2014  " + label};
            ui::DrawString(ctx, text, ctx.fmtValue,
                           D2D1::RectF(g.list.left + 8.0f, y, g.list.right - 8.0f, y + g.rowH),
                           selected ? s.accentText : (hovered ? s.rowHover.text : s.headerText));

            y += g.rowH;

            // Detail: monospace diff tables for selected snapshot.
            if (selected)
            {
                const auto paintTable = [&](const wchar_t* header,
                                            const std::vector<std::wstring>& table) {
                    if (table.empty()) return;
                    ui::DrawString(ctx, header, ctx.fmtHeader,
                                   D2D1::RectF(g.list.left + 12.0f, y, g.list.right - 8.0f, y + g.detailH),
                                   s.headerSubtext);
                    y += g.detailH;
                    for (const auto& line : table)
                    {
                        ui::DrawString(ctx, line, ctx.fmtMono,
                                       D2D1::RectF(g.list.left + 4.0f, y, g.list.right, y + g.detailH),
                                       s.headerSubtext);
                        y += g.detailH;
                    }
                };
                paintTable(L"Recorded changes:", m_recordedTable);
                paintTable(L"Difference from current:", m_currentTable);
            }
        }
    }

    ctx.rt->PopAxisAlignedClip();

    // Buttons
    const bool hasSelection{m_selected >= 0
                            && m_selected < static_cast<int>(m_snapshotList.size())};
    DrawButton(ctx, g.deleteBtn, L"Delete", false, m_btnHover == 2 && hasSelection);
    DrawButton(ctx, g.closeBtn, L"Close", false, m_btnHover == 0);
    DrawButton(ctx, g.restoreBtn, L"Restore", hasSelection, m_btnHover == 1 && hasSelection);

    // If no selection, dim the Restore + Delete buttons
    if (!hasSelection)
    {
        ctx.brush->SetColor(D2D1::ColorF(s.card.fill.r, s.card.fill.g, s.card.fill.b, 0.5f));
        ctx.rt->FillRoundedRectangle(D2D1::RoundedRect(g.restoreBtn, 6.0f, 6.0f), ctx.brush);
        ctx.rt->FillRoundedRectangle(D2D1::RoundedRect(g.deleteBtn, 6.0f, 6.0f), ctx.brush);
    }
}

bool ui::HistoryView::OnMouseMove(const ViewContext& /*ctx*/, float x, float y)
{
    const Geom g{ComputeLayout(m_lastBounds)};
    bool need{false};
    const int btnHover{Contains(g.closeBtn, x, y) ? 0
        : (Contains(g.restoreBtn, x, y) ? 1
        : (Contains(g.deleteBtn, x, y) ? 2 : -1))};
    if (btnHover != m_btnHover) { m_btnHover = btnHover; need = true; }
    const int rowHover{RowAtPoint(g, x, y)};
    if (rowHover != m_rowHover) { m_rowHover = rowHover; need = true; }
    return need;
}

bool ui::HistoryView::OnMouseLeave()
{
    bool need{false};
    if (m_btnHover != -1) { m_btnHover = -1; need = true; }
    if (m_rowHover != -1) { m_rowHover = -1; need = true; }
    return need;
}

bool ui::HistoryView::OnLButtonDown(const ViewContext& /*ctx*/, float x, float y,
                                     bool /*shift*/, bool /*ctrl*/)
{
    const Geom g{ComputeLayout(m_lastBounds)};
    if (Contains(g.closeBtn, x, y))
    {
        m_pendingAction = Action::Close;
        return true;
    }
    if (Contains(g.restoreBtn, x, y) && m_selected >= 0)
    {
        PrepareRestore();
        m_pendingAction = Action::Restore;
        return true;
    }
    if (Contains(g.deleteBtn, x, y) && m_selected >= 0)
    {
        DeleteSelected();
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
        Select(row);
        return true;
    }
    if (row >= 0 && row == m_selected)
    {
        // Click on already-selected: deselect
        Select(-1);
        return true;
    }
    return false;
}

bool ui::HistoryView::OnWheel(const ViewContext& /*ctx*/, int delta)
{
    const Geom g{ComputeLayout(m_lastBounds)};
    const float viewH{g.list.bottom - g.list.top};
    const float maxScroll{std::max(0.0f, ListContentH(g) - viewH)};
    m_scroll -= (static_cast<float>(delta) / WHEEL_DELTA) * 3.0f * g.rowH;
    m_scroll = std::clamp(m_scroll, 0.0f, maxScroll);
    return true;
}

bool ui::HistoryView::OnKey(const ViewContext& /*ctx*/, int vk)
{
    if (vk == VK_ESCAPE)
    {
        m_pendingAction = Action::Close;
        return true;
    }
    if (vk == VK_RETURN && m_selected >= 0)
    {
        PrepareRestore();
        m_pendingAction = Action::Restore;
        return true;
    }
    if (vk == VK_DELETE)
    {
        DeleteSelected();
        return true;
    }
    if (vk == VK_UP || vk == VK_DOWN)
    {
        const int n{static_cast<int>(m_snapshotList.size())};
        if (n > 0)
        {
            int newSel;
            if (vk == VK_UP)
                newSel = (m_selected <= 0) ? 0 : m_selected - 1;
            else
                newSel = (m_selected < 0) ? 0 : std::min(m_selected + 1, n - 1);
            Select(newSel);
            const Geom g{ComputeLayout(m_lastBounds)};
            EnsureVisible(g, m_selected);
        }
        return true;
    }
    return false;
}

std::wstring ui::HistoryView::GetStatusText(const ViewContext& ctx) const
{
    const theme::ColorScheme& s{*ctx.scheme};
    std::wstring footer{std::format(
        L"{}   \x2022   \x2191\x2193 navigate   \x2022   Enter restore   \x2022   Del delete   \x2022   Esc close",
        std::wstring(s.name.begin(), s.name.end()))};
    if (ctx.zoom != 1.0f)
        footer += std::format(L"   \x2022   {}%", static_cast<int>(ctx.zoom * 100));
    return footer;
}

// ---------------------------------------------------------------------------
// Pending-action API
// ---------------------------------------------------------------------------

ui::HistoryView::RestoreData ui::HistoryView::TakeRestoreData()
{
    RestoreData data{std::move(m_restoreData)};
    m_restoreData = {};
    return data;
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

ui::HistoryView::Geom ui::HistoryView::ComputeLayout(const D2D1_RECT_F& bounds) const
{
    Geom g{};
    const float boundsW{bounds.right - bounds.left};
    const float boundsH{bounds.bottom - bounds.top};
    // Fill the full view area.  The bounds are already inset by ViewBounds
    // (sides, caption, footer), so the card spans the entire region.
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
    g.restoreBtn = D2D1::RectF(g.card.right - pad - bw, by, g.card.right - pad, by + bh);
    g.closeBtn = D2D1::RectF(g.restoreBtn.left - 12.0f - bw, by, g.restoreBtn.left - 12.0f, by + bh);
    g.deleteBtn = D2D1::RectF(g.card.left + pad, by, g.card.left + pad + bw, by + bh);
    return g;
}

float ui::HistoryView::ListContentH(const Geom& g) const
{
    float h{0.0f};
    for (int i{0}; i < static_cast<int>(m_snapshotList.size()); ++i)
    {
        h += g.rowH;
        if (i == m_selected)
            h += static_cast<float>(DetailLineCount(i)) * g.detailH;
    }
    return h;
}

int ui::HistoryView::DetailLineCount(int idx) const
{
    if (idx != m_selected || idx < 0) return 0;
    int count{0};
    if (!m_recordedTable.empty())
        count += 1 + static_cast<int>(m_recordedTable.size());
    if (!m_currentTable.empty())
        count += 1 + static_cast<int>(m_currentTable.size());
    return count;
}

int ui::HistoryView::RowAtPoint(const Geom& g, float x, float y) const
{
    if (x < g.list.left || x >= g.list.right || y < g.list.top || y >= g.list.bottom)
        return -1;
    float ry{g.list.top - m_scroll};
    for (int i{0}; i < static_cast<int>(m_snapshotList.size()); ++i)
    {
        float rowBottom{ry + g.rowH};
        if (i == m_selected)
            rowBottom += static_cast<float>(DetailLineCount(i)) * g.detailH;
        if (y >= ry && y < rowBottom) return i;
        ry = rowBottom;
    }
    return -1;
}

void ui::HistoryView::EnsureVisible(const Geom& g, int idx)
{
    if (idx < 0 || idx >= static_cast<int>(m_snapshotList.size())) return;
    float top{0.0f};
    for (int i{0}; i < idx; ++i)
    {
        top += g.rowH;
        if (i == m_selected)
            top += static_cast<float>(DetailLineCount(i)) * g.detailH;
    }
    float bottom{top + g.rowH};
    if (idx == m_selected)
        bottom += static_cast<float>(DetailLineCount(idx)) * g.detailH;
    const float viewH{g.list.bottom - g.list.top};
    if (top < m_scroll) m_scroll = top;
    else if (bottom > m_scroll + viewH) m_scroll = bottom - viewH;
    const float maxScroll{std::max(0.0f, ListContentH(g) - viewH)};
    m_scroll = std::clamp(m_scroll, 0.0f, maxScroll);
}

// ---------------------------------------------------------------------------
// Selection / data
// ---------------------------------------------------------------------------

void ui::HistoryView::Select(int idx)
{
    m_selected = idx;
    ComputeDiffTables();
}

void ui::HistoryView::ComputeDiffTables()
{
    using namespace Environ::core;
    m_recordedTable.clear();
    m_currentTable.clear();
    if (m_selected < 0 ||
        m_selected >= static_cast<int>(m_snapshotList.size()))
        return;

    const auto& snap{m_snapshotList[static_cast<size_t>(m_selected)]};
    auto snapVars{m_snapshots.load_snapshot(snap.id)};
    auto snapUser{reconstruct_variables(snapVars, Scope::User)};
    auto snapMachine{reconstruct_variables(snapVars, Scope::Machine)};

    // "Difference from current" table.
    m_currentTable = build_diff_table(L"Current", L"Snapshot",
        m_curUser, m_curMachine, snapUser, snapMachine);

    // "Recorded changes" table -- compare against the previous snapshot.
    const size_t nextIdx{static_cast<size_t>(m_selected) + 1};
    if (nextIdx < m_snapshotList.size())
    {
        auto prevVars{m_snapshots.load_snapshot(m_snapshotList[nextIdx].id)};
        auto prevUser{reconstruct_variables(prevVars, Scope::User)};
        auto prevMachine{reconstruct_variables(prevVars, Scope::Machine)};
        m_recordedTable = build_diff_table(L"Before", L"After",
            prevUser, prevMachine, snapUser, snapMachine);
    }
    else
    {
        // Oldest snapshot -- no previous.
        std::vector<EnvVariable> empty;
        m_recordedTable = build_diff_table(L"Before", L"After",
            empty, empty, snapUser, snapMachine);
    }
}

void ui::HistoryView::DeleteSelected()
{
    if (m_selected < 0 || m_selected >= static_cast<int>(m_snapshotList.size()))
        return;
    const auto id{m_snapshotList[static_cast<size_t>(m_selected)].id};
    m_snapshots.delete_snapshot(id);
    m_snapshotList.erase(m_snapshotList.begin() + m_selected);
    if (m_selected >= static_cast<int>(m_snapshotList.size()))
        m_selected = static_cast<int>(m_snapshotList.size()) - 1;
    ComputeDiffTables();
}

void ui::HistoryView::PrepareRestore()
{
    using namespace Environ::core;
    if (m_selected < 0 || m_selected >= static_cast<int>(m_snapshotList.size()))
        return;

    const auto snapId{m_snapshotList[static_cast<size_t>(m_selected)].id};
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

    m_restoreData.curUser = std::move(curUser);
    m_restoreData.curMachine = std::move(curMachine);
    m_restoreData.snapUser = std::move(snapUser);
    m_restoreData.snapMachine = std::move(snapMachine);
}

// ---------------------------------------------------------------------------
// Drawing helpers
// ---------------------------------------------------------------------------

void ui::HistoryView::DrawButton(const ViewContext& ctx, const D2D1_RECT_F& r,
                                  const wchar_t* label, bool primary, bool hover) const
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
