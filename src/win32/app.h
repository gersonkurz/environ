#pragma once

#include "d2dwindow.h"
#include "grid.h"
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
    ~MainWindow();

    bool Create(HINSTANCE hInst, int nCmdShow);

private:
    // Nested layout types (used in method signatures)
    struct ReviewGeom { D2D1_RECT_F card, list, cancelBtn, applyBtn; };
    struct HistoryGeom {
        D2D1_RECT_F card, list, deleteBtn, closeBtn, restoreBtn;
        float rowH{28.0f};
        float detailH{16.0f};
    };

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
    void PaintReview(const theme::ColorScheme& s, const D2D1_SIZE_F& sz);
    void PaintHistory(const theme::ColorScheme& s, const D2D1_SIZE_F& sz);

    // Fonts
    bool CreateFonts();
    void RefreshEditBrush();
    void RefreshEditFont();

    // Editor
    void EnsureEditControl();
    void EndEdit(bool commit);
    void PositionEditor(const Grid::EditTarget& target);
    void BeginEditFromGrid();
    void BeginEditNameFromGrid();
    void BeginEditAt(float x, float y);

    // Title bar
    void ApplyTitleBar();

    // Menu / clipboard
    void ShowGridContextMenu(int screenX, int screenY);
    void CopyToClipboard(const std::wstring& text);

    // Data / save
    void LoadData();
    void SaveChanges();
    void CancelReview();
    void ApplyReviewed();

    // Review layout
    ReviewGeom ReviewLayout(const D2D1_SIZE_F& sz);

    // History
    HistoryGeom HistoryLayout(const D2D1_SIZE_F& sz);
    int   HistoryDetailLineCount(int idx);
    float HistoryListContentH(const HistoryGeom& hg);
    void  ComputeHistoryTables();
    void  HistorySelect(int idx);
    void  DeleteSelectedSnapshot();
    void  OpenHistory();
    void  CloseHistory();
    void  RestoreSnapshot();
    int   HistoryRowAtPoint(const HistoryGeom& hg, float x, float y);
    void  HistoryEnsureVisible(const HistoryGeom& hg, int idx);

    // === Members ===

    // Text formats (subclass-owned; base owns m_fmtCaption + m_fmtGlyph)
    IDWriteTextFormat* m_fmtSub{nullptr};
    IDWriteTextFormat* m_fmtName{nullptr};
    IDWriteTextFormat* m_fmtValue{nullptr};
    IDWriteTextFormat* m_fmtHeader{nullptr};
    IDWriteTextFormat* m_fmtButton{nullptr};
    IDWriteTextFormat* m_fmtMono{nullptr};

    // Theme + grid + core
    theme::ThemeSet              m_theme;
    Grid                         m_grid;
    Environ::core::SnapshotStore m_snapshots;
    Environ::core::AppSettings   m_settings;
    float  m_zoom{1.0f};
    size_t m_userCount{0};
    size_t m_machineCount{0};
    bool   m_elevated{false};

    // Inline editor
    HWND   m_edit{nullptr};
    HFONT  m_editFont{nullptr};
    HFONT  m_editFontName{nullptr};
    HBRUSH m_editBrush{nullptr};

    // Review modal
    bool m_reviewOpen{false};
    int  m_reviewHover{-1};
    bool m_eatNextDblClk{false};
    std::vector<Environ::core::EnvChange>   m_reviewUser;
    std::vector<Environ::core::EnvChange>   m_reviewMachine;
    std::vector<Environ::core::EnvVariable> m_reviewCurUser;
    std::vector<Environ::core::EnvVariable> m_reviewCurMachine;

    // History modal
    bool  m_historyOpen{false};
    int   m_historyHover{-1};
    int   m_historySelected{-1};
    int   m_historyRowHover{-1};
    float m_historyScroll{0.0f};
    std::vector<Environ::core::SnapshotInfo>   m_historySnapshots;
    std::vector<std::wstring>                  m_historyRecordedTable;
    std::vector<std::wstring>                  m_historyCurrentTable;
    std::vector<Environ::core::EnvVariable>    m_historyCurUser;
    std::vector<Environ::core::EnvVariable>    m_historyCurMachine;
};

class App final
{
    PNQ_DECLARE_NON_COPYABLE(App)

public:
    App() = default;

    // Entry point. Returns process exit code.
    int Run(HINSTANCE hInst, int nCmdShow);

private:
    bool InitLogging();
    int  MessageLoop();

    HINSTANCE m_hInst{nullptr};
    int       m_nCmdShow{SW_SHOWDEFAULT};
};

} // namespace ui
