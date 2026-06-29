// ThemeSelectionView — lists all loaded Base16 themes in two columns (Dark |
// Light), each row showing the theme name and a 16-color swatch.  Selecting a
// theme live-previews it (colours change immediately, not persisted); Apply
// commits the choice, Cancel/Esc restores the theme that was active on open.
#pragma once

#include "view.h"

#include <array>

namespace ui {

class ThemeSelectionView final : public View
{
public:
    // Apply commits + closes; Cancel restores the opening theme + closes.
    enum class Action { None, Apply, Cancel };

    explicit ThemeSelectionView(theme::ThemeSet& themes);

    // View overrides
    void Activate(const ViewContext& ctx) override;
    void Deactivate() override;

    void Paint(const ViewContext& ctx, const D2D1_RECT_F& bounds) override;

    bool OnMouseMove(const ViewContext& ctx, float x, float y) override;
    bool OnMouseLeave() override;
    bool OnLButtonDown(const ViewContext& ctx, float x, float y,
                       bool shift, bool ctrl) override;
    bool OnWheel(const ViewContext& ctx, int delta) override;
    bool OnKey(const ViewContext& ctx, int vk) override;

    std::wstring GetStatusText(const ViewContext& ctx) const override;

    // Pending-action API (polled by MainWindow after input dispatch)
    Action PendingAction() const { return m_pendingAction; }
    void   ClearPendingAction()  { m_pendingAction = Action::None; }
    bool   ThemeChanged() const  { return m_themeChanged; }
    void   ClearThemeChanged()   { m_themeChanged = false; }

private:
    struct Geom {
        D2D1_RECT_F card{}, col[2]{}, okBtn{}, cancelBtn{};
        float rowH{46.0f};
    };

    // One row in a column: a group header ("Dark"/"Light") or a theme.
    struct Entry
    {
        bool header{false};
        std::wstring label;                  // display text (header title or theme name)
        std::string themeName;               // scheme name to select (theme rows only)
        std::array<D2D1_COLOR_F, 16> base16; // palette for the swatch (theme rows only)
    };

    void  BuildColumns();
    void  Preview(const std::string& name); // visual-only theme switch + repaint flag
    int   NextThemeRow(int col, int from, int dir) const; // skips headers; -1 if none

    Geom  ComputeLayout(const D2D1_RECT_F& bounds) const;
    bool  RowAtPoint(const Geom& g, float x, float y, int& col, int& row) const;
    void  EnsureVisible(const Geom& g, int row);
    void  ClampScroll(const Geom& g);
    int   MaxRows() const; // tallest column, for scroll bounds
    void  DrawRow(const ViewContext& ctx, const Entry& e, const D2D1_RECT_F& r,
                  bool selected, bool hovered) const;

    theme::ThemeSet&   m_themes;
    std::vector<Entry> m_col[2]; // 0 = Dark, 1 = Light (each: header + theme rows)
    int   m_selCol{-1}, m_selRow{-1}; // selected theme (column + row), or -1
    int   m_hovCol{-1}, m_hovRow{-1}; // hovered theme row, or -1
    int   m_btnHover{-1};             // 0 = Apply, 1 = Cancel, else -1
    float m_scroll{0.0f};
    bool  m_needScrollToSelected{false};
    D2D1_RECT_F m_lastBounds{};
    std::string m_openingTheme;       // theme active when the view opened (for Cancel)
    Action m_pendingAction{Action::None};
    bool   m_themeChanged{false};
};

} // namespace ui
