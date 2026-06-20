#pragma once

#include "d2dwindow.h"
#include "grid.h"
#include "gridview.h"
#include "historyview.h"
#include "AppSettings.h"
#include "EnvStore.h"
#include "EnvWriter.h"
#include "SnapshotStore.h"

namespace ui {

class MainWindow final : public D2DWindow
{
    PNQ_DECLARE_NON_COPYABLE(MainWindow)

public:
    MainWindow() = default;

    bool Create(HINSTANCE hInst, int nCmdShow);

private:
    // Nested layout types (used in method signatures)
    struct ReviewGeom { D2D1_RECT_F card, list, cancelBtn, applyBtn; };

    // D2DWindow overrides
    LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp) override;
    void    OnPaint(const D2D1_SIZE_F& sz) override;
    void    OnSize() override;
    void    OnDpiChanged() override;
    void    OnDestroy() override;

    // Init
    bool    InitSettings();
    bool    InitFonts();
    void    InitData();

    // Painting
    void DrawReviewButton(const D2D1_RECT_F& r, const wchar_t* label,
                          bool primary, bool hover, const theme::ColorScheme& s);
    void PaintNav(const theme::ColorScheme& s, const D2D1_SIZE_F& sz);
    void PaintReview(const theme::ColorScheme& s, const D2D1_SIZE_F& sz);

    // Fonts
    bool CreateFonts();

    // Title bar
    void ApplyTitleBar();

    // Data / save
    void LoadData();
    void SaveChanges();
    void CancelReview();
    void ApplyReviewed();

    // Nav panel
    int  NavItemAt(float x, float y, const D2D1_SIZE_F& sz) const;
    void HandleNavClick(int item);

    // View plumbing
    ViewContext MakeContext() const;
    D2D1_RECT_F ViewBounds(const D2D1_SIZE_F& sz) const;
    void SwitchToView(View* view);
    void CheckHistoryAction();
    void ApplyHistoryRestore();

    // Review layout
    ReviewGeom ReviewLayout(const D2D1_SIZE_F& sz);

    // === Members ===

    // Text formats (subclass-owned; base owns m_fmtCaption + m_fmtGlyph)
    TextFormat m_fmtSub;
    TextFormat m_fmtName;
    TextFormat m_fmtValue;
    TextFormat m_fmtHeader;
    TextFormat m_fmtButton;
    TextFormat m_fmtMono;

    // Theme + grid + core
    theme::ThemeSet              m_theme;
    Grid                         m_grid;
    Environ::core::SnapshotStore m_snapshots;
    Environ::core::AppSettings   m_settings;
    float  m_zoom{1.0f};

    // View layer
    GridView    m_gridView{m_grid, m_theme};
    HistoryView m_historyView{m_snapshots};
    View*       m_activeView{&m_gridView};

    // Nav panel
    bool m_navOpen{false};
    int  m_navHover{-1};
    bool m_navBurgerHover{false};

    // Review modal
    bool m_reviewOpen{false};
    int  m_reviewHover{-1};
    bool m_eatNextDblClk{false};
    std::vector<Environ::core::EnvChange>   m_reviewUser;
    std::vector<Environ::core::EnvChange>   m_reviewMachine;
    std::vector<Environ::core::EnvVariable> m_reviewCurUser;
    std::vector<Environ::core::EnvVariable> m_reviewCurMachine;
};

} // namespace ui
