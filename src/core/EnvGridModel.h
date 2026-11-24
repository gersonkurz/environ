#pragma once

#include "EnvStore.h"

#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace Environ::core
{

/// Display grouping for grid rows. Process and Inherited are read-only and never persisted.
enum class RowGroup
{
    User,
    Machine,
    Process,
    Inherited
};

/// Returns true if the row group is a read-only process environment extra.
constexpr bool is_process_row(RowGroup g)
{
    return g == RowGroup::Process || g == RowGroup::Inherited;
}

/// Active global sort column.
enum class SortColumn
{
    Name,
    Value
};

/// One flattened display row in the environment grid (a variable row or a path-list segment).
struct GridRow
{
    enum class Kind { Variable, Segment };

    Kind kind{Kind::Variable};
    RowGroup group{RowGroup::User};
    std::wstring col1;           // variable name (Variable rows)
    std::wstring col2;           // scalar value / first segment / segment path
    std::wstring original;       // initial col2, for value-dirty detection
    std::wstring col1_original;  // initial name (Variable rows), for rename-dirty detection
    int depth{0};                // 0 = variable, 1 = segment
    bool read_only{false};       // machine var when unelevated, or process/inherited
    bool invalid{false};         // segment path missing on disk
    bool duplicate{false};       // segment duplicated elsewhere
    Scope scope{Scope::User};    // registry scope (unused for process rows)
    int var_index{-1};           // index into scope's original vector (-1 = unsaved new var)
    int seg_index{-1};           // path-list segment index (-1 for scalar)
    int unit{-1};                // stable display-unit id shared by all rows of one variable
};

/// Detail metadata for the currently selected row (shown in the detail strip).
struct SelectionDetail
{
    std::wstring expanded_path;  // expanded form of segment or scalar value
    std::wstring display_path;   // raw display text (col2)
    std::wstring duplicate_desc; // e.g. "duplicate in Machine:PATH" or empty
    bool valid{true};            // false = path not found
    bool is_segment{true};       // true = path-list entry, false = scalar
};

/// Variable counts per display group (for the status footer).
struct GroupCounts
{
    int user{0};
    int machine{0};
    int session{0};
    int inherited{0};
};

/// Headless state machine for the environment-variable grid.
/// Owns loaded originals, flattened rows, global sort, substring filter with promoted
/// segment names, multi-selection, inline edit state, structural path-list / variable
/// edits, diff reconstruction, snapshot restore staging, and clipboard formatting.
class EnvGridModel
{
public:
    /// Load fresh user, machine, and process-extra variables into the grid.
    void set_data(const std::vector<EnvVariable>& user_vars,
                  const std::vector<EnvVariable>& machine_vars,
                  const std::vector<EnvVariable>& process_vars,
                  bool elevated);

    /// Stage a snapshot restore: current registry vars are kept as originals so
    /// differences against snapshot vars show up as dirty edits ready for Save Review.
    void set_data_for_restore(const std::vector<EnvVariable>& current_user,
                              const std::vector<EnvVariable>& current_machine,
                              const std::vector<EnvVariable>& snapshot_user,
                              const std::vector<EnvVariable>& snapshot_machine,
                              bool elevated);

    // --- Sorting -------------------------------------------------------------

    SortColumn sort_column() const { return m_sort_column; }
    bool sort_ascending() const { return m_sort_ascending; }

    /// Sort by `col`: flips direction if already active, otherwise sorts ascending.
    void toggle_sort(SortColumn col);

    /// Set sort column and direction explicitly.
    void set_sort(SortColumn col, bool ascending);

    // --- Filtering & visible-row projection ----------------------------------

    /// Live-filter visible rows by case-insensitive substring match.
    void set_filter(const std::wstring& text);
    bool has_filter() const { return m_filter_active; }
    const std::wstring& filter_text() const { return m_filter_text; }

    int total_row_count() const { return static_cast<int>(m_rows.size()); }
    int visible_row_count() const;

    int visible_to_actual(int visible_index) const;
    int actual_to_visible(int actual_index) const;

    const GridRow& row(int actual_index) const { return m_rows[static_cast<size_t>(actual_index)]; }
    const GridRow& visible_row(int visible_index) const;

    /// Display name for a row: returns the variable name on Variable rows or on
    /// filter-promoted Segment rows; empty string for normal Segment rows.
    const std::wstring& display_name(int actual_index) const;

    /// True if `actual_index` is a Segment row promoted to show its variable name under filter.
    bool is_promoted_segment(int actual_index) const;

    /// Dirty state queries for painting/styling.
    bool row_value_dirty(int actual_index) const;
    bool row_name_dirty(int actual_index) const;
    bool row_is_dirty(int actual_index) const;

    // --- Selection -----------------------------------------------------------

    bool has_selection() const { return m_selected >= 0 && m_selected < static_cast<int>(m_rows.size()); }
    int selected_row() const { return m_selected; }
    int selected_visible_index() const { return actual_to_visible(m_selected); }
    int selection_count() const { return static_cast<int>(m_selection.size()); }
    const std::set<int>& selection() const { return m_selection; }
    bool is_row_selected(int actual_index) const { return m_selection.contains(actual_index); }

    /// Select a single actual row (clears previous multi-selection).
    void select_single(int actual_index);

    /// Toggle an actual row in/out of the multi-selection set and move focus to it.
    void toggle_select(int actual_index);

    /// Range-select from current focus to `actual_index` across visible rows.
    void range_select(int actual_index);

    /// Replace selection directly from a list of visible indices (e.g. ListView sync).
    void set_visible_selection(const std::vector<int>& visible_indices, int focus_visible_index);

    /// Clear all selection.
    void clear_selection();

    /// Move focus to the next (+1) or previous (-1) editable visible row.
    bool select_next_editable(int dir);

    /// True if the focused row exists and is not read-only.
    bool selection_editable() const;

    /// True if the focused row is a Variable row (has a Name cell).
    bool selection_has_name_cell() const;

    // --- Inline editing ------------------------------------------------------

    /// Begin editing the focused row's Value cell. Returns initial text or nullopt.
    std::optional<std::wstring> begin_edit_value();

    /// Begin editing the focused row's Name cell. Returns initial text or nullopt.
    std::optional<std::wstring> begin_edit_name();

    /// Select `actual_index` and begin editing its Name or Value cell.
    std::optional<std::wstring> begin_edit_at(int actual_index, bool edit_name);

    /// Commit the active inline edit and mark the row dirty.
    void commit_edit(const std::wstring& text);

    /// Cancel the active inline edit without modifying the row.
    void cancel_edit();

    bool is_editing() const { return m_editing >= 0; }
    bool is_editing_name() const { return m_editing >= 0 && m_editing_name; }
    int editing_row() const { return m_editing; }

    /// Write `text` directly into the focused editable row's Value cell (e.g. after Browse).
    bool set_selected_value(const std::wstring& text);

    // --- Structural editing --------------------------------------------------

    /// True if the focused row belongs to an editable PathList variable.
    bool selected_is_path_entry() const;

    /// Insert a blank path-list segment after the focused row and select it.
    bool add_entry();

    /// Remove the focused path-list segment (or promote next segment / clear if first).
    bool remove_entry();

    /// Move the focused path-list segment up (-1) or down (+1) within its variable.
    bool move_entry(int dir);

    /// Infer the target scope for Ins / New Variable based on current selection.
    Scope new_variable_scope() const;

    /// Create a new blank variable in `scope` (or inferred scope if nullopt) and select it.
    bool add_variable(std::optional<Scope> scope = std::nullopt);

    /// Delete the focused variable and all its segment rows.
    bool remove_variable();

    // --- Detail strip, clipboard & save reconstruction -----------------------

    std::optional<SelectionDetail> get_row_detail(int actual_index) const;
    std::wstring row_variable_name(int actual_index) const;
    bool row_shadowed(int actual_index) const;
    std::wstring row_effective_value(int actual_index) const;

    std::optional<SelectionDetail> get_selection_detail() const;
    std::wstring selected_variable_name() const;
    bool selection_shadowed() const;
    std::wstring selection_effective_value() const;
    bool selection_inherited() const;
    std::wstring selected_value_text() const;
    std::wstring copy_text() const;

    bool has_changes() const;
    const std::vector<EnvVariable>& original_vars(Scope scope) const;
    std::vector<EnvVariable> current_vars(Scope scope) const;
    GroupCounts group_counts() const;

private:
    void apply_sort();
    void rebuild_filtered();
    void clear_and_select_focus();
    const std::vector<EnvVariable>& vars_for_row(const GridRow& r) const;

    std::vector<GridRow> m_rows;
    std::vector<EnvVariable> m_user_orig;
    std::vector<EnvVariable> m_machine_orig;
    std::vector<EnvVariable> m_process_vars;
    std::vector<bool> m_user_struct;
    std::vector<bool> m_machine_struct;

    int m_selected{-1};
    std::set<int> m_selection;
    int m_editing{-1};
    bool m_editing_name{false};

    SortColumn m_sort_column{SortColumn::Name};
    bool m_sort_ascending{true};
    int m_next_unit{0};

    std::wstring m_filter_text;
    std::vector<int> m_filtered;
    std::unordered_map<int, std::wstring> m_filtered_promoted;
    bool m_filter_active{false};
};

} // namespace Environ::core
