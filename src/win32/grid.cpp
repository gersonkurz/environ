#include "precomp.h"
#include "grid.h"

namespace ui
{
    namespace
    {
        constexpr float kPad{12.0f};

        bool Contains(const D2D1_RECT_F& r, float x, float y)
        {
            return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
        }

        void DrawString(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush,
                      const std::wstring& s, IDWriteTextFormat* fmt,
                      const D2D1_RECT_F& box, const D2D1_COLOR_F& color)
        {
            if (s.empty()) return;
            brush->SetColor(color);
            rt->DrawTextW(s.c_str(), static_cast<UINT32>(s.size()), fmt, box, brush,
                          D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
    }

    void Grid::SetData(const std::vector<Environ::core::EnvVariable>& userVars,
                       const std::vector<Environ::core::EnvVariable>& machineVars,
                       bool elevated)
    {
        using Environ::core::EnvVariable;
        using Environ::core::EnvVariableKind;
        using Environ::core::Scope;

        m_userOrig = userVars;
        m_machineOrig = machineVars;
        m_userStruct.assign(userVars.size(), false);
        m_machineStruct.assign(machineVars.size(), false);
        m_rows.clear();
        m_scrollY = 0.0f;
        m_hover = -1;
        m_selected = -1;
        m_selection.clear();
        m_editing = -1;
        m_editingName = false;

        const auto addGroup = [&](const std::vector<EnvVariable>& vars, Scope scope, bool readOnly) {
            for (int vi{0}; vi < static_cast<int>(vars.size()); ++vi)
            {
                const EnvVariable& v{vars[static_cast<size_t>(vi)]};
                Row var{};
                var.kind = Row::Kind::Variable;
                var.col1 = v.name;
                var.col1Original = v.name;
                var.depth = 0;
                var.readOnly = readOnly;
                var.scope = scope;
                var.varIndex = vi;

                if (v.kind == EnvVariableKind::PathList && !v.segments.empty())
                {
                    // First folder sits on the variable row; the rest stack beneath it.
                    var.col2 = v.segments[0];
                    var.original = var.col2;
                    var.segIndex = 0;
                    var.invalid = !v.segment_valid.empty() && !v.segment_valid[0];
                    var.duplicate = !v.segment_duplicate.empty() && !v.segment_duplicate[0].empty();
                    m_rows.push_back(std::move(var));
                    for (size_t i{1}; i < v.segments.size(); ++i)
                    {
                        Row seg{};
                        seg.kind = Row::Kind::Segment;
                        seg.col2 = v.segments[i];
                        seg.original = seg.col2;
                        seg.depth = 1;
                        seg.readOnly = readOnly;
                        seg.scope = scope;
                        seg.varIndex = vi;
                        seg.segIndex = static_cast<int>(i);
                        seg.invalid = (i < v.segment_valid.size()) && !v.segment_valid[i];
                        seg.duplicate = (i < v.segment_duplicate.size()) && !v.segment_duplicate[i].empty();
                        m_rows.push_back(std::move(seg));
                    }
                }
                else
                {
                    var.col2 = v.value;
                    var.original = var.col2;
                    var.segIndex = -1;
                    m_rows.push_back(std::move(var));
                }
            }
        };

        addGroup(userVars, Scope::User, false);
        addGroup(machineVars, Scope::Machine, !elevated);
    }

    void Grid::SetDataForRestore(
        const std::vector<Environ::core::EnvVariable>& currentUser,
        const std::vector<Environ::core::EnvVariable>& currentMachine,
        const std::vector<Environ::core::EnvVariable>& snapshotUser,
        const std::vector<Environ::core::EnvVariable>& snapshotMachine,
        bool elevated)
    {
        using Environ::core::EnvVariable;
        using Environ::core::EnvVariableKind;
        using Environ::core::Scope;

        // The originals are the CURRENT registry state — compute_diff later compares
        // current (originals in the grid) vs the snapshot values (displayed in the grid).
        m_userOrig = currentUser;
        m_machineOrig = currentMachine;
        m_rows.clear();
        m_scrollY = 0.0f;
        m_hover = -1;
        m_selected = -1;
        m_selection.clear();
        m_editing = -1;
        m_editingName = false;

        // Build a case-insensitive name→index map for the current (original) variables.
        const auto buildMap = [](const std::vector<EnvVariable>& vars) {
            std::unordered_map<std::wstring, int> map;
            for (int i{0}; i < static_cast<int>(vars.size()); ++i)
            {
                std::wstring key{vars[static_cast<size_t>(i)].name};
                std::ranges::transform(key, key.begin(), ::towlower);
                map[key] = i;
            }
            return map;
        };
        auto userMap{buildMap(currentUser)};
        auto machineMap{buildMap(currentMachine)};

        // Track which originals are referenced, to detect deletions.
        std::vector<bool> userSeen(currentUser.size(), false);
        std::vector<bool> machineSeen(currentMachine.size(), false);

        const auto addRestoreGroup = [&](const std::vector<EnvVariable>& snapVars, Scope scope,
                                         bool readOnly, const std::vector<EnvVariable>& curVars,
                                         std::unordered_map<std::wstring, int>& curMap,
                                         std::vector<bool>& seen)
        {
            for (int si{0}; si < static_cast<int>(snapVars.size()); ++si)
            {
                const EnvVariable& sv{snapVars[static_cast<size_t>(si)]};

                // Find the matching current variable (by name, case-insensitive).
                std::wstring key{sv.name};
                std::ranges::transform(key, key.begin(), ::towlower);
                auto it{curMap.find(key)};
                const int origIdx{it != curMap.end() ? it->second : -1};
                if (origIdx >= 0) seen[static_cast<size_t>(origIdx)] = true;

                Row var{};
                var.kind = Row::Kind::Variable;
                var.col1 = sv.name;
                var.col1Original = sv.name;
                var.depth = 0;
                var.readOnly = readOnly;
                var.scope = scope;
                var.varIndex = origIdx; // -1 if new in snapshot (absent from registry)
                var.segIndex = -1;

                if (sv.kind == EnvVariableKind::PathList && !sv.segments.empty())
                {
                    // Snapshot value for col2; original from current registry for dirty detection.
                    var.col2 = sv.segments[0];
                    var.segIndex = 0;

                    if (origIdx >= 0 && curVars[static_cast<size_t>(origIdx)].kind == EnvVariableKind::PathList
                        && !curVars[static_cast<size_t>(origIdx)].segments.empty())
                        var.original = curVars[static_cast<size_t>(origIdx)].segments[0];
                    else
                        var.original = L""; // new or type-changed → always dirty

                    var.invalid = !sv.segment_valid.empty() && !sv.segment_valid[0];
                    var.duplicate = !sv.segment_duplicate.empty() && !sv.segment_duplicate[0].empty();
                    m_rows.push_back(std::move(var));

                    for (size_t i{1}; i < sv.segments.size(); ++i)
                    {
                        Row seg{};
                        seg.kind = Row::Kind::Segment;
                        seg.col2 = sv.segments[i];
                        seg.depth = 1;
                        seg.readOnly = readOnly;
                        seg.scope = scope;
                        seg.varIndex = origIdx;
                        seg.segIndex = static_cast<int>(i);

                        if (origIdx >= 0 && curVars[static_cast<size_t>(origIdx)].kind == EnvVariableKind::PathList
                            && i < curVars[static_cast<size_t>(origIdx)].segments.size())
                            seg.original = curVars[static_cast<size_t>(origIdx)].segments[i];
                        else
                            seg.original = L"";

                        seg.invalid = (i < sv.segment_valid.size()) && !sv.segment_valid[i];
                        seg.duplicate = (i < sv.segment_duplicate.size()) && !sv.segment_duplicate[i].empty();
                        m_rows.push_back(std::move(seg));
                    }
                }
                else
                {
                    var.col2 = sv.value;
                    if (origIdx >= 0)
                        var.original = curVars[static_cast<size_t>(origIdx)].value;
                    else
                        var.original = L""; // new → dirty
                    m_rows.push_back(std::move(var));
                }
            }
        };

        addRestoreGroup(snapshotUser, Scope::User, false, currentUser, userMap, userSeen);
        addRestoreGroup(snapshotMachine, Scope::Machine, !elevated, currentMachine, machineMap, machineSeen);

        // Structural flags: mark originals that differ structurally from their snapshot
        // counterpart, or that are absent from the snapshot (deletions).
        m_userStruct.assign(currentUser.size(), false);
        m_machineStruct.assign(currentMachine.size(), false);

        // Mark unseen originals as structurally edited (they become deletions).
        for (size_t i{0}; i < userSeen.size(); ++i)
            if (!userSeen[i]) m_userStruct[i] = true;
        for (size_t i{0}; i < machineSeen.size(); ++i)
            if (!machineSeen[i]) m_machineStruct[i] = true;

        // Mark any variable where the snapshot segment count differs from the original
        // (structural change needed for correct reconstruction by CurrentVars).
        for (const auto& r : m_rows)
        {
            if (r.kind != Row::Kind::Variable || r.varIndex < 0) continue;
            const auto& curVars{(r.scope == Scope::User) ? currentUser : currentMachine};
            auto& flags{(r.scope == Scope::User) ? m_userStruct : m_machineStruct};
            if (r.varIndex >= static_cast<int>(curVars.size())) continue;
            const auto& cv{curVars[static_cast<size_t>(r.varIndex)]};
            const auto& snapVars{(r.scope == Scope::User) ? snapshotUser : snapshotMachine};

            // Find the snapshot variable by name
            for (const auto& sv : snapVars)
            {
                if (_wcsicmp(sv.name.c_str(), cv.name.c_str()) == 0)
                {
                    if (sv.kind != cv.kind
                        || (sv.kind == EnvVariableKind::PathList && sv.segments.size() != cv.segments.size()))
                        flags[static_cast<size_t>(r.varIndex)] = true;
                    break;
                }
            }
        }
    }

    bool Grid::HasChanges() const
    {
        for (bool f : m_userStruct) if (f) return true;       // added/removed/reordered entries
        for (bool f : m_machineStruct) if (f) return true;
        for (const Row& r : m_rows)
        {
            if (r.varIndex == -1) return true;                // new variable exists
            if (r.col2 != r.original || (r.kind == Row::Kind::Variable && r.col1 != r.col1Original))
                return true;
        }
        return false;
    }

    std::optional<Grid::SelectionDetail> Grid::GetSelectionDetail() const
    {
        if (m_selected < 0 || m_selected >= static_cast<int>(m_rows.size()))
            return std::nullopt;

        const Row& r{m_rows[static_cast<size_t>(m_selected)]};

        // Suppress stale data when the row has been edited.
        if (r.col2 != r.original) return std::nullopt;

        const auto& orig{(r.scope == Environ::core::Scope::User) ? m_userOrig : m_machineOrig};
        if (r.varIndex < 0 || r.varIndex >= static_cast<int>(orig.size()))
            return std::nullopt;

        const Environ::core::EnvVariable& v{orig[static_cast<size_t>(r.varIndex)]};
        SelectionDetail d{};
        d.displayPath = r.col2;

        if (r.segIndex >= 0) // path-list segment
        {
            const auto si{static_cast<size_t>(r.segIndex)};
            d.isSegment = true;
            d.expandedPath = (si < v.expanded_segments.size()) ? v.expanded_segments[si] : r.col2;
            d.valid = (si < v.segment_valid.size()) ? v.segment_valid[si] : true;
            d.duplicateDesc = (si < v.segment_duplicate.size()) ? v.segment_duplicate[si] : std::wstring{};
        }
        else // scalar
        {
            d.isSegment = false;
            d.expandedPath = v.expanded_value;
            d.valid = true;
        }
        return d;
    }

    const std::vector<Environ::core::EnvVariable>& Grid::OriginalVars(Environ::core::Scope scope) const
    {
        return (scope == Environ::core::Scope::User) ? m_userOrig : m_machineOrig;
    }

    std::vector<Environ::core::EnvVariable> Grid::CurrentVars(Environ::core::Scope scope) const
    {
        using Environ::core::EnvVariable;
        using Environ::core::EnvVariableKind;
        std::vector<EnvVariable> result{(scope == Environ::core::Scope::User) ? m_userOrig : m_machineOrig};
        const std::vector<bool>& structEdited{(scope == Environ::core::Scope::User) ? m_userStruct : m_machineStruct};

        // Track which original varIndexes are still referenced by rows, so we can detect
        // deletions after the walk.
        std::vector<bool> seen(result.size(), false);

        // New variables (varIndex == -1) collected during the walk; appended at the end.
        std::vector<EnvVariable> newVars;

        // Reconstruct each variable from its contiguous block of rows in display order: the
        // variable row holds the name (and the first entry), segment rows hold the rest.
        for (size_t i{0}; i < m_rows.size();)
        {
            if (m_rows[i].scope != scope) { ++i; continue; }
            const int vi{m_rows[i].varIndex};

            std::wstring name;
            std::wstring nameOriginal;
            std::vector<std::wstring> entries;
            size_t j{i};
            for (; j < m_rows.size() && m_rows[j].scope == scope && m_rows[j].varIndex == vi; ++j)
            {
                if (m_rows[j].kind == Row::Kind::Variable)
                {
                    name = m_rows[j].col1;
                    nameOriginal = m_rows[j].col1Original;
                }
                entries.push_back(m_rows[j].col2);
            }
            i = j;

            // New variable (not yet saved): build a fresh EnvVariable and collect it.
            if (vi == -1)
            {
                EnvVariable nv{};
                nv.name = name;
                if (entries.size() > 1)
                {
                    nv.segments = entries;
                    nv.value = Environ::core::join_segments(entries);
                    nv.kind = EnvVariableKind::PathList;
                }
                else
                {
                    nv.value = entries.empty() ? std::wstring{} : entries.front();
                    nv.kind = EnvVariableKind::Scalar;
                }
                nv.is_expandable = false;
                newVars.push_back(std::move(nv));
                continue;
            }

            if (vi < 0 || vi >= static_cast<int>(result.size())) continue;
            seen[static_cast<size_t>(vi)] = true;
            EnvVariable& v{result[static_cast<size_t>(vi)]};

            if (name != nameOriginal) // rename: compute_diff turns original_name into a Rename
            {
                v.original_name = nameOriginal;
                v.name = name;
            }

            if (v.kind != EnvVariableKind::PathList)
            {
                v.value = entries.empty() ? std::wstring{} : entries.front(); // scalar
                continue;
            }

            v.segments = entries;
            if (structEdited[static_cast<size_t>(vi)])
            {
                // Added / removed / reordered → clean re-join (original structure no longer applies).
                v.value = Environ::core::join_segments(entries);
            }
            else
            {
                // In-place edits only → preserve the original separator structure (empty/trailing
                // entries the display split drops); untouched entries reproduce the original value.
                v.value = Environ::core::apply_segment_edits(v.value, entries);
            }
        }

        // Remove deleted variables: structural flag set but no rows reference them.
        for (size_t k{result.size()}; k > 0; --k)
        {
            const size_t idx{k - 1};
            if (!seen[idx] && idx < structEdited.size() && structEdited[idx])
                result.erase(result.begin() + static_cast<long long>(idx));
        }

        // Append newly created variables.
        for (auto& nv : newVars)
            result.push_back(std::move(nv));

        return result;
    }

    void Grid::SetZoom(float zoom)
    {
        m_rowH = 30.0f * zoom;
        m_headerH = 32.0f * zoom;
        ClampScroll();
    }

    Grid::Layout Grid::Compute() const
    {
        Layout lay{};
        lay.header = D2D1::RectF(m_bounds.left, m_bounds.top, m_bounds.right, m_bounds.top + m_headerH);
        lay.data = D2D1::RectF(m_bounds.left, m_bounds.top + m_headerH, m_bounds.right, m_bounds.bottom);
        lay.viewH = std::max(0.0f, lay.data.bottom - lay.data.top);
        lay.contentH = static_cast<float>(m_rows.size()) * m_rowH;
        lay.maxScroll = std::max(0.0f, lay.contentH - lay.viewH);
        lay.hasScrollbar = lay.contentH > lay.viewH + 0.5f;

        if (lay.hasScrollbar)
        {
            const float thumbH = std::max(28.0f, lay.viewH * (lay.viewH / lay.contentH));
            const float frac = (lay.maxScroll > 0.0f) ? (m_scrollY / lay.maxScroll) : 0.0f;
            const float thumbTop = lay.data.top + frac * (lay.viewH - thumbH);
            lay.thumb = D2D1::RectF(lay.data.right - m_scrollbarW + 2.0f, thumbTop,
                                    lay.data.right - 2.0f, thumbTop + thumbH);
        }
        return lay;
    }

    int Grid::RowAtPoint(const Layout& lay, float x, float y) const
    {
        const float rightEdge = lay.data.right - (lay.hasScrollbar ? m_scrollbarW : 0.0f);
        if (x < lay.data.left || x >= rightEdge || y < lay.data.top || y >= lay.data.bottom)
            return -1;
        const int idx = static_cast<int>((y - lay.data.top + m_scrollY) / m_rowH);
        return (idx >= 0 && idx < static_cast<int>(m_rows.size())) ? idx : -1;
    }

    void Grid::ClampScroll()
    {
        const Layout lay{Compute()};
        m_scrollY = std::clamp(m_scrollY, 0.0f, lay.maxScroll);
    }

    void Grid::ClearAndSelectFocus()
    {
        m_selection.clear();
        if (m_selected >= 0) m_selection.insert(m_selected);
    }

    float Grid::NameColWidth() const
    {
        return std::min(260.0f, (m_bounds.right - m_bounds.left) * 0.34f);
    }

    D2D1_RECT_F Grid::ValueCellRect(int row) const
    {
        const Layout lay{Compute()};
        const float rightEdge = lay.data.right - (lay.hasScrollbar ? m_scrollbarW : 0.0f);
        const float top = lay.data.top + static_cast<float>(row) * m_rowH - m_scrollY;
        return D2D1::RectF(lay.data.left + kPad + NameColWidth(), top, rightEdge - kPad, top + m_rowH);
    }

    D2D1_RECT_F Grid::NameCellRect(int row) const
    {
        const Layout lay{Compute()};
        const float top = lay.data.top + static_cast<float>(row) * m_rowH - m_scrollY;
        return D2D1::RectF(lay.data.left + kPad, top, lay.data.left + NameColWidth(), top + m_rowH);
    }

    bool Grid::SelectionEditable() const
    {
        return m_selected >= 0 && m_selected < static_cast<int>(m_rows.size())
            && !m_rows[static_cast<size_t>(m_selected)].readOnly;
    }

    std::optional<Grid::EditTarget> Grid::BeginEdit()
    {
        if (!SelectionEditable()) return std::nullopt;
        EnsureVisible(m_selected);
        m_editing = m_selected;
        m_editingName = false; // Enter edits the value
        return EditTarget{ValueCellRect(m_selected), m_rows[static_cast<size_t>(m_selected)].col2, false};
    }

    std::optional<Grid::EditTarget> Grid::BeginEditName()
    {
        if (!SelectionEditable()) return std::nullopt;
        if (m_rows[static_cast<size_t>(m_selected)].kind != Row::Kind::Variable) return std::nullopt;
        EnsureVisible(m_selected);
        m_editing = m_selected;
        m_editingName = true;
        return EditTarget{NameCellRect(m_selected), m_rows[static_cast<size_t>(m_selected)].col1, true};
    }

    void Grid::CommitEdit(const std::wstring& text)
    {
        if (m_editing < 0) return;
        Row& r = m_rows[static_cast<size_t>(m_editing)];
        if (m_editingName)
        {
            r.col1 = text;
        }
        else
        {
            r.col2 = text;
            // Validity/duplicate flags are stale once edited; recomputed at save.
            r.invalid = false;
            r.duplicate = false;
        }
        m_editing = -1;
    }

    void Grid::CancelEdit()
    {
        m_editing = -1;
    }

    bool Grid::SelectedIsPathEntry() const
    {
        if (m_selected < 0 || m_selected >= static_cast<int>(m_rows.size())) return false;
        const Row& r{m_rows[static_cast<size_t>(m_selected)]};
        if (r.readOnly) return false;
        const std::vector<Environ::core::EnvVariable>& orig{
            (r.scope == Environ::core::Scope::User) ? m_userOrig : m_machineOrig};
        return r.varIndex >= 0 && r.varIndex < static_cast<int>(orig.size())
            && orig[static_cast<size_t>(r.varIndex)].kind == Environ::core::EnvVariableKind::PathList;
    }

    bool Grid::AddEntry()
    {
        if (!SelectedIsPathEntry()) return false;
        const Environ::core::Scope scope{m_rows[static_cast<size_t>(m_selected)].scope};
        const int vi{m_rows[static_cast<size_t>(m_selected)].varIndex};

        Row entry{};
        entry.kind = Row::Kind::Segment;
        entry.depth = 1;
        entry.scope = scope;
        entry.varIndex = vi;
        m_rows.insert(m_rows.begin() + m_selected + 1, entry);
        m_selected += 1; // select the new (blank) entry so the host can edit it
        ClearAndSelectFocus();

        std::vector<bool>& flags{(scope == Environ::core::Scope::User) ? m_userStruct : m_machineStruct};
        if (vi >= 0 && vi < static_cast<int>(flags.size())) flags[static_cast<size_t>(vi)] = true;
        EnsureVisible(m_selected);
        return true;
    }

    bool Grid::RemoveEntry()
    {
        if (!SelectedIsPathEntry()) return false;
        const Environ::core::Scope scope{m_rows[static_cast<size_t>(m_selected)].scope};
        const int vi{m_rows[static_cast<size_t>(m_selected)].varIndex};

        if (m_rows[static_cast<size_t>(m_selected)].kind == Row::Kind::Segment)
        {
            m_rows.erase(m_rows.begin() + m_selected);
            if (m_selected >= static_cast<int>(m_rows.size())) m_selected = static_cast<int>(m_rows.size()) - 1;
        }
        else
        {
            // Variable row holds the first entry (and the name): promote the next entry into it,
            // or blank it if this was the last entry.
            const size_t next{static_cast<size_t>(m_selected) + 1};
            if (next < m_rows.size() && m_rows[next].scope == scope && m_rows[next].varIndex == vi
                && m_rows[next].kind == Row::Kind::Segment)
            {
                m_rows[static_cast<size_t>(m_selected)].col2 = m_rows[next].col2;
                m_rows.erase(m_rows.begin() + static_cast<long long>(next));
            }
            else
            {
                m_rows[static_cast<size_t>(m_selected)].col2.clear();
            }
        }

        ClearAndSelectFocus();
        std::vector<bool>& flags{(scope == Environ::core::Scope::User) ? m_userStruct : m_machineStruct};
        if (vi >= 0 && vi < static_cast<int>(flags.size())) flags[static_cast<size_t>(vi)] = true;
        ClampScroll();
        return true;
    }

    bool Grid::AddVariable()
    {
        // Determine scope: use the selected row's scope if available and editable,
        // otherwise default to User (always editable).
        Environ::core::Scope scope{Environ::core::Scope::User};
        if (HasSelection() && !m_rows[static_cast<size_t>(m_selected)].readOnly)
            scope = m_rows[static_cast<size_t>(m_selected)].scope;

        // Find insertion point: after the last row belonging to the same scope section.
        // If we have a selection in that scope, insert after the selected variable's group.
        int insertAt{0};
        if (HasSelection() && m_rows[static_cast<size_t>(m_selected)].scope == scope)
        {
            const int vi{m_rows[static_cast<size_t>(m_selected)].varIndex};
            size_t j{static_cast<size_t>(m_selected)};
            while (j + 1 < m_rows.size() && m_rows[j + 1].scope == scope && m_rows[j + 1].varIndex == vi)
                ++j;
            insertAt = static_cast<int>(j) + 1;
        }
        else
        {
            // Append at the end of the scope's section.
            for (size_t i{0}; i < m_rows.size(); ++i)
            {
                if (m_rows[i].scope == scope)
                    insertAt = static_cast<int>(i) + 1;
            }
        }

        Row var{};
        var.kind = Row::Kind::Variable;
        var.depth = 0;
        var.readOnly = false;
        var.scope = scope;
        var.varIndex = -1; // sentinel: new variable
        var.segIndex = -1;
        m_rows.insert(m_rows.begin() + insertAt, std::move(var));
        m_selected = insertAt;
        ClearAndSelectFocus();
        EnsureVisible(m_selected);
        return true;
    }

    bool Grid::RemoveVariable()
    {
        if (!HasSelection()) return false;
        const Row& sel{m_rows[static_cast<size_t>(m_selected)]};
        if (sel.readOnly) return false;

        const Environ::core::Scope scope{sel.scope};
        const int vi{sel.varIndex};

        if (vi == -1)
        {
            // New, unsaved variable: just erase the single row.
            m_rows.erase(m_rows.begin() + m_selected);
        }
        else
        {
            // Existing variable: erase ALL rows with this scope+varIndex.
            std::vector<bool>& flags{(scope == Environ::core::Scope::User) ? m_userStruct : m_machineStruct};
            if (vi >= 0 && vi < static_cast<int>(flags.size()))
                flags[static_cast<size_t>(vi)] = true;

            for (auto it{m_rows.begin()}; it != m_rows.end();)
            {
                if (it->scope == scope && it->varIndex == vi)
                    it = m_rows.erase(it);
                else
                    ++it;
            }
        }

        if (m_selected >= static_cast<int>(m_rows.size()))
            m_selected = static_cast<int>(m_rows.size()) - 1;
        ClearAndSelectFocus();
        ClampScroll();
        return true;
    }

    bool Grid::MoveEntry(int dir)
    {
        if (!SelectedIsPathEntry()) return false;
        const int other{m_selected + dir};
        if (other < 0 || other >= static_cast<int>(m_rows.size())) return false;
        const Environ::core::Scope scope{m_rows[static_cast<size_t>(m_selected)].scope};
        const int vi{m_rows[static_cast<size_t>(m_selected)].varIndex};
        if (m_rows[static_cast<size_t>(other)].scope != scope
            || m_rows[static_cast<size_t>(other)].varIndex != vi)
            return false; // can't move across variables

        std::swap(m_rows[static_cast<size_t>(m_selected)].col2, m_rows[static_cast<size_t>(other)].col2);
        m_selected = other; // selection follows the moved entry
        ClearAndSelectFocus();

        std::vector<bool>& flags{(scope == Environ::core::Scope::User) ? m_userStruct : m_machineStruct};
        if (vi >= 0 && vi < static_cast<int>(flags.size())) flags[static_cast<size_t>(vi)] = true;
        EnsureVisible(m_selected);
        return true;
    }

    std::optional<Grid::EditTarget> Grid::BeginEditAt(float x, float y)
    {
        const Layout lay{Compute()};
        const int row{RowAtPoint(lay, x, y)};
        if (row < 0) return std::nullopt;
        m_selected = row; // selection moves on any row double-click
        const Row& r{m_rows[static_cast<size_t>(row)]};
        if (r.readOnly) return std::nullopt;

        // Name column (Variable rows only) → rename; value column → value.
        if (r.kind == Row::Kind::Variable)
        {
            const D2D1_RECT_F nameCell{NameCellRect(row)};
            if (x >= nameCell.left && x < nameCell.right)
            {
                m_editing = row;
                m_editingName = true;
                return EditTarget{nameCell, r.col1, true};
            }
        }
        const D2D1_RECT_F valueCell{ValueCellRect(row)};
        if (x < valueCell.left || x >= valueCell.right) return std::nullopt;
        m_editing = row;
        m_editingName = false;
        return EditTarget{valueCell, r.col2, false};
    }

    bool Grid::SelectNextEditable(int dir)
    {
        const int n{static_cast<int>(m_rows.size())};
        for (int i{m_selected + dir}; i >= 0 && i < n; i += dir)
        {
            if (!m_rows[static_cast<size_t>(i)].readOnly)
            {
                m_selected = i;
                ClearAndSelectFocus();
                EnsureVisible(i);
                return true;
            }
        }
        return false;
    }

    void Grid::EnsureVisible(int row)
    {
        const float viewH = m_bounds.bottom - m_bounds.top - m_headerH;
        if (viewH <= 0.0f) return;
        const float top = static_cast<float>(row) * m_rowH;
        const float bottom = top + m_rowH;
        if (top < m_scrollY) m_scrollY = top;
        else if (bottom > m_scrollY + viewH) m_scrollY = bottom - viewH;
        ClampScroll();
    }

    void Grid::Paint(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush,
                     const GridFonts& fonts, const theme::ColorScheme& s,
                     const D2D1_RECT_F& bounds)
    {
        m_bounds = bounds;
        ClampScroll();
        const Layout lay{Compute()};

        const float nameCol{NameColWidth()};

        // Column header band.
        brush->SetColor(s.header.fill);
        rt->FillRectangle(lay.header, brush);
        DrawString(rt, brush, L"NAME", fonts.header,
                 D2D1::RectF(lay.header.left + kPad, lay.header.top, lay.header.left + nameCol, lay.header.bottom),
                 s.header.text);
        DrawString(rt, brush, L"VALUE", fonts.header,
                 D2D1::RectF(lay.header.left + kPad + nameCol, lay.header.top, lay.header.right, lay.header.bottom),
                 s.header.text);
        if (s.header.borderWidth > 0.0f)
        {
            brush->SetColor(s.header.border);
            rt->DrawLine(D2D1::Point2F(lay.header.left, lay.header.bottom - 0.5f),
                         D2D1::Point2F(lay.header.right, lay.header.bottom - 0.5f),
                         brush, s.header.borderWidth);
        }

        rt->PushAxisAlignedClip(lay.data, D2D1_ANTIALIAS_MODE_ALIASED);

        const float rightEdge{lay.data.right - (lay.hasScrollbar ? m_scrollbarW : 0.0f)};
        int i{static_cast<int>(m_scrollY / m_rowH)};
        float y{lay.data.top - (m_scrollY - static_cast<float>(i) * m_rowH)};
        for (; i < static_cast<int>(m_rows.size()) && y < lay.data.bottom; ++i, y += m_rowH)
        {
            const Row& r{m_rows[static_cast<size_t>(i)]};
            const bool selected = m_selection.contains(i);
            const bool focused = (i == m_selected);
            const bool hovered = (i == m_hover && !selected);
            const std::vector<bool>& sf{(r.scope == Environ::core::Scope::User) ? m_userStruct : m_machineStruct};
            const bool varStruct{r.varIndex >= 0 && r.varIndex < static_cast<int>(sf.size())
                                 && sf[static_cast<size_t>(r.varIndex)]};
            const bool valueDirty = (r.col2 != r.original) || varStruct;
            const bool nameDirty = (r.kind == Row::Kind::Variable && r.col1 != r.col1Original);
            const D2D1_RECT_F rowRect = D2D1::RectF(lay.data.left, y, rightEdge, y + m_rowH);

            // Fill (selection / hover win; then per-segment state; then base row).
            D2D1_COLOR_F fill{s.row.fill};
            if (selected)        fill = s.rowSelected.fill;
            else if (hovered)    fill = s.rowHover.fill;
            else if (r.invalid)  fill = s.rowInvalid.fill;
            else if (valueDirty || nameDirty) fill = s.rowDirty.fill;
            else if (r.duplicate)fill = s.rowDuplicate.fill;
            brush->SetColor(fill);
            rt->FillRectangle(rowRect, brush);

            if (!selected && !hovered && r.invalid && s.rowInvalid.borderWidth > 0.0f)
            {
                brush->SetColor(s.rowInvalid.border);
                rt->DrawRectangle(D2D1::RectF(rowRect.left + 0.5f, rowRect.top + 0.5f,
                                              rowRect.right - 0.5f, rowRect.bottom - 0.5f),
                                  brush, s.rowInvalid.borderWidth);
            }

            // The name column reflects rename-dirty (not path state — the name isn't a path).
            D2D1_COLOR_F nameColor{s.row.text};
            if (nameDirty)       nameColor = s.rowDirty.text;
            else if (selected)   nameColor = s.rowSelected.text;
            else if (r.readOnly) nameColor = s.readonlyText;
            else if (hovered)    nameColor = s.rowHover.text;

            // The value column reflects the (first) path segment's state.
            // Dirty/invalid win over selection so a just-committed (still-selected) row
            // visibly shows its changed state.
            D2D1_COLOR_F valueColor{s.row.text};
            if (r.invalid)        valueColor = s.rowInvalid.text;
            else if (valueDirty)  valueColor = s.rowDirty.text;
            else if (selected)    valueColor = s.rowSelected.text;
            else if (r.duplicate) valueColor = s.rowDuplicate.text;
            else if (r.readOnly)  valueColor = s.readonlyText;
            else if (hovered)     valueColor = s.rowHover.text;

            if (focused)
            {
                brush->SetColor(s.accent);
                rt->FillRectangle(D2D1::RectF(rowRect.left, y + 6.0f, rowRect.left + 3.0f, y + m_rowH - 6.0f), brush);
            }

            if (r.kind == Row::Kind::Variable)
            {
                DrawString(rt, brush, r.col1, fonts.name,
                         D2D1::RectF(rowRect.left + kPad, y, rowRect.left + nameCol, y + m_rowH), nameColor);
                DrawString(rt, brush, r.col2, fonts.value,
                         D2D1::RectF(rowRect.left + kPad + nameCol, y, rowRect.right - kPad, y + m_rowH),
                         valueColor);
            }
            else
            {
                // Path segments align under the VALUE column — the variable's contents
                // laid out across multiple lines, not indented from the name.
                DrawString(rt, brush, r.col2, fonts.value,
                         D2D1::RectF(rowRect.left + kPad + nameCol, y, rowRect.right - kPad, y + m_rowH), valueColor);
            }
        }

        rt->PopAxisAlignedClip();

        if (lay.hasScrollbar)
        {
            D2D1_COLOR_F thumb = s.headerSubtext;
            thumb.a = m_draggingThumb ? 0.85f : 0.5f;
            brush->SetColor(thumb);
            rt->FillRoundedRectangle(D2D1::RoundedRect(lay.thumb, 4.0f, 4.0f), brush);
        }
    }

    bool Grid::OnMouseMove(float x, float y)
    {
        const Layout lay{Compute()};
        if (m_draggingThumb && lay.hasScrollbar)
        {
            const float thumbH = lay.thumb.bottom - lay.thumb.top;
            const float travel = lay.viewH - thumbH;
            const float newTop = y - m_dragGrabOffset - lay.data.top;
            m_scrollY = (travel > 0.0f) ? (newTop / travel) * lay.maxScroll : 0.0f;
            ClampScroll();
            return true;
        }
        const int r = RowAtPoint(lay, x, y);
        if (r != m_hover) { m_hover = r; return true; }
        return false;
    }

    bool Grid::OnMouseLeave()
    {
        if (m_hover != -1) { m_hover = -1; return true; }
        return false;
    }

    bool Grid::OnRButtonDown(float x, float y, bool /*shift*/, bool /*ctrl*/)
    {
        const Layout lay{Compute()};
        const int r = RowAtPoint(lay, x, y);
        if (r < 0) return false;

        if (m_selection.contains(r))
        {
            // Clicked row is already selected: keep the selection, just move focus.
            if (r != m_selected) { m_selected = r; return true; }
            return false;
        }
        // Clicked row is not in selection: clear and select just this row.
        m_selected = r;
        m_selection.clear();
        m_selection.insert(r);
        return true;
    }

    std::wstring Grid::CopyText() const
    {
        if (m_selection.empty()) return {};

        // Collect selected row indices in order.
        std::vector<int> sel(m_selection.begin(), m_selection.end());

        std::wstring result;
        size_t pos{0};
        while (pos < sel.size())
        {
            const int idx{sel[pos]};
            if (idx < 0 || idx >= static_cast<int>(m_rows.size())) { ++pos; continue; }
            const Row& r{m_rows[static_cast<size_t>(idx)]};

            if (r.kind == Row::Kind::Variable)
            {
                // Collect all contiguous selected rows belonging to this same variable.
                std::vector<std::wstring> entries;
                entries.push_back(r.col2);
                size_t j{pos + 1};
                while (j < sel.size())
                {
                    const int ni{sel[j]};
                    if (ni < 0 || ni >= static_cast<int>(m_rows.size())) break;
                    const Row& nr{m_rows[static_cast<size_t>(ni)]};
                    if (nr.scope != r.scope || nr.varIndex != r.varIndex) break;
                    entries.push_back(nr.col2);
                    ++j;
                }
                if (!result.empty()) result += L"\r\n";
                if (r.segIndex < 0)
                    result += r.col1 + L"=" + (entries.empty() ? std::wstring{} : entries.front());
                else
                    result += r.col1 + L"=" + Environ::core::join_segments(entries);
                pos = j;
            }
            else
            {
                // Standalone segment row (no Variable row selected above it).
                if (!result.empty()) result += L"\r\n";
                result += r.col2;
                ++pos;
            }
        }
        return result;
    }

    bool Grid::OnLButtonDown(float x, float y, bool shift, bool ctrl)
    {
        const Layout lay{Compute()};
        if (lay.hasScrollbar && Contains(lay.thumb, x, y))
        {
            m_draggingThumb = true;
            m_dragGrabOffset = y - lay.thumb.top;
            return true; // thumb darkens while grabbed
        }
        const int r = RowAtPoint(lay, x, y);
        if (r < 0) return false;

        if (ctrl)
        {
            // Toggle the clicked row in/out of the selection set.
            if (m_selection.contains(r))
                m_selection.erase(r);
            else
                m_selection.insert(r);
            m_selected = r;
        }
        else if (shift)
        {
            // Range-select from the current focus to the clicked row (inclusive).
            const int anchor{m_selected >= 0 ? m_selected : 0};
            const int lo{std::min(anchor, r)};
            const int hi{std::max(anchor, r)};
            for (int i{lo}; i <= hi; ++i)
                m_selection.insert(i);
            m_selected = r;
        }
        else
        {
            // Plain click: clear selection, set focus = clicked row.
            m_selected = r;
            m_selection.clear();
            m_selection.insert(r);
        }
        return true;
    }

    bool Grid::OnLButtonUp()
    {
        if (m_draggingThumb) { m_draggingThumb = false; return true; } // thumb lightens on release
        return false;
    }

    bool Grid::OnWheel(int delta)
    {
        m_scrollY -= (static_cast<float>(delta) / WHEEL_DELTA) * 3.0f * m_rowH;
        ClampScroll();
        return true;
    }

    bool Grid::OnKey(int vk)
    {
        if (m_rows.empty()) return false;
        const int n = static_cast<int>(m_rows.size());
        const float viewH = m_bounds.bottom - m_bounds.top - m_headerH;
        const int page = std::max(1, static_cast<int>(viewH / m_rowH));
        int sel = m_selected;

        switch (vk)
        {
        case VK_DOWN:  sel = (sel < 0) ? 0 : std::min(sel + 1, n - 1); break;
        case VK_UP:    sel = (sel < 0) ? 0 : std::max(sel - 1, 0); break;
        case VK_NEXT:  sel = (sel < 0) ? 0 : std::min(sel + page, n - 1); break;
        case VK_PRIOR: sel = (sel < 0) ? 0 : std::max(sel - page, 0); break;
        case VK_HOME:  sel = 0; break;
        case VK_END:   sel = n - 1; break;
        default:       return false;
        }

        m_selected = sel;
        ClearAndSelectFocus();
        EnsureVisible(sel);
        return true;
    }
}
