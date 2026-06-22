// GridView — wraps the Grid control in a View for the navigation architecture.
// Owns the inline EDIT management, context menu, clipboard, detail strip, and footer.
// The Grid object itself is owned by MainWindow and passed by reference.
#pragma once

#include "view.h"
#include "grid.h"
#include "KnowledgeBase.h"

namespace ui {

class GridView final : public View
{
public:
    GridView(Grid& grid, theme::ThemeSet& theme, Environ::core::KnowledgeBase& knowledge);
    ~GridView();

    // View overrides
    void Activate(const ViewContext& ctx) override;
    void Deactivate() override;

    void Paint(const ViewContext& ctx, const D2D1_RECT_F& bounds) override;

    bool OnMouseMove(const ViewContext& ctx, float x, float y) override;
    bool OnMouseLeave() override;
    bool OnLButtonDown(const ViewContext& ctx, float x, float y,
                       bool shift, bool ctrl) override;
    bool OnLButtonUp() override;
    bool OnLButtonDblClk(const ViewContext& ctx, float x, float y) override;
    bool OnRButtonDown(const ViewContext& ctx, float x, float y,
                       bool shift, bool ctrl) override;
    bool OnWheel(const ViewContext& ctx, int delta) override;
    bool OnHWheel(const ViewContext& ctx, int delta) override;
    bool OnKey(const ViewContext& ctx, int vk) override;
    bool OnSysKey(const ViewContext& ctx, int vk, LPARAM lp) override;
    bool OnContextMenu(const ViewContext& ctx, int screenX, int screenY) override;

    void OnSize(const ViewContext& ctx) override;
    void OnDpiChanged(const ViewContext& ctx) override;

    std::wstring GetStatusText(const ViewContext& ctx) const override;

    // Open a folder/file browse dialog for the selected variable or path segment and
    // write the chosen path back. No-op if the selection isn't folder/file-classified.
    void BrowseSelected(const ViewContext& ctx);

    // Reveal the selected path value in Explorer (open the folder / select the file).
    // Works for read-only rows too (Process vars, machine vars when unelevated).
    void RevealSelected(const ViewContext& ctx);

    // GridView-specific API (called by MainWindow)
    void LoadData(bool elevated);
    void OnEditEnd(const ViewContext& ctx, bool commit, bool tab, bool shift);
    LRESULT OnCtlColorEdit(const ViewContext& ctx, WPARAM wp);
    bool HasChanges() const;

    // Editor helpers exposed for MainWindow forwarding
    void BeginEditFromGrid(const ViewContext& ctx);
    void BeginEditNameFromGrid(const ViewContext& ctx);
    void CopyToClipboard(const std::wstring& text);
    void RefreshEditBrush();
    void RefreshEditFont(const ViewContext& ctx);

    // Data count update (e.g. after snapshot restore)
    void SetCounts(size_t userCount, size_t machineCount) { m_userCount = userCount; m_machineCount = machineCount; }

    // Where learned classifications are persisted (%LOCALAPPDATA%\environ\knowledge.toml).
    void SetUserKnowledgePath(const std::wstring& path) { m_userKnowledgePath = path; }

    // Search bar (always visible when GridView is active)
    void FocusSearch();
    void ClearSearch(const ViewContext& ctx);
    bool IsSearchControl(HWND h) const { return h == m_searchEdit; }
    void OnSearchTextChanged(const ViewContext& ctx);

    // Accessors for MainWindow
    HFONT EditFont() const { return m_editFont; }
    bool  IsEditControl(HWND h) const { return h == m_edit; }

private:
    // Editor
    void EnsureEditControl(const ViewContext& ctx);
    void EndEdit(bool commit);
    void PositionEditor(const ViewContext& ctx, const Grid::EditTarget& target);
    void BeginEditAt(const ViewContext& ctx, float x, float y);

    // Menu
    void ShowGridContextMenu(const ViewContext& ctx, int screenX, int screenY);

    // If the selection is a read-only variable with a knowledge-base note, stage that note
    // for prominent display in the detail strip — so an edit attempt explains itself rather
    // than doing nothing. Returns true if a note was staged.
    bool StageReadOnlyNote(const ViewContext& ctx);

    // Search
    void EnsureSearchControl(const ViewContext& ctx);
    void PositionSearchEdit(const ViewContext& ctx, const D2D1_RECT_F& bounds);

    // Path-cell actions. canEdit (Browse, change the value) is gated on editability;
    // canReveal (Open in Explorer) is not — read-only rows can still be revealed.
    enum class CellAction { None, Browse, Reveal };
    Environ::core::KnowledgeBase::PathRole SelectedPathRole() const;   // editable path -> browse
    Environ::core::KnowledgeBase::PathRole SelectedRevealRole() const; // any path -> reveal
    CellAction SelectedCellAction() const;                            // which the in-cell button does
    std::optional<D2D1_RECT_F> CellButtonRect() const;                // in-cell button for the selected row

    Grid&             m_grid;
    theme::ThemeSet&  m_theme;
    Environ::core::KnowledgeBase& m_knowledge; // non-const: LoadData learns into it
    std::wstring      m_userKnowledgePath;     // where learnings persist

    // Read-only edit-attempt note: staged when the user tries to edit a read-only var, and
    // shown (prominently) in the detail strip only while this variable stays selected.
    std::wstring      m_editNote;
    std::wstring      m_editNoteVar;

    // Inline editor (HWND is a child of MainWindow, managed here)
    HWND   m_edit{nullptr};
    HFONT  m_editFont{nullptr};
    HFONT  m_editFontName{nullptr};
    HBRUSH m_editBrush{nullptr};

    // Search bar (always visible, no toggle)
    HWND   m_searchEdit{nullptr};

    // Data counts for footer
    size_t m_userCount{0};
    size_t m_machineCount{0};
    size_t m_processCount{0};
    bool   m_elevated{false};
};

} // namespace ui
