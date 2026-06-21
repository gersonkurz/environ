// Virtualized, theme-driven grid for the environ host (Phase 2: read + inline editing).
// Builds a flat display-row list from core EnvVariables (scalars, and path-lists
// expanded into per-segment child rows), paints only the visible rows, and tracks the
// edit target + dirty state. Not an HWND — the host window forwards paint/input here and
// owns the EDIT control.
#pragma once

#include <d2d1.h>
#include <dwrite.h>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "theme.h"
#include "EnvStore.h"

namespace ui
{
    struct GridFonts
    {
        IDWriteTextFormat* name;   // variable name (semibold)
        IDWriteTextFormat* value;  // value / segment path (regular)
        IDWriteTextFormat* header; // column header
    };

    class Grid
    {
    public:
        void SetData(const std::vector<Environ::core::EnvVariable>& userVars,
                     const std::vector<Environ::core::EnvVariable>& machineVars,
                     bool elevated);

        // Paint within `bounds` (DIPs). `brush` is a reusable solid color brush.
        void Paint(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush,
                   const GridFonts& fonts, const theme::ColorScheme& scheme,
                   const D2D1_RECT_F& bounds);

        // Input — coordinates in DIPs, relative to the window client.
        // Each returns true when a repaint is needed.
        bool OnMouseMove(float x, float y);
        bool OnMouseLeave();
        bool OnLButtonDown(float x, float y, bool shift, bool ctrl);
        bool OnLButtonUp();
        bool OnWheel(int delta);
        bool OnKey(int vk);

        // Inline editing — the host owns the EDIT control; the grid owns the target cell
        // and the in-memory model. Coordinates are DIPs.
        struct EditTarget
        {
            D2D1_RECT_F cell;
            std::wstring text;
            bool isName{false}; // editing the name cell (host uses the heavier name font)
        };
        bool SelectionEditable() const;
        std::optional<EditTarget> BeginEdit();             // edit the current selection
        std::optional<EditTarget> BeginEditName();         // edit the name cell (after AddVariable)
        std::optional<EditTarget> BeginEditAt(float x, float y); // select + edit the row under a point
        bool SelectNextEditable(int dir);                  // Tab: move to next/prev editable row
        void CommitEdit(const std::wstring& text);         // write back + mark dirty
        void CancelEdit();

        // Structural editing of a path-list (keyboard-driven). Each returns true if the grid
        // changed (host should repaint). AddEntry selects the new blank row so the host can
        // open its editor.
        bool SelectedIsPathEntry() const; // selection is an editable path-list entry
        bool AddEntry();                   // insert a blank entry after the selection
        bool RemoveEntry();                // remove the selected entry
        bool MoveEntry(int dir);           // move the entry up (-1) / down (+1) within its variable

        // Variable-level structural editing: create a new blank variable or delete
        // the selected variable entirely (all its rows). Returns true if changed.
        bool AddVariable();
        bool RemoveVariable();

        // Detail strip: info about the currently selected row for display below the grid.
        struct SelectionDetail
        {
            std::wstring expandedPath;   // expanded form of segment or scalar value
            std::wstring displayPath;    // raw display text (col2)
            std::wstring duplicateDesc;  // "duplicate in Machine:PATH" or empty
            bool valid{true};            // false = path not found
            bool isSegment{true};        // true = path-list entry, false = scalar
        };
        std::optional<SelectionDetail> GetSelectionDetail() const;

        // Name of the variable owning the current selection (empty if none). Used to look
        // up a knowledge-base description for the detail strip.
        std::wstring SelectedVariableName() const;

        // Selected cell's current value text (col2), empty if no selection. Used to seed
        // the browse dialog.
        std::wstring SelectedValueText() const;

        // Write `text` into the selected (editable) cell's value, like a committed edit
        // but without an edit session. Returns false if the selection isn't editable.
        bool SetSelectedValue(const std::wstring& text);

        // Value-cell rect of the selection if it is currently on-screen, for placing an
        // in-cell affordance (browse button). nullopt if no selection or scrolled away.
        std::optional<D2D1_RECT_F> SelectedValueCellRect() const;

        // Right-click: select the row under the cursor (like OnLButtonDown but without
        // scrollbar-thumb drag logic). Returns true if selection changed.
        bool OnRButtonDown(float x, float y, bool shift, bool ctrl);

        // Selection queries for context menu.
        bool HasSelection() const { return m_selected >= 0 && m_selected < static_cast<int>(m_rows.size()); }
        int SelectionCount() const { return static_cast<int>(m_selection.size()); }

        // Clipboard text for the selected rows: single row → segment or NAME=value;
        // multi-selection → all selected rows' data joined with \r\n.
        std::wstring CopyText() const;

        // Save support: the originals as loaded, and the current (edited) state rebuilt
        // from the rows. Used by the host with core EnvWriter.
        bool HasChanges() const;
        const std::vector<Environ::core::EnvVariable>& OriginalVars(Environ::core::Scope scope) const;
        std::vector<Environ::core::EnvVariable> CurrentVars(Environ::core::Scope scope) const;
        bool IsEditing() const { return m_editing >= 0; }
        bool IsEditingName() const { return m_editing >= 0 && m_editingName; }

        // Filter support: live-filter visible rows by substring match.
        void SetFilter(const std::wstring& text);
        bool HasFilter() const { return m_filterActive; }
        int  FilteredRowCount() const;

        // Adjust row/header heights by zoom factor (1.0 = 100%).
        void SetZoom(float zoom);

        // Restore from snapshot: loads snapshot variables into the grid with current
        // registry state as originals, so differences show as dirty edits.
        void SetDataForRestore(
            const std::vector<Environ::core::EnvVariable>& currentUser,
            const std::vector<Environ::core::EnvVariable>& currentMachine,
            const std::vector<Environ::core::EnvVariable>& snapshotUser,
            const std::vector<Environ::core::EnvVariable>& snapshotMachine,
            bool elevated);

    private:
        struct Row
        {
            enum class Kind { Variable, Segment } kind;
            std::wstring col1;          // variable name (Variable rows)
            std::wstring col2;          // scalar value / first-segment / segment path
            std::wstring original;      // initial col2, for value-dirty detection
            std::wstring col1Original;  // initial name (Variable rows), for rename-dirty detection
            int depth;                  // 0 = variable, 1 = segment
            bool readOnly;              // machine var, unelevated
            bool invalid;               // segment path missing
            bool duplicate;             // segment duplicated elsewhere
            // Back-references into the originals for save reconstruction:
            Environ::core::Scope scope; // which scope this row belongs to
            int varIndex;               // index into that scope's original vector
            int segIndex;               // path-list segment index (-1 for a scalar value)
        };

        struct Layout
        {
            D2D1_RECT_F header;
            D2D1_RECT_F data;
            float viewH;
            float contentH;
            float maxScroll;
            bool hasScrollbar;
            D2D1_RECT_F thumb;
        };

        Layout Compute() const;
        void ClearAndSelectFocus();
        float NameColWidth() const;
        D2D1_RECT_F ValueCellRect(int row) const;
        D2D1_RECT_F NameCellRect(int row) const;
        int RowAtPoint(const Layout& lay, float x, float y) const;
        void EnsureVisible(int row);
        void ClampScroll();

        // Filter helpers
        void RebuildFiltered();
        int  FilteredToActual(int fi) const;
        int  ActualToFiltered(int actual) const;
        int  FilteredCount() const;

        std::vector<Row> m_rows;
        std::vector<Environ::core::EnvVariable> m_userOrig;
        std::vector<Environ::core::EnvVariable> m_machineOrig;
        // Per-variable "structurally edited" (add/remove/reorder) flags, by scope + varIndex.
        std::vector<bool> m_userStruct;
        std::vector<bool> m_machineStruct;
        D2D1_RECT_F m_bounds{};
        float m_rowH{30.0f};
        float m_headerH{32.0f};
        float m_zoom{1.0f};        // scales row/header heights and the name-column width
        float m_scrollbarW{10.0f};
        float m_scrollY{0.0f};
        int m_hover{-1};
        int m_selected{-1};
        std::set<int> m_selection;  // highlighted rows (always includes m_selected when non-empty)
        int m_editing{-1};
        bool m_editingName{false}; // true = editing the name cell, false = the value cell
        bool m_draggingThumb{false};
        float m_dragGrabOffset{0.0f};

        // Filter state
        std::wstring     m_filterText;          // lowercased, for comparison
        std::vector<int> m_filtered;            // filtered-index -> actual-row-index
        std::unordered_map<int, std::wstring> m_filteredPromoted; // Segment rows that display a variable name
        bool             m_filterActive{false};
    };
}
