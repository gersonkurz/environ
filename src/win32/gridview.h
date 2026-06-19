// GridView — wraps the Grid control in a View for the navigation architecture.
// Owns the inline EDIT management, context menu, clipboard, detail strip, and footer.
// The Grid object itself is owned by MainWindow and passed by reference.
#pragma once

#include "view.h"
#include "grid.h"

namespace ui {

class GridView final : public View
{
public:
    GridView(Grid& grid, theme::ThemeSet& theme);
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
    bool OnKey(const ViewContext& ctx, int vk) override;
    bool OnSysKey(const ViewContext& ctx, int vk, LPARAM lp) override;
    bool OnContextMenu(const ViewContext& ctx, int screenX, int screenY) override;

    void OnSize(const ViewContext& ctx) override;
    void OnDpiChanged(const ViewContext& ctx) override;

    std::wstring GetStatusText(const ViewContext& ctx) const override;

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

    Grid&             m_grid;
    theme::ThemeSet&  m_theme;

    // Inline editor (HWND is a child of MainWindow, managed here)
    HWND   m_edit{nullptr};
    HFONT  m_editFont{nullptr};
    HFONT  m_editFontName{nullptr};
    HBRUSH m_editBrush{nullptr};

    // Data counts for footer
    size_t m_userCount{0};
    size_t m_machineCount{0};
    bool   m_elevated{false};
};

} // namespace ui
