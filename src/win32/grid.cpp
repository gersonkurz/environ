#include "precomp.h"
#include "grid.h"

namespace ui
{
    namespace
    {
        constexpr float kPad{12.0f};

        // Per-row scope icons (Segoe Fluent Icons). Invariant to sort/filter order.
        constexpr wchar_t kGlyphUser{0xE77B};    // Contact (person)
        constexpr wchar_t kGlyphMachine{0xEC76}; // Devices (this PC)
        constexpr wchar_t kGlyphProcess{0xE72E}; // Lock (read-only / set by Windows)

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
                       const std::vector<Environ::core::EnvVariable>& processVars,
                       bool elevated)
    {
        using Environ::core::EnvVariable;
        using Environ::core::EnvVariableKind;
        using Environ::core::Scope;

        m_userOrig = userVars;
        m_machineOrig = machineVars;
        m_processVars = processVars;
        m_userStruct.assign(userVars.size(), false);
        m_machineStruct.assign(machineVars.size(), false);
        m_rows.clear();
        m_nextUnit = 0;
        m_scrollY = 0.0f;
        m_scrollX = 0.0f;
        m_needMeasure = true;
        m_hover = -1;
        m_selected = -1;
        m_selection.clear();
        m_editing = -1;
        m_editingName = false;

        const auto addGroup = [&](const std::vector<EnvVariable>& vars, RowGroup group,
                                  Scope scope, bool readOnly) {
            for (int vi{0}; vi < static_cast<int>(vars.size()); ++vi)
            {
                const EnvVariable& v{vars[static_cast<size_t>(vi)]};
                const int unit{m_nextUnit++};
                Row var{};
                var.kind = Row::Kind::Variable;
                var.group = group;
                var.col1 = v.name;
                var.col1Original = v.name;
                var.depth = 0;
                var.readOnly = readOnly;
                var.scope = scope;
                var.varIndex = vi;
                var.unit = unit;

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
                        seg.group = group;
                        seg.col2 = v.segments[i];
                        seg.original = seg.col2;
                        seg.depth = 1;
                        seg.readOnly = readOnly;
                        seg.scope = scope;
                        seg.varIndex = vi;
                        seg.segIndex = static_cast<int>(i);
                        seg.unit = unit;
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

        addGroup(userVars, RowGroup::User, Scope::User, false);
        addGroup(machineVars, RowGroup::Machine, Scope::Machine, !elevated);
        // Process extras are always read-only and never written; the dummy Scope is never used
        // (Process rows are excluded from CurrentVars/diff and the struct-dirty paint guard).
        addGroup(processVars, RowGroup::Process, Scope::User, true);
        ApplySort(); // merge the three groups into one globally-sorted list
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
        m_nextUnit = 0;
        m_scrollY = 0.0f;
        m_scrollX = 0.0f;
        m_needMeasure = true;
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

                const int unit{m_nextUnit++};
                Row var{};
                var.kind = Row::Kind::Variable;
                var.col1 = sv.name;
                var.col1Original = sv.name;
                var.depth = 0;
                var.readOnly = readOnly;
                var.group = (scope == Scope::User) ? RowGroup::User : RowGroup::Machine;
                var.scope = scope;
                var.varIndex = origIdx; // -1 if new in snapshot (absent from registry)
                var.segIndex = -1;
                var.unit = unit;

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
                        seg.group = (scope == Scope::User) ? RowGroup::User : RowGroup::Machine;
                        seg.scope = scope;
                        seg.varIndex = origIdx;
                        seg.segIndex = static_cast<int>(i);
                        seg.unit = unit;

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
        ApplySort(); // same global sort as the normal load path
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

        const auto& orig{varsForRow(r)};
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

    std::wstring Grid::SelectedVariableName() const
    {
        if (m_selected < 0 || m_selected >= static_cast<int>(m_rows.size()))
            return {};
        const Row& r{m_rows[static_cast<size_t>(m_selected)]};
        const auto& orig{varsForRow(r)};
        if (r.varIndex < 0 || r.varIndex >= static_cast<int>(orig.size()))
            return {};
        return orig[static_cast<size_t>(r.varIndex)].name;
    }

    bool Grid::SelectionShadowed() const
    {
        if (m_selected < 0 || m_selected >= static_cast<int>(m_rows.size())) return false;
        const Row& r{m_rows[static_cast<size_t>(m_selected)]};
        const auto& orig{varsForRow(r)};
        return r.varIndex >= 0 && r.varIndex < static_cast<int>(orig.size())
            && orig[static_cast<size_t>(r.varIndex)].shadowed;
    }

    std::wstring Grid::SelectionEffectiveValue() const
    {
        if (m_selected < 0 || m_selected >= static_cast<int>(m_rows.size())) return {};
        const Row& r{m_rows[static_cast<size_t>(m_selected)]};
        const auto& orig{varsForRow(r)};
        if (r.varIndex < 0 || r.varIndex >= static_cast<int>(orig.size())) return {};
        return orig[static_cast<size_t>(r.varIndex)].effective_value;
    }

    const std::vector<Environ::core::EnvVariable>& Grid::varsForRow(const Row& r) const
    {
        switch (r.group)
        {
        case RowGroup::Machine: return m_machineOrig;
        case RowGroup::Process: return m_processVars;
        case RowGroup::User:    break;
        }
        return m_userOrig;
    }

    std::wstring Grid::SelectedValueText() const
    {
        if (m_selected < 0 || m_selected >= static_cast<int>(m_rows.size()))
            return {};
        return m_rows[static_cast<size_t>(m_selected)].col2;
    }

    bool Grid::SetSelectedValue(const std::wstring& text)
    {
        if (!SelectionEditable()) return false;
        Row& r{m_rows[static_cast<size_t>(m_selected)]};
        r.col2 = text;
        r.invalid = false;   // stale until recomputed at save
        r.duplicate = false;
        m_needMeasure = true;
        return true;
    }

    std::optional<D2D1_RECT_F> Grid::SelectedValueCellRect() const
    {
        if (m_selected < 0 || m_selected >= static_cast<int>(m_rows.size()))
            return std::nullopt;
        const Layout lay{Compute()};
        const D2D1_RECT_F r{ValueCellRect(m_selected)};
        if (r.bottom <= lay.data.top || r.top >= lay.data.bottom) // scrolled off-screen
            return std::nullopt;
        return r;
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
            if (m_rows[i].group == RowGroup::Process) { ++i; continue; } // never written
            if (m_rows[i].scope != scope) { ++i; continue; }
            const int unit{m_rows[i].unit}; // a variable's rows share a stable unit id
            const int vi{m_rows[i].varIndex};

            std::wstring name;
            std::wstring nameOriginal;
            std::vector<std::wstring> entries;
            size_t j{i};
            for (; j < m_rows.size() && m_rows[j].unit == unit; ++j)
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

    void Grid::SetFilter(const std::wstring& text)
    {
        m_filterText.resize(text.size());
        std::ranges::transform(text, m_filterText.begin(), ::towlower);
        RebuildFiltered();
    }

    int Grid::FilteredRowCount() const
    {
        return FilteredCount();
    }

    void Grid::RebuildFiltered()
    {
        m_needMeasure = true; // visible set changed -> content width may differ
        if (m_filterText.empty())
        {
            m_filtered.clear();
            m_filteredPromoted.clear();
            m_filterActive = false;
            return;
        }

        m_filtered.clear();
        m_filteredPromoted.clear();
        m_filterActive = true;

        const auto matchesFilter = [this](const std::wstring& s) {
            if (s.empty()) return false;
            std::wstring lower(s.size(), L'\0');
            std::ranges::transform(s, lower.begin(), ::towlower);
            return lower.find(m_filterText) != std::wstring::npos;
        };

        // Walk rows grouped by variable (contiguous runs sharing scope + varIndex).
        for (size_t i{0}; i < m_rows.size();)
        {
            const int unit{m_rows[i].unit}; // a variable's rows share a stable unit id
            const int vi{m_rows[i].varIndex};

            // Collect the contiguous group.
            const size_t groupStart{i};
            size_t groupEnd{i};
            while (groupEnd < m_rows.size() && m_rows[groupEnd].unit == unit)
                ++groupEnd;

            // Always include all rows of new variables (varIndex == -1).
            if (vi == -1)
            {
                for (size_t j{groupStart}; j < groupEnd; ++j)
                    m_filtered.push_back(static_cast<int>(j));
                i = groupEnd;
                continue;
            }

            // Check if the variable name matches.
            bool nameMatch{false};
            for (size_t j{groupStart}; j < groupEnd && !nameMatch; ++j)
            {
                if (!m_rows[j].col1.empty() && matchesFilter(m_rows[j].col1))
                    nameMatch = true;
            }

            if (nameMatch)
            {
                // Name matches: include all rows of this variable.
                for (size_t j{groupStart}; j < groupEnd; ++j)
                    m_filtered.push_back(static_cast<int>(j));
            }
            else
            {
                // Name doesn't match: check individual rows for value matches.
                const size_t varRow{groupStart};

                // Single-row variable (scalar): include if value matches.
                if (groupEnd - groupStart == 1)
                {
                    if (matchesFilter(m_rows[varRow].col2))
                        m_filtered.push_back(static_cast<int>(varRow));
                }
                else
                {
                    // Multi-row path-list: collect rows whose col2 matches.
                    std::vector<int> matchingRows;
                    for (size_t j{groupStart}; j < groupEnd; ++j)
                    {
                        if (matchesFilter(m_rows[j].col2))
                            matchingRows.push_back(static_cast<int>(j));
                    }
                    if (!matchingRows.empty())
                    {
                        const int varIdx{static_cast<int>(varRow)};
                        const bool varRowMatched{matchingRows[0] == varIdx};

                        if (varRowMatched)
                        {
                            // First segment matched: Variable row naturally shows name + value.
                            for (int idx : matchingRows)
                                m_filtered.push_back(idx);
                        }
                        else
                        {
                            // First segment didn't match: skip the Variable row, promote
                            // the first matching segment to display the variable name.
                            m_filteredPromoted[matchingRows[0]] = m_rows[varRow].col1;
                            for (int idx : matchingRows)
                                m_filtered.push_back(idx);
                        }
                    }
                }
            }
            i = groupEnd;
        }

        // If current selection is not in the filtered set, move to first filtered row.
        if (m_selected >= 0 && ActualToFiltered(m_selected) < 0)
        {
            m_selected = m_filtered.empty() ? -1 : m_filtered[0];
        }

        // Intersect m_selection with filtered set.
        std::set<int> newSel;
        for (int idx : m_selection)
        {
            if (ActualToFiltered(idx) >= 0) newSel.insert(idx);
        }
        m_selection = std::move(newSel);
        if (m_selected >= 0 && !m_selection.contains(m_selected))
            m_selection.insert(m_selected);

        m_scrollY = 0.0f;
        ClampScroll();
    }

    int Grid::FilteredToActual(int fi) const
    {
        if (!m_filterActive) return fi;
        if (fi < 0 || fi >= static_cast<int>(m_filtered.size())) return -1;
        return m_filtered[static_cast<size_t>(fi)];
    }

    int Grid::ActualToFiltered(int actual) const
    {
        if (!m_filterActive) return actual;
        for (int i{0}; i < static_cast<int>(m_filtered.size()); ++i)
        {
            if (m_filtered[static_cast<size_t>(i)] == actual) return i;
        }
        return -1;
    }

    int Grid::FilteredCount() const
    {
        return m_filterActive ? static_cast<int>(m_filtered.size()) : static_cast<int>(m_rows.size());
    }

    void Grid::SetZoom(float zoom)
    {
        m_zoom = zoom;
        m_rowH = 30.0f * zoom;
        m_headerH = 32.0f * zoom;
        m_needMeasure = true; // value font scaled -> widths change
        ClampScroll();
    }

    Grid::Layout Grid::Compute() const
    {
        Layout lay{};
        lay.header = D2D1::RectF(m_bounds.left, m_bounds.top, m_bounds.right, m_bounds.top + m_headerH);
        lay.data = D2D1::RectF(m_bounds.left, m_bounds.top + m_headerH, m_bounds.right, m_bounds.bottom);
        lay.viewH = std::max(0.0f, lay.data.bottom - lay.data.top);
        lay.contentH = static_cast<float>(FilteredCount()) * m_rowH;
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

        // Horizontal value-column geometry (name column stays frozen).
        const float rightEdge{lay.data.right - (lay.hasScrollbar ? m_scrollbarW : 0.0f)};
        lay.valueLeft = lay.data.left + kPad + NameColWidth();
        lay.valueRight = rightEdge - kPad;
        const float valueViewW{std::max(0.0f, lay.valueRight - lay.valueLeft)};
        lay.maxScrollX = std::max(0.0f, m_contentW - valueViewW);
        lay.hasHScroll = lay.maxScrollX > 0.5f;
        if (lay.hasHScroll)
        {
            constexpr float hbarH{8.0f};
            const float thumbW{std::max(28.0f, valueViewW * (valueViewW / m_contentW))};
            const float fracX{(lay.maxScrollX > 0.0f) ? (m_scrollX / lay.maxScrollX) : 0.0f};
            const float thumbLeft{lay.valueLeft + fracX * (valueViewW - thumbW)};
            lay.hThumb = D2D1::RectF(thumbLeft, lay.data.bottom - hbarH,
                                     thumbLeft + thumbW, lay.data.bottom - 1.0f);
        }
        return lay;
    }

    void Grid::ApplySort()
    {
        if (m_rows.empty()) { RebuildFiltered(); return; }

        // Remember the focused row's identity (unit + segIndex) so it stays selected after
        // the reorder. Multi-selection across a re-sort collapses to focus.
        const bool hadSel{m_selected >= 0 && m_selected < static_cast<int>(m_rows.size())};
        int selUnit{-1}, selSeg{-1};
        if (hadSel)
        {
            const Row& r{m_rows[static_cast<size_t>(m_selected)]};
            selUnit = r.unit; selSeg = r.segIndex;
        }

        // Split rows into variable units: a Variable row plus its trailing Segment rows,
        // i.e. a contiguous run sharing Row::unit (a stable per-variable id).
        std::vector<std::pair<size_t, size_t>> units;
        for (size_t i{0}; i < m_rows.size();)
        {
            size_t j{i + 1};
            while (j < m_rows.size() && m_rows[j].unit == m_rows[i].unit)
                ++j;
            units.push_back({i, j});
            i = j;
        }

        // Sort key per unit: the Variable row (always the unit's first row) carries the
        // name/value. Case-insensitive.
        std::vector<std::wstring> keys(units.size());
        for (size_t u{0}; u < units.size(); ++u)
        {
            const Row& v{m_rows[units[u].first]};
            std::wstring k{m_sortColumn == SortColumn::Name ? v.col1 : v.col2};
            std::ranges::transform(k, k.begin(), ::towlower);
            keys[u] = std::move(k);
        }

        std::vector<size_t> order(units.size());
        for (size_t i{0}; i < order.size(); ++i) order[i] = i;
        std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
            const int c{keys[a].compare(keys[b])};
            if (c == 0) return false; // keep input order (stable) for equal keys
            return m_sortAscending ? (c < 0) : (c > 0);
        });

        std::vector<Row> sorted;
        sorted.reserve(m_rows.size());
        for (size_t oi : order)
            for (size_t k{units[oi].first}; k < units[oi].second; ++k)
                sorted.push_back(std::move(m_rows[k]));
        m_rows = std::move(sorted);

        // Restore focus by identity (unit + segIndex uniquely identify a row).
        m_selected = -1;
        if (hadSel)
        {
            for (size_t i{0}; i < m_rows.size(); ++i)
            {
                const Row& r{m_rows[i]};
                if (r.unit == selUnit && r.segIndex == selSeg) { m_selected = static_cast<int>(i); break; }
            }
        }
        m_selection.clear();
        if (m_selected >= 0) m_selection.insert(m_selected);
        m_hover = -1;
        m_editing = -1;
        RebuildFiltered();
    }

    int Grid::RowAtPoint(const Layout& lay, float x, float y) const
    {
        const float rightEdge = lay.data.right - (lay.hasScrollbar ? m_scrollbarW : 0.0f);
        if (x < lay.data.left || x >= rightEdge || y < lay.data.top || y >= lay.data.bottom)
            return -1;
        const int fi = static_cast<int>((y - lay.data.top + m_scrollY) / m_rowH);
        if (fi < 0 || fi >= FilteredCount()) return -1;
        return FilteredToActual(fi);
    }

    void Grid::ClampScroll()
    {
        const Layout lay{Compute()};
        m_scrollY = std::clamp(m_scrollY, 0.0f, lay.maxScroll);
        m_scrollX = std::clamp(m_scrollX, 0.0f, lay.maxScrollX);
    }

    void Grid::MeasureContentWidth(const GridFonts& fonts)
    {
        m_contentW = 0.0f;
        if (!fonts.factory || !fonts.value) return;
        for (int fi{0}; fi < FilteredCount(); ++fi)
        {
            const int i{FilteredToActual(fi)};
            if (i < 0 || i >= static_cast<int>(m_rows.size())) continue;
            const std::wstring& text{m_rows[static_cast<size_t>(i)].col2};
            if (text.empty()) continue;
            IDWriteTextLayout* layout{nullptr};
            if (SUCCEEDED(fonts.factory->CreateTextLayout(text.c_str(),
                    static_cast<UINT32>(text.size()), fonts.value, 1.0e6f, m_rowH, &layout)) && layout)
            {
                DWRITE_TEXT_METRICS tm{};
                if (SUCCEEDED(layout->GetMetrics(&tm)))
                    m_contentW = std::max(m_contentW, tm.width);
                layout->Release();
            }
        }
    }

    void Grid::ClearAndSelectFocus()
    {
        m_selection.clear();
        if (m_selected >= 0) m_selection.insert(m_selected);
    }

    float Grid::NameColWidth() const
    {
        // Scale the cap with zoom so larger fonts still fit; the proportional bound keeps
        // the name column from eating the whole row on narrow windows.
        return std::min(260.0f * m_zoom, (m_bounds.right - m_bounds.left) * 0.45f);
    }

    D2D1_RECT_F Grid::ValueCellRect(int row) const
    {
        const Layout lay{Compute()};
        const float rightEdge = lay.data.right - (lay.hasScrollbar ? m_scrollbarW : 0.0f);
        const int fi{ActualToFiltered(row)};
        const float top = lay.data.top + static_cast<float>(fi >= 0 ? fi : row) * m_rowH - m_scrollY;
        return D2D1::RectF(lay.data.left + kPad + NameColWidth(), top, rightEdge - kPad, top + m_rowH);
    }

    D2D1_RECT_F Grid::NameCellRect(int row) const
    {
        const Layout lay{Compute()};
        const int fi{ActualToFiltered(row)};
        const float top = lay.data.top + static_cast<float>(fi >= 0 ? fi : row) * m_rowH - m_scrollY;
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
        m_needMeasure = true;
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
        entry.group = m_rows[static_cast<size_t>(m_selected)].group;
        entry.scope = scope;
        entry.varIndex = vi;
        entry.unit = m_rows[static_cast<size_t>(m_selected)].unit; // same variable unit
        m_rows.insert(m_rows.begin() + m_selected + 1, entry);
        m_selected += 1; // select the new (blank) entry so the host can edit it
        ClearAndSelectFocus();

        std::vector<bool>& flags{(scope == Environ::core::Scope::User) ? m_userStruct : m_machineStruct};
        if (vi >= 0 && vi < static_cast<int>(flags.size())) flags[static_cast<size_t>(vi)] = true;
        RebuildFiltered();
        EnsureVisible(m_selected);
        return true;
    }

    bool Grid::RemoveEntry()
    {
        if (!SelectedIsPathEntry()) return false;
        const Environ::core::Scope scope{m_rows[static_cast<size_t>(m_selected)].scope};
        const int vi{m_rows[static_cast<size_t>(m_selected)].varIndex};

        const int unit{m_rows[static_cast<size_t>(m_selected)].unit};
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
            if (next < m_rows.size() && m_rows[next].unit == unit
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
        RebuildFiltered();
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

        // Find insertion point. If we have an editable selection, insert right after the
        // selected variable's unit; otherwise append after the last row of the target group.
        // (Use group, not scope: Process rows carry a dummy scope = User.)
        const RowGroup targetGroup{scope == Environ::core::Scope::User ? RowGroup::User : RowGroup::Machine};
        int insertAt{0};
        if (HasSelection() && m_rows[static_cast<size_t>(m_selected)].group == targetGroup)
        {
            const int unit{m_rows[static_cast<size_t>(m_selected)].unit};
            size_t j{static_cast<size_t>(m_selected)};
            while (j + 1 < m_rows.size() && m_rows[j + 1].unit == unit)
                ++j;
            insertAt = static_cast<int>(j) + 1;
        }
        else
        {
            for (size_t i{0}; i < m_rows.size(); ++i)
            {
                if (m_rows[i].group == targetGroup)
                    insertAt = static_cast<int>(i) + 1;
            }
        }

        Row var{};
        var.kind = Row::Kind::Variable;
        var.depth = 0;
        var.readOnly = false;
        var.group = (scope == Environ::core::Scope::User) ? RowGroup::User : RowGroup::Machine;
        var.scope = scope;
        var.varIndex = -1; // sentinel: new variable
        var.segIndex = -1;
        var.unit = m_nextUnit++; // fresh, unique unit id
        m_rows.insert(m_rows.begin() + insertAt, std::move(var));
        m_selected = insertAt;
        ClearAndSelectFocus();
        RebuildFiltered();
        EnsureVisible(m_selected);
        return true;
    }

    bool Grid::RemoveVariable()
    {
        if (!HasSelection()) return false;
        const Row& sel{m_rows[static_cast<size_t>(m_selected)]};
        if (sel.readOnly) return false;

        const Environ::core::Scope scope{sel.scope};
        const int unit{sel.unit};
        const int vi{sel.varIndex};

        // For an existing variable, flag it for deletion; new (unsaved) variables just vanish.
        if (vi >= 0)
        {
            std::vector<bool>& flags{(scope == Environ::core::Scope::User) ? m_userStruct : m_machineStruct};
            if (vi < static_cast<int>(flags.size()))
                flags[static_cast<size_t>(vi)] = true;
        }

        // Erase ALL rows of the unit (variable row + any segment rows).
        for (auto it{m_rows.begin()}; it != m_rows.end();)
        {
            if (it->unit == unit)
                it = m_rows.erase(it);
            else
                ++it;
        }

        if (m_selected >= static_cast<int>(m_rows.size()))
            m_selected = static_cast<int>(m_rows.size()) - 1;
        ClearAndSelectFocus();
        RebuildFiltered();
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
        if (m_rows[static_cast<size_t>(other)].unit != m_rows[static_cast<size_t>(m_selected)].unit)
            return false; // can't move across variables

        std::swap(m_rows[static_cast<size_t>(m_selected)].col2, m_rows[static_cast<size_t>(other)].col2);
        m_selected = other; // selection follows the moved entry
        ClearAndSelectFocus();

        std::vector<bool>& flags{(scope == Environ::core::Scope::User) ? m_userStruct : m_machineStruct};
        if (vi >= 0 && vi < static_cast<int>(flags.size())) flags[static_cast<size_t>(vi)] = true;
        RebuildFiltered();
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
        const int fc{FilteredCount()};
        int fi{ActualToFiltered(m_selected)};
        if (fi < 0) fi = (dir > 0) ? -1 : fc;
        for (int i{fi + dir}; i >= 0 && i < fc; i += dir)
        {
            const int ai{FilteredToActual(i)};
            if (ai >= 0 && !m_rows[static_cast<size_t>(ai)].readOnly)
            {
                m_selected = ai;
                ClearAndSelectFocus();
                EnsureVisible(ai);
                return true;
            }
        }
        return false;
    }

    void Grid::EnsureVisible(int row)
    {
        const float viewH = m_bounds.bottom - m_bounds.top - m_headerH;
        if (viewH <= 0.0f) return;
        const int fi{ActualToFiltered(row)};
        if (fi < 0) return; // row hidden by filter
        const float top = static_cast<float>(fi) * m_rowH;
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
        if (m_needMeasure) { MeasureContentWidth(fonts); m_needMeasure = false; }
        ClampScroll();
        const Layout lay{Compute()};

        const float nameCol{NameColWidth()};

        // Draws value text within the value column, offset by the horizontal scroll. The
        // clip keeps it from bleeding into the (frozen) name column on the left.
        const auto drawValue = [&](const std::wstring& text, float top, const D2D1_COLOR_F& color) {
            rt->PushAxisAlignedClip(D2D1::RectF(lay.valueLeft, top, lay.valueRight, top + m_rowH),
                                    D2D1_ANTIALIAS_MODE_ALIASED);
            DrawString(rt, brush, text, fonts.value,
                       D2D1::RectF(lay.valueLeft - m_scrollX, top, lay.valueRight, top + m_rowH), color);
            rt->PopAxisAlignedClip();
        };

        // Column header band. The active sort column shows an ascending/descending arrow.
        brush->SetColor(s.header.fill);
        rt->FillRectangle(lay.header, brush);
        const wchar_t arrow{m_sortAscending ? L'\x25B2' : L'\x25BC'}; // ▲ / ▼
        std::wstring nameLabel{L"NAME"};
        std::wstring valueLabel{L"VALUE"};
        if (m_sortColumn == SortColumn::Name) nameLabel += std::wstring{L"  "} + arrow;
        else                                  valueLabel += std::wstring{L"  "} + arrow;
        DrawString(rt, brush, nameLabel, fonts.header,
                 D2D1::RectF(lay.header.left + kPad, lay.header.top, lay.header.left + nameCol, lay.header.bottom),
                 s.header.text);
        DrawString(rt, brush, valueLabel, fonts.header,
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
        int fi{static_cast<int>(m_scrollY / m_rowH)};
        float y{lay.data.top - (m_scrollY - static_cast<float>(fi) * m_rowH)};
        for (; fi < FilteredCount() && y < lay.data.bottom; ++fi, y += m_rowH)
        {
            const int i{FilteredToActual(fi)};
            if (i < 0 || i >= static_cast<int>(m_rows.size())) continue;
            const Row& r{m_rows[static_cast<size_t>(i)]};
            const bool selected = m_selection.contains(i);
            const bool focused = (i == m_selected);
            const bool hovered = (i == m_hover && !selected);
            const std::vector<bool>& sf{(r.scope == Environ::core::Scope::User) ? m_userStruct : m_machineStruct};
            const bool varStruct{r.group != RowGroup::Process && r.varIndex >= 0
                                 && r.varIndex < static_cast<int>(sf.size())
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

            // Check if this segment row is promoted to show a variable name.
            const auto promoted{m_filteredPromoted.find(i)};

            if (r.kind == Row::Kind::Variable || promoted != m_filteredPromoted.end())
            {
                const std::wstring& name{promoted != m_filteredPromoted.end() ? promoted->second : r.col1};

                // Per-row scope icon (survives any sort/filter); takes the row's text color so
                // read-only rows grey out with it.
                const float iconW{18.0f * m_zoom};
                if (fonts.glyph)
                {
                    const wchar_t g{r.group == RowGroup::Process ? kGlyphProcess
                                    : r.group == RowGroup::Machine ? kGlyphMachine
                                                                   : kGlyphUser};
                    DrawString(rt, brush, std::wstring(1, g), fonts.glyph,
                               D2D1::RectF(rowRect.left + kPad, y, rowRect.left + kPad + iconW, y + m_rowH),
                               nameColor);
                }
                DrawString(rt, brush, name, fonts.name,
                         D2D1::RectF(rowRect.left + kPad + iconW, y, rowRect.left + nameCol, y + m_rowH), nameColor);
                drawValue(r.col2, y, valueColor);
            }
            else
            {
                // Path segments align under the VALUE column — the variable's contents
                // laid out across multiple lines, not indented from the name.
                drawValue(r.col2, y, valueColor);
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
        if (lay.hasHScroll)
        {
            D2D1_COLOR_F thumb = s.headerSubtext;
            thumb.a = m_draggingHThumb ? 0.85f : 0.5f;
            brush->SetColor(thumb);
            rt->FillRoundedRectangle(D2D1::RoundedRect(lay.hThumb, 4.0f, 4.0f), brush);
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
        if (m_draggingHThumb && lay.hasHScroll)
        {
            const float thumbW = lay.hThumb.right - lay.hThumb.left;
            const float travel = (lay.valueRight - lay.valueLeft) - thumbW;
            const float newLeft = x - m_hDragGrabOffset - lay.valueLeft;
            m_scrollX = (travel > 0.0f) ? (newLeft / travel) * lay.maxScrollX : 0.0f;
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
                    if (nr.unit != r.unit) break;
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

        // Column-header click: sort by that column; clicking the active column flips order.
        if (Contains(lay.header, x, y))
        {
            const SortColumn col{(x < lay.valueLeft) ? SortColumn::Name : SortColumn::Value};
            if (col == m_sortColumn) m_sortAscending = !m_sortAscending;
            else { m_sortColumn = col; m_sortAscending = true; }
            ApplySort();
            if (m_selected >= 0) EnsureVisible(m_selected);
            return true;
        }

        if (lay.hasScrollbar && Contains(lay.thumb, x, y))
        {
            m_draggingThumb = true;
            m_dragGrabOffset = y - lay.thumb.top;
            return true; // thumb darkens while grabbed
        }
        if (lay.hasHScroll && Contains(lay.hThumb, x, y))
        {
            m_draggingHThumb = true;
            m_hDragGrabOffset = x - lay.hThumb.left;
            return true;
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
            // Range-select from the current focus to the clicked row (inclusive),
            // only including rows visible in the filtered set.
            const int anchor{m_selected >= 0 ? m_selected : 0};
            const int anchorFi{ActualToFiltered(anchor)};
            const int rFi{ActualToFiltered(r)};
            if (anchorFi >= 0 && rFi >= 0)
            {
                const int loFi{std::min(anchorFi, rFi)};
                const int hiFi{std::max(anchorFi, rFi)};
                for (int fi{loFi}; fi <= hiFi; ++fi)
                {
                    const int ai{FilteredToActual(fi)};
                    if (ai >= 0) m_selection.insert(ai);
                }
            }
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
        bool changed{false};
        if (m_draggingThumb)  { m_draggingThumb = false;  changed = true; } // lightens on release
        if (m_draggingHThumb) { m_draggingHThumb = false; changed = true; }
        return changed;
    }

    bool Grid::OnWheel(int delta)
    {
        m_scrollY -= (static_cast<float>(delta) / WHEEL_DELTA) * 3.0f * m_rowH;
        ClampScroll();
        return true;
    }

    bool Grid::OnHWheel(int delta)
    {
        if (Compute().maxScrollX <= 0.0f) return false; // nothing to scroll
        m_scrollX -= (static_cast<float>(delta) / WHEEL_DELTA) * 48.0f;
        ClampScroll();
        return true;
    }

    bool Grid::OnKey(int vk)
    {
        const int fc{FilteredCount()};
        if (fc == 0) return false;
        const float viewH = m_bounds.bottom - m_bounds.top - m_headerH;
        const int page = std::max(1, static_cast<int>(viewH / m_rowH));

        // Convert current selection to filtered index for navigation.
        int fi{ActualToFiltered(m_selected)};

        switch (vk)
        {
        case VK_DOWN:  fi = (fi < 0) ? 0 : std::min(fi + 1, fc - 1); break;
        case VK_UP:    fi = (fi < 0) ? 0 : std::max(fi - 1, 0); break;
        case VK_NEXT:  fi = (fi < 0) ? 0 : std::min(fi + page, fc - 1); break;
        case VK_PRIOR: fi = (fi < 0) ? 0 : std::max(fi - page, 0); break;
        case VK_HOME:  fi = 0; break;
        case VK_END:   fi = fc - 1; break;
        default:       return false;
        }

        m_selected = FilteredToActual(fi);
        ClearAndSelectFocus();
        EnsureVisible(m_selected);
        return true;
    }
}
