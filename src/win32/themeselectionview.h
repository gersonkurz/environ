// ThemeSelectionView — lists all loaded Base16 themes and lets the user
// pick one.  Selection is immediate (colours change on click / arrow key).
// Close returns to GridView; the chosen theme is persisted by MainWindow.
#pragma once

#include "view.h"

namespace ui {

class ThemeSelectionView final : public View
{
public:
    enum class Action { None, Close };

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
        D2D1_RECT_F card{}, list{}, closeBtn{};
        float rowH{32.0f};
    };

    // One row in the list: either a group header ("Dark"/"Light") or a theme.
    struct Entry
    {
        bool header{false};
        std::wstring label;    // display text (header title or theme name)
        std::string themeName; // scheme name to select (theme rows only)
    };

    void  BuildEntries();
    int   NextThemeRow(int from, int dir) const; // skips headers; -1 if none

    Geom  ComputeLayout(const D2D1_RECT_F& bounds) const;
    int   RowAtPoint(const Geom& g, float x, float y) const; // theme rows only, else -1
    void  EnsureVisible(const Geom& g, int idx);
    void  ClampScroll(const Geom& g);
    void  DrawButton(const ViewContext& ctx, const D2D1_RECT_F& r,
                     const wchar_t* label, bool primary, bool hover) const;

    theme::ThemeSet&     m_themes;
    std::vector<Entry>   m_entries;
    int   m_selected{-1};   // index into m_entries (a theme row), or -1
    int   m_rowHover{-1};   // index into m_entries (a theme row), or -1
    int   m_btnHover{-1};
    float m_scroll{0.0f};
    bool  m_needScrollToSelected{false}; // honor on next paint (real bounds known)
    D2D1_RECT_F m_lastBounds{};
    Action m_pendingAction{Action::None};
    bool   m_themeChanged{false};
};

} // namespace ui
