#include "precomp.h"
#include "EnvGridModel.h"

#include <algorithm>

namespace Environ::core
{

void EnvGridModel::set_data(const std::vector<EnvVariable>& user_vars,
                            const std::vector<EnvVariable>& machine_vars,
                            const std::vector<EnvVariable>& process_vars,
                            bool elevated)
{
    m_user_orig = user_vars;
    m_machine_orig = machine_vars;
    m_process_vars = process_vars;
    m_user_struct.assign(user_vars.size(), false);
    m_machine_struct.assign(machine_vars.size(), false);
    m_rows.clear();
    m_next_unit = 0;
    m_selected = -1;
    m_selection.clear();
    m_editing = -1;
    m_editing_name = false;

    const auto add_group = [&](const std::vector<EnvVariable>& vars, RowGroup group,
                               Scope scope, bool read_only) {
        for (int vi{0}; vi < static_cast<int>(vars.size()); ++vi)
        {
            const EnvVariable& v{vars[static_cast<size_t>(vi)]};
            const int unit{m_next_unit++};
            const RowGroup g{(group == RowGroup::Process && v.inherited) ? RowGroup::Inherited : group};

            GridRow var{};
            var.kind = GridRow::Kind::Variable;
            var.group = g;
            var.col1 = v.name;
            var.col1_original = v.name;
            var.depth = 0;
            var.read_only = read_only;
            var.scope = scope;
            var.var_index = vi;
            var.unit = unit;

            if (v.kind == EnvVariableKind::PathList && !v.segments.empty())
            {
                var.col2 = v.segments[0];
                var.original = var.col2;
                var.seg_index = 0;
                var.invalid = !v.segment_valid.empty() && !v.segment_valid[0];
                var.duplicate = !v.segment_duplicate.empty() && !v.segment_duplicate[0].empty();
                m_rows.push_back(std::move(var));

                for (size_t i{1}; i < v.segments.size(); ++i)
                {
                    GridRow seg{};
                    seg.kind = GridRow::Kind::Segment;
                    seg.group = g;
                    seg.col2 = v.segments[i];
                    seg.original = seg.col2;
                    seg.depth = 1;
                    seg.read_only = read_only;
                    seg.scope = scope;
                    seg.var_index = vi;
                    seg.seg_index = static_cast<int>(i);
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
                var.seg_index = -1;
                m_rows.push_back(std::move(var));
            }
        }
    };

    add_group(user_vars, RowGroup::User, Scope::User, false);
    add_group(machine_vars, RowGroup::Machine, Scope::Machine, !elevated);
    add_group(process_vars, RowGroup::Process, Scope::User, true);
    apply_sort();
}

void EnvGridModel::set_data_for_restore(const std::vector<EnvVariable>& current_user,
                                        const std::vector<EnvVariable>& current_machine,
                                        const std::vector<EnvVariable>& snapshot_user,
                                        const std::vector<EnvVariable>& snapshot_machine,
                                        bool elevated)
{
    m_user_orig = current_user;
    m_machine_orig = current_machine;
    m_rows.clear();
    m_next_unit = 0;
    m_selected = -1;
    m_selection.clear();
    m_editing = -1;
    m_editing_name = false;

    const auto build_map = [](const std::vector<EnvVariable>& vars) {
        std::unordered_map<std::wstring, int> map;
        for (int i{0}; i < static_cast<int>(vars.size()); ++i)
            map[to_wlower(vars[static_cast<size_t>(i)].name)] = i;
        return map;
    };

    auto user_map{build_map(current_user)};
    auto machine_map{build_map(current_machine)};

    std::vector<bool> user_seen(current_user.size(), false);
    std::vector<bool> machine_seen(current_machine.size(), false);

    const auto add_restore_group = [&](const std::vector<EnvVariable>& snap_vars, Scope scope,
                                       bool read_only, const std::vector<EnvVariable>& cur_vars,
                                       std::unordered_map<std::wstring, int>& cur_map,
                                       std::vector<bool>& seen) {
        for (int si{0}; si < static_cast<int>(snap_vars.size()); ++si)
        {
            const EnvVariable& sv{snap_vars[static_cast<size_t>(si)]};
            const auto it{cur_map.find(to_wlower(sv.name))};
            const int orig_idx{it != cur_map.end() ? it->second : -1};
            if (orig_idx >= 0) seen[static_cast<size_t>(orig_idx)] = true;

            const int unit{m_next_unit++};
            GridRow var{};
            var.kind = GridRow::Kind::Variable;
            var.col1 = sv.name;
            var.col1_original = sv.name;
            var.depth = 0;
            var.read_only = read_only;
            var.group = (scope == Scope::User) ? RowGroup::User : RowGroup::Machine;
            var.scope = scope;
            var.var_index = orig_idx;
            var.seg_index = -1;
            var.unit = unit;

            if (sv.kind == EnvVariableKind::PathList && !sv.segments.empty())
            {
                var.col2 = sv.segments[0];
                var.seg_index = 0;

                if (orig_idx >= 0 && cur_vars[static_cast<size_t>(orig_idx)].kind == EnvVariableKind::PathList
                    && !cur_vars[static_cast<size_t>(orig_idx)].segments.empty())
                    var.original = cur_vars[static_cast<size_t>(orig_idx)].segments[0];
                else
                    var.original = L"";

                var.invalid = !sv.segment_valid.empty() && !sv.segment_valid[0];
                var.duplicate = !sv.segment_duplicate.empty() && !sv.segment_duplicate[0].empty();
                m_rows.push_back(std::move(var));

                for (size_t i{1}; i < sv.segments.size(); ++i)
                {
                    GridRow seg{};
                    seg.kind = GridRow::Kind::Segment;
                    seg.col2 = sv.segments[i];
                    seg.depth = 1;
                    seg.read_only = read_only;
                    seg.group = (scope == Scope::User) ? RowGroup::User : RowGroup::Machine;
                    seg.scope = scope;
                    seg.var_index = orig_idx;
                    seg.seg_index = static_cast<int>(i);
                    seg.unit = unit;

                    if (orig_idx >= 0 && cur_vars[static_cast<size_t>(orig_idx)].kind == EnvVariableKind::PathList
                        && i < cur_vars[static_cast<size_t>(orig_idx)].segments.size())
                        seg.original = cur_vars[static_cast<size_t>(orig_idx)].segments[i];
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
                var.original = (orig_idx >= 0) ? cur_vars[static_cast<size_t>(orig_idx)].value : L"";
                m_rows.push_back(std::move(var));
            }
        }
    };

    add_restore_group(snapshot_user, Scope::User, false, current_user, user_map, user_seen);
    add_restore_group(snapshot_machine, Scope::Machine, !elevated, current_machine, machine_map, machine_seen);

    m_user_struct.assign(current_user.size(), false);
    m_machine_struct.assign(current_machine.size(), false);

    for (size_t i{0}; i < user_seen.size(); ++i)
        if (!user_seen[i]) m_user_struct[i] = true;
    for (size_t i{0}; i < machine_seen.size(); ++i)
        if (!machine_seen[i]) m_machine_struct[i] = true;

    for (const auto& r : m_rows)
    {
        if (r.kind != GridRow::Kind::Variable || r.var_index < 0) continue;
        const auto& cur_vars{(r.scope == Scope::User) ? current_user : current_machine};
        auto& flags{(r.scope == Scope::User) ? m_user_struct : m_machine_struct};
        if (r.var_index >= static_cast<int>(cur_vars.size())) continue;
        const auto& cv{cur_vars[static_cast<size_t>(r.var_index)]};
        const auto& snap_vars{(r.scope == Scope::User) ? snapshot_user : snapshot_machine};

        for (const auto& sv : snap_vars)
        {
            if (_wcsicmp(sv.name.c_str(), cv.name.c_str()) == 0)
            {
                if (sv.kind != cv.kind
                    || (sv.kind == EnvVariableKind::PathList && sv.segments.size() != cv.segments.size()))
                    flags[static_cast<size_t>(r.var_index)] = true;
                break;
            }
        }
    }

    apply_sort();
}

void EnvGridModel::toggle_sort(SortColumn col)
{
    if (col == m_sort_column)
        m_sort_ascending = !m_sort_ascending;
    else
    {
        m_sort_column = col;
        m_sort_ascending = true;
    }
    apply_sort();
}

void EnvGridModel::set_sort(SortColumn col, bool ascending)
{
    m_sort_column = col;
    m_sort_ascending = ascending;
    apply_sort();
}

void EnvGridModel::apply_sort()
{
    if (m_rows.empty())
    {
        rebuild_filtered();
        return;
    }

    const bool had_sel{m_selected >= 0 && m_selected < static_cast<int>(m_rows.size())};
    int sel_unit{-1};
    int sel_seg{-1};
    if (had_sel)
    {
        const GridRow& r{m_rows[static_cast<size_t>(m_selected)]};
        sel_unit = r.unit;
        sel_seg = r.seg_index;
    }

    std::vector<std::pair<size_t, size_t>> units;
    for (size_t i{0}; i < m_rows.size();)
    {
        size_t j{i + 1};
        while (j < m_rows.size() && m_rows[j].unit == m_rows[i].unit)
            ++j;
        units.push_back({i, j});
        i = j;
    }

    std::vector<std::wstring> keys(units.size());
    for (size_t u{0}; u < units.size(); ++u)
    {
        const GridRow& v{m_rows[units[u].first]};
        keys[u] = to_wlower(m_sort_column == SortColumn::Name ? v.col1 : v.col2);
    }

    std::vector<size_t> order(units.size());
    for (size_t i{0}; i < order.size(); ++i) order[i] = i;

    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        const int c{keys[a].compare(keys[b])};
        if (c == 0) return false;
        return m_sort_ascending ? (c < 0) : (c > 0);
    });

    std::vector<GridRow> sorted;
    sorted.reserve(m_rows.size());
    for (size_t oi : order)
        for (size_t k{units[oi].first}; k < units[oi].second; ++k)
            sorted.push_back(std::move(m_rows[k]));
    m_rows = std::move(sorted);

    m_selected = -1;
    if (had_sel)
    {
        for (size_t i{0}; i < m_rows.size(); ++i)
        {
            if (m_rows[i].unit == sel_unit && m_rows[i].seg_index == sel_seg)
            {
                m_selected = static_cast<int>(i);
                break;
            }
        }
    }
    clear_and_select_focus();
    m_editing = -1;
    rebuild_filtered();
}

void EnvGridModel::set_filter(const std::wstring& text)
{
    m_filter_text = to_wlower(text);
    rebuild_filtered();
}

void EnvGridModel::rebuild_filtered()
{
    if (m_filter_text.empty())
    {
        m_filtered.clear();
        m_filtered_promoted.clear();
        m_filter_active = false;
        return;
    }

    m_filtered.clear();
    m_filtered_promoted.clear();
    m_filter_active = true;

    const auto matches_filter = [this](const std::wstring& s) {
        if (s.empty()) return false;
        return to_wlower(s).find(m_filter_text) != std::wstring::npos;
    };

    for (size_t i{0}; i < m_rows.size();)
    {
        const int unit{m_rows[i].unit};
        const int vi{m_rows[i].var_index};

        const size_t group_start{i};
        size_t group_end{i};
        while (group_end < m_rows.size() && m_rows[group_end].unit == unit)
            ++group_end;

        if (vi == -1)
        {
            for (size_t j{group_start}; j < group_end; ++j)
                m_filtered.push_back(static_cast<int>(j));
            i = group_end;
            continue;
        }

        bool name_match{false};
        for (size_t j{group_start}; j < group_end && !name_match; ++j)
        {
            if (!m_rows[j].col1.empty() && matches_filter(m_rows[j].col1))
                name_match = true;
        }

        if (name_match)
        {
            for (size_t j{group_start}; j < group_end; ++j)
                m_filtered.push_back(static_cast<int>(j));
        }
        else
        {
            const size_t var_row{group_start};
            if (group_end - group_start == 1)
            {
                if (matches_filter(m_rows[var_row].col2))
                    m_filtered.push_back(static_cast<int>(var_row));
            }
            else
            {
                std::vector<int> matching_rows;
                for (size_t j{group_start}; j < group_end; ++j)
                {
                    if (matches_filter(m_rows[j].col2))
                        matching_rows.push_back(static_cast<int>(j));
                }
                if (!matching_rows.empty())
                {
                    const int var_idx{static_cast<int>(var_row)};
                    if (matching_rows[0] != var_idx)
                        m_filtered_promoted[matching_rows[0]] = m_rows[var_row].col1;
                    for (int idx : matching_rows)
                        m_filtered.push_back(idx);
                }
            }
        }
        i = group_end;
    }

    if (m_selected >= 0 && actual_to_visible(m_selected) < 0)
        m_selected = m_filtered.empty() ? -1 : m_filtered[0];

    std::set<int> new_sel;
    for (int idx : m_selection)
    {
        if (actual_to_visible(idx) >= 0) new_sel.insert(idx);
    }
    m_selection = std::move(new_sel);
    if (m_selected >= 0 && !m_selection.contains(m_selected))
        m_selection.insert(m_selected);
}

int EnvGridModel::visible_row_count() const
{
    return m_filter_active ? static_cast<int>(m_filtered.size()) : static_cast<int>(m_rows.size());
}

int EnvGridModel::visible_to_actual(int visible_index) const
{
    if (!m_filter_active)
        return (visible_index >= 0 && visible_index < static_cast<int>(m_rows.size())) ? visible_index : -1;
    if (visible_index < 0 || visible_index >= static_cast<int>(m_filtered.size()))
        return -1;
    return m_filtered[static_cast<size_t>(visible_index)];
}

int EnvGridModel::actual_to_visible(int actual_index) const
{
    if (!m_filter_active)
        return (actual_index >= 0 && actual_index < static_cast<int>(m_rows.size())) ? actual_index : -1;
    for (int i{0}; i < static_cast<int>(m_filtered.size()); ++i)
    {
        if (m_filtered[static_cast<size_t>(i)] == actual_index)
            return i;
    }
    return -1;
}

const GridRow& EnvGridModel::visible_row(int visible_index) const
{
    return m_rows[static_cast<size_t>(visible_to_actual(visible_index))];
}

const std::wstring& EnvGridModel::display_name(int actual_index) const
{
    static const std::wstring kEmpty;
    if (actual_index < 0 || actual_index >= static_cast<int>(m_rows.size()))
        return kEmpty;
    const auto it{m_filtered_promoted.find(actual_index)};
    if (it != m_filtered_promoted.end())
        return it->second;
    const GridRow& r{m_rows[static_cast<size_t>(actual_index)]};
    return (r.kind == GridRow::Kind::Variable) ? r.col1 : kEmpty;
}

bool EnvGridModel::is_promoted_segment(int actual_index) const
{
    return m_filtered_promoted.contains(actual_index);
}

bool EnvGridModel::row_value_dirty(int actual_index) const
{
    if (actual_index < 0 || actual_index >= static_cast<int>(m_rows.size())) return false;
    const GridRow& r{m_rows[static_cast<size_t>(actual_index)]};
    if (r.var_index == -1) return true;
    const std::vector<bool>& sf{(r.scope == Scope::User) ? m_user_struct : m_machine_struct};
    const bool var_struct{!is_process_row(r.group) && r.var_index >= 0
                          && r.var_index < static_cast<int>(sf.size())
                          && sf[static_cast<size_t>(r.var_index)]};
    return (r.col2 != r.original) || var_struct;
}

bool EnvGridModel::row_name_dirty(int actual_index) const
{
    if (actual_index < 0 || actual_index >= static_cast<int>(m_rows.size())) return false;
    const GridRow& r{m_rows[static_cast<size_t>(actual_index)]};
    return r.kind == GridRow::Kind::Variable && (r.var_index == -1 || r.col1 != r.col1_original);
}

bool EnvGridModel::row_is_dirty(int actual_index) const
{
    return row_value_dirty(actual_index) || row_name_dirty(actual_index);
}

void EnvGridModel::clear_and_select_focus()
{
    m_selection.clear();
    if (m_selected >= 0) m_selection.insert(m_selected);
}

void EnvGridModel::select_single(int actual_index)
{
    if (actual_index < 0 || actual_index >= static_cast<int>(m_rows.size()))
    {
        clear_selection();
        return;
    }
    m_selected = actual_index;
    clear_and_select_focus();
}

void EnvGridModel::toggle_select(int actual_index)
{
    if (actual_index < 0 || actual_index >= static_cast<int>(m_rows.size())) return;
    if (m_selection.contains(actual_index))
        m_selection.erase(actual_index);
    else
        m_selection.insert(actual_index);
    m_selected = actual_index;
}

void EnvGridModel::range_select(int actual_index)
{
    if (actual_index < 0 || actual_index >= static_cast<int>(m_rows.size())) return;
    const int anchor{m_selected >= 0 ? m_selected : visible_to_actual(0)};
    const int anchor_fi{actual_to_visible(anchor)};
    const int target_fi{actual_to_visible(actual_index)};
    if (anchor_fi >= 0 && target_fi >= 0)
    {
        const int lo{std::min(anchor_fi, target_fi)};
        const int hi{std::max(anchor_fi, target_fi)};
        for (int fi{lo}; fi <= hi; ++fi)
        {
            const int ai{visible_to_actual(fi)};
            if (ai >= 0) m_selection.insert(ai);
        }
    }
    m_selected = actual_index;
}

void EnvGridModel::set_visible_selection(const std::vector<int>& visible_indices, int focus_visible_index)
{
    m_selection.clear();
    for (int fi : visible_indices)
    {
        const int ai{visible_to_actual(fi)};
        if (ai >= 0) m_selection.insert(ai);
    }
    const int focus_ai{visible_to_actual(focus_visible_index)};
    if (focus_ai >= 0)
    {
        m_selected = focus_ai;
        m_selection.insert(focus_ai);
    }
    else if (!m_selection.empty())
    {
        m_selected = *m_selection.begin();
    }
    else
    {
        m_selected = -1;
    }
}

void EnvGridModel::clear_selection()
{
    m_selected = -1;
    m_selection.clear();
}

bool EnvGridModel::select_next_editable(int dir)
{
    const int fc{visible_row_count()};
    int fi{actual_to_visible(m_selected)};
    if (fi < 0) fi = (dir > 0) ? -1 : fc;
    for (int i{fi + dir}; i >= 0 && i < fc; i += dir)
    {
        const int ai{visible_to_actual(i)};
        if (ai >= 0 && !m_rows[static_cast<size_t>(ai)].read_only)
        {
            m_selected = ai;
            clear_and_select_focus();
            return true;
        }
    }
    return false;
}

bool EnvGridModel::selection_editable() const
{
    return has_selection() && !m_rows[static_cast<size_t>(m_selected)].read_only;
}

bool EnvGridModel::selection_has_name_cell() const
{
    return has_selection() && m_rows[static_cast<size_t>(m_selected)].kind == GridRow::Kind::Variable;
}

std::optional<std::wstring> EnvGridModel::begin_edit_value()
{
    if (!selection_editable()) return std::nullopt;
    m_editing = m_selected;
    m_editing_name = false;
    return m_rows[static_cast<size_t>(m_selected)].col2;
}

std::optional<std::wstring> EnvGridModel::begin_edit_name()
{
    if (!selection_editable() || !selection_has_name_cell()) return std::nullopt;
    m_editing = m_selected;
    m_editing_name = true;
    return m_rows[static_cast<size_t>(m_selected)].col1;
}

std::optional<std::wstring> EnvGridModel::begin_edit_at(int actual_index, bool edit_name)
{
    if (actual_index < 0 || actual_index >= static_cast<int>(m_rows.size())) return std::nullopt;
    m_selected = actual_index;
    clear_and_select_focus();
    if (edit_name && m_rows[static_cast<size_t>(actual_index)].kind == GridRow::Kind::Variable)
        return begin_edit_name();
    return begin_edit_value();
}

void EnvGridModel::commit_edit(const std::wstring& text)
{
    if (m_editing < 0 || m_editing >= static_cast<int>(m_rows.size()))
    {
        m_editing = -1;
        return;
    }
    GridRow& r{m_rows[static_cast<size_t>(m_editing)]};
    if (m_editing_name)
    {
        r.col1 = text;
    }
    else
    {
        r.col2 = text;
        r.invalid = false;
        r.duplicate = false;
    }
    m_editing = -1;
}

void EnvGridModel::cancel_edit()
{
    m_editing = -1;
}

bool EnvGridModel::set_selected_value(const std::wstring& text)
{
    if (!selection_editable()) return false;
    GridRow& r{m_rows[static_cast<size_t>(m_selected)]};
    r.col2 = text;
    r.invalid = false;
    r.duplicate = false;
    return true;
}

bool EnvGridModel::selected_is_path_entry() const
{
    if (!has_selection()) return false;
    const GridRow& r{m_rows[static_cast<size_t>(m_selected)]};
    if (r.read_only) return false;
    const auto& orig{(r.scope == Scope::User) ? m_user_orig : m_machine_orig};
    return r.var_index >= 0 && r.var_index < static_cast<int>(orig.size())
        && orig[static_cast<size_t>(r.var_index)].kind == EnvVariableKind::PathList;
}

bool EnvGridModel::add_entry()
{
    if (!selected_is_path_entry()) return false;
    const Scope scope{m_rows[static_cast<size_t>(m_selected)].scope};
    const int vi{m_rows[static_cast<size_t>(m_selected)].var_index};

    GridRow entry{};
    entry.kind = GridRow::Kind::Segment;
    entry.depth = 1;
    entry.group = m_rows[static_cast<size_t>(m_selected)].group;
    entry.scope = scope;
    entry.var_index = vi;
    entry.unit = m_rows[static_cast<size_t>(m_selected)].unit;

    m_rows.insert(m_rows.begin() + m_selected + 1, std::move(entry));
    m_selected += 1;
    clear_and_select_focus();

    auto& flags{(scope == Scope::User) ? m_user_struct : m_machine_struct};
    if (vi >= 0 && vi < static_cast<int>(flags.size())) flags[static_cast<size_t>(vi)] = true;
    rebuild_filtered();
    return true;
}

bool EnvGridModel::remove_entry()
{
    if (!selected_is_path_entry()) return false;
    const Scope scope{m_rows[static_cast<size_t>(m_selected)].scope};
    const int vi{m_rows[static_cast<size_t>(m_selected)].var_index};
    const int unit{m_rows[static_cast<size_t>(m_selected)].unit};

    if (m_rows[static_cast<size_t>(m_selected)].kind == GridRow::Kind::Segment)
    {
        m_rows.erase(m_rows.begin() + m_selected);
        if (m_selected >= static_cast<int>(m_rows.size()))
            m_selected = static_cast<int>(m_rows.size()) - 1;
    }
    else
    {
        const size_t next{static_cast<size_t>(m_selected) + 1};
        if (next < m_rows.size() && m_rows[next].unit == unit
            && m_rows[next].kind == GridRow::Kind::Segment)
        {
            m_rows[static_cast<size_t>(m_selected)].col2 = m_rows[next].col2;
            m_rows.erase(m_rows.begin() + static_cast<long long>(next));
        }
        else
        {
            m_rows[static_cast<size_t>(m_selected)].col2.clear();
        }
    }

    clear_and_select_focus();
    auto& flags{(scope == Scope::User) ? m_user_struct : m_machine_struct};
    if (vi >= 0 && vi < static_cast<int>(flags.size())) flags[static_cast<size_t>(vi)] = true;
    rebuild_filtered();
    return true;
}

bool EnvGridModel::move_entry(int dir)
{
    if (!selected_is_path_entry()) return false;
    const int other{m_selected + dir};
    if (other < 0 || other >= static_cast<int>(m_rows.size())) return false;
    const Scope scope{m_rows[static_cast<size_t>(m_selected)].scope};
    const int vi{m_rows[static_cast<size_t>(m_selected)].var_index};
    if (m_rows[static_cast<size_t>(other)].unit != m_rows[static_cast<size_t>(m_selected)].unit)
        return false;

    std::swap(m_rows[static_cast<size_t>(m_selected)].col2, m_rows[static_cast<size_t>(other)].col2);
    m_selected = other;
    clear_and_select_focus();

    auto& flags{(scope == Scope::User) ? m_user_struct : m_machine_struct};
    if (vi >= 0 && vi < static_cast<int>(flags.size())) flags[static_cast<size_t>(vi)] = true;
    rebuild_filtered();
    return true;
}

Scope EnvGridModel::new_variable_scope() const
{
    if (has_selection() && !m_rows[static_cast<size_t>(m_selected)].read_only)
        return m_rows[static_cast<size_t>(m_selected)].scope;
    return Scope::User;
}

bool EnvGridModel::add_variable(std::optional<Scope> requested)
{
    const Scope scope{requested.value_or(new_variable_scope())};
    const RowGroup target_group{scope == Scope::User ? RowGroup::User : RowGroup::Machine};

    int insert_at{0};
    if (has_selection() && m_rows[static_cast<size_t>(m_selected)].group == target_group)
    {
        const int unit{m_rows[static_cast<size_t>(m_selected)].unit};
        size_t j{static_cast<size_t>(m_selected)};
        while (j + 1 < m_rows.size() && m_rows[j + 1].unit == unit)
            ++j;
        insert_at = static_cast<int>(j) + 1;
    }
    else
    {
        for (size_t i{0}; i < m_rows.size(); ++i)
        {
            if (m_rows[i].group == target_group)
                insert_at = static_cast<int>(i) + 1;
        }
    }

    GridRow var{};
    var.kind = GridRow::Kind::Variable;
    var.depth = 0;
    var.read_only = false;
    var.group = target_group;
    var.scope = scope;
    var.var_index = -1;
    var.seg_index = -1;
    var.unit = m_next_unit++;
    m_rows.insert(m_rows.begin() + insert_at, std::move(var));
    m_selected = insert_at;
    clear_and_select_focus();
    rebuild_filtered();
    return true;
}

bool EnvGridModel::remove_variable()
{
    if (!has_selection()) return false;
    const GridRow& sel{m_rows[static_cast<size_t>(m_selected)]};
    if (sel.read_only) return false;

    const Scope scope{sel.scope};
    const int unit{sel.unit};
    const int vi{sel.var_index};

    if (vi >= 0)
    {
        auto& flags{(scope == Scope::User) ? m_user_struct : m_machine_struct};
        if (vi < static_cast<int>(flags.size()))
            flags[static_cast<size_t>(vi)] = true;
    }

    for (auto it{m_rows.begin()}; it != m_rows.end();)
    {
        if (it->unit == unit)
            it = m_rows.erase(it);
        else
            ++it;
    }

    if (m_selected >= static_cast<int>(m_rows.size()))
        m_selected = static_cast<int>(m_rows.size()) - 1;
    clear_and_select_focus();
    rebuild_filtered();
    return true;
}

const std::vector<EnvVariable>& EnvGridModel::vars_for_row(const GridRow& r) const
{
    switch (r.group)
    {
    case RowGroup::Machine:   return m_machine_orig;
    case RowGroup::Process:
    case RowGroup::Inherited: return m_process_vars;
    case RowGroup::User:      break;
    }
    return m_user_orig;
}

std::optional<SelectionDetail> EnvGridModel::get_row_detail(int actual_index) const
{
    if (actual_index < 0 || actual_index >= static_cast<int>(m_rows.size())) return std::nullopt;
    const GridRow& r{m_rows[static_cast<size_t>(actual_index)]};
    if (r.col2 != r.original) return std::nullopt;

    const auto& orig{vars_for_row(r)};
    if (r.var_index < 0 || r.var_index >= static_cast<int>(orig.size()))
        return std::nullopt;

    const EnvVariable& v{orig[static_cast<size_t>(r.var_index)]};
    SelectionDetail d{};
    d.display_path = r.col2;

    if (r.seg_index >= 0)
    {
        const auto si{static_cast<size_t>(r.seg_index)};
        d.is_segment = true;
        d.expanded_path = (si < v.expanded_segments.size()) ? v.expanded_segments[si] : r.col2;
        d.valid = (si < v.segment_valid.size()) ? v.segment_valid[si] : true;
        d.duplicate_desc = (si < v.segment_duplicate.size()) ? v.segment_duplicate[si] : std::wstring{};
    }
    else
    {
        d.is_segment = false;
        d.expanded_path = v.expanded_value;
        d.valid = true;
    }
    return d;
}

std::wstring EnvGridModel::row_variable_name(int actual_index) const
{
    if (actual_index < 0 || actual_index >= static_cast<int>(m_rows.size())) return {};
    const GridRow& r{m_rows[static_cast<size_t>(actual_index)]};
    if (r.var_index == -1)
    {
        for (const auto& candidate : m_rows)
        {
            if (candidate.unit == r.unit && candidate.kind == GridRow::Kind::Variable)
                return candidate.col1;
        }
        return {};
    }
    const auto& orig{vars_for_row(r)};
    if (r.var_index < 0 || r.var_index >= static_cast<int>(orig.size()))
        return {};
    return orig[static_cast<size_t>(r.var_index)].name;
}

bool EnvGridModel::row_shadowed(int actual_index) const
{
    if (actual_index < 0 || actual_index >= static_cast<int>(m_rows.size())) return false;
    const GridRow& r{m_rows[static_cast<size_t>(actual_index)]};
    const auto& orig{vars_for_row(r)};
    return r.var_index >= 0 && r.var_index < static_cast<int>(orig.size())
        && orig[static_cast<size_t>(r.var_index)].shadowed;
}

std::wstring EnvGridModel::row_effective_value(int actual_index) const
{
    if (actual_index < 0 || actual_index >= static_cast<int>(m_rows.size())) return {};
    const GridRow& r{m_rows[static_cast<size_t>(actual_index)]};
    const auto& orig{vars_for_row(r)};
    if (r.var_index < 0 || r.var_index >= static_cast<int>(orig.size())) return {};
    return orig[static_cast<size_t>(r.var_index)].effective_value;
}

std::optional<SelectionDetail> EnvGridModel::get_selection_detail() const
{
    return get_row_detail(m_selected);
}

std::wstring EnvGridModel::selected_variable_name() const
{
    return row_variable_name(m_selected);
}

bool EnvGridModel::selection_shadowed() const
{
    return row_shadowed(m_selected);
}

std::wstring EnvGridModel::selection_effective_value() const
{
    return row_effective_value(m_selected);
}

bool EnvGridModel::selection_inherited() const
{
    return has_selection() && m_rows[static_cast<size_t>(m_selected)].group == RowGroup::Inherited;
}

std::wstring EnvGridModel::selected_value_text() const
{
    if (!has_selection()) return {};
    return m_rows[static_cast<size_t>(m_selected)].col2;
}

std::wstring EnvGridModel::copy_text() const
{
    if (m_selection.empty()) return {};

    std::vector<int> sel(m_selection.begin(), m_selection.end());
    std::wstring result;
    size_t pos{0};
    while (pos < sel.size())
    {
        const int idx{sel[pos]};
        if (idx < 0 || idx >= static_cast<int>(m_rows.size()))
        {
            ++pos;
            continue;
        }
        const GridRow& r{m_rows[static_cast<size_t>(idx)]};

        if (r.kind == GridRow::Kind::Variable)
        {
            std::vector<std::wstring> entries;
            entries.push_back(r.col2);
            size_t j{pos + 1};
            while (j < sel.size())
            {
                const int ni{sel[j]};
                if (ni < 0 || ni >= static_cast<int>(m_rows.size())) break;
                const GridRow& nr{m_rows[static_cast<size_t>(ni)]};
                if (nr.unit != r.unit) break;
                entries.push_back(nr.col2);
                ++j;
            }
            if (!result.empty()) result += L"\r\n";
            if (r.seg_index < 0)
                result += r.col1 + L"=" + (entries.empty() ? std::wstring{} : entries.front());
            else
                result += r.col1 + L"=" + join_segments(entries);
            pos = j;
        }
        else
        {
            if (!result.empty()) result += L"\r\n";
            result += r.col2;
            ++pos;
        }
    }
    return result;
}

bool EnvGridModel::has_changes() const
{
    for (bool f : m_user_struct)
        if (f) return true;
    for (bool f : m_machine_struct)
        if (f) return true;
    for (const GridRow& r : m_rows)
    {
        if (r.var_index == -1) return true;
        if (r.col2 != r.original || (r.kind == GridRow::Kind::Variable && r.col1 != r.col1_original))
            return true;
    }
    return false;
}

const std::vector<EnvVariable>& EnvGridModel::original_vars(Scope scope) const
{
    return (scope == Scope::User) ? m_user_orig : m_machine_orig;
}

std::vector<EnvVariable> EnvGridModel::current_vars(Scope scope) const
{
    std::vector<EnvVariable> result{(scope == Scope::User) ? m_user_orig : m_machine_orig};
    const std::vector<bool>& struct_edited{(scope == Scope::User) ? m_user_struct : m_machine_struct};

    std::vector<bool> seen(result.size(), false);
    std::vector<EnvVariable> new_vars;

    for (size_t i{0}; i < m_rows.size();)
    {
        if (is_process_row(m_rows[i].group))
        {
            ++i;
            continue;
        }
        if (m_rows[i].scope != scope)
        {
            ++i;
            continue;
        }

        const int unit{m_rows[i].unit};
        const int vi{m_rows[i].var_index};

        std::wstring name;
        std::wstring name_original;
        std::vector<std::wstring> entries;
        size_t j{i};
        for (; j < m_rows.size() && m_rows[j].unit == unit; ++j)
        {
            if (m_rows[j].kind == GridRow::Kind::Variable)
            {
                name = m_rows[j].col1;
                name_original = m_rows[j].col1_original;
            }
            entries.push_back(m_rows[j].col2);
        }
        i = j;

        if (vi == -1)
        {
            EnvVariable nv{};
            nv.name = name;
            if (entries.size() > 1)
            {
                nv.segments = entries;
                nv.value = join_segments(entries);
                nv.kind = EnvVariableKind::PathList;
            }
            else
            {
                nv.value = entries.empty() ? std::wstring{} : entries.front();
                nv.kind = EnvVariableKind::Scalar;
            }
            nv.is_expandable = false;
            new_vars.push_back(std::move(nv));
            continue;
        }

        if (vi < 0 || vi >= static_cast<int>(result.size())) continue;
        seen[static_cast<size_t>(vi)] = true;
        EnvVariable& v{result[static_cast<size_t>(vi)]};

        if (name != name_original)
        {
            v.original_name = name_original;
            v.name = name;
        }

        if (v.kind != EnvVariableKind::PathList)
        {
            v.value = entries.empty() ? std::wstring{} : entries.front();
            continue;
        }

        v.segments = entries;
        if (struct_edited[static_cast<size_t>(vi)])
            v.value = join_segments(entries);
        else
            v.value = apply_segment_edits(v.value, entries);
    }

    for (size_t k{result.size()}; k > 0; --k)
    {
        const size_t idx{k - 1};
        if (!seen[idx] && idx < struct_edited.size() && struct_edited[idx])
            result.erase(result.begin() + static_cast<long long>(idx));
    }

    for (auto& nv : new_vars)
        result.push_back(std::move(nv));

    return result;
}

GroupCounts EnvGridModel::group_counts() const
{
    GroupCounts c{};
    for (const auto& r : m_rows)
    {
        if (r.kind != GridRow::Kind::Variable) continue;
        switch (r.group)
        {
        case RowGroup::User:      ++c.user; break;
        case RowGroup::Machine:   ++c.machine; break;
        case RowGroup::Process:   ++c.session; break;
        case RowGroup::Inherited: ++c.inherited; break;
        }
    }
    return c;
}

} // namespace Environ::core
