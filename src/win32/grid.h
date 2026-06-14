// Virtualized, theme-driven grid for the environ host (Phase 2: read + inline editing).
// Builds a flat display-row list from core EnvVariables (scalars, and path-lists
// expanded into per-segment child rows), paints only the visible rows, and tracks the
// edit target + dirty state. Not an HWND — the host window forwards paint/input here and
// owns the EDIT control.
#pragma once

#include <d2d1.h>
#include <dwrite.h>
#include <optional>
#include <string>
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
        bool OnLButtonDown(float x, float y);
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
        std::optional<EditTarget> BeginEditAt(float x, float y); // select + edit the row under a point
        bool SelectNextEditable(int dir);                  // Tab: move to next/prev editable row
        void CommitEdit(const std::wstring& text);         // write back + mark dirty
        void CancelEdit();

        // Save support: the originals as loaded, and the current (edited) state rebuilt
        // from the rows. Used by the host with core EnvWriter.
        bool HasChanges() const;
        const std::vector<Environ::core::EnvVariable>& OriginalVars(Environ::core::Scope scope) const;
        std::vector<Environ::core::EnvVariable> CurrentVars(Environ::core::Scope scope) const;
        bool IsEditing() const { return m_editing >= 0; }

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
        float NameColWidth() const;
        D2D1_RECT_F ValueCellRect(int row) const;
        D2D1_RECT_F NameCellRect(int row) const;
        int RowAtPoint(const Layout& lay, float x, float y) const;
        void EnsureVisible(int row);
        void ClampScroll();

        std::vector<Row> m_rows;
        std::vector<Environ::core::EnvVariable> m_userOrig;
        std::vector<Environ::core::EnvVariable> m_machineOrig;
        D2D1_RECT_F m_bounds{};
        float m_rowH{30.0f};
        float m_headerH{32.0f};
        float m_scrollbarW{10.0f};
        float m_scrollY{0.0f};
        int m_hover{-1};
        int m_selected{-1};
        int m_editing{-1};
        bool m_editingName{false}; // true = editing the name cell, false = the value cell
        bool m_draggingThumb{false};
        float m_dragGrabOffset{0.0f};
    };
}
