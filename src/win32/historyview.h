// HistoryView — snapshot history browser, the first real view switch.
// Ctrl+H swaps the active view from GridView to HistoryView.  Esc/Close
// swaps back; Restore swaps back and applies snapshot data to the grid.
#pragma once

#include "view.h"
#include "SnapshotStore.h"

namespace ui {

class HistoryView final : public View
{
public:
    enum class Action { None, Close, Restore };

    struct RestoreData
    {
        std::vector<Environ::core::EnvVariable> curUser;
        std::vector<Environ::core::EnvVariable> curMachine;
        std::vector<Environ::core::EnvVariable> snapUser;
        std::vector<Environ::core::EnvVariable> snapMachine;
    };

    explicit HistoryView(Environ::core::SnapshotStore& snapshots);

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
    RestoreData TakeRestoreData();

private:
    struct Geom {
        D2D1_RECT_F card{}, list{}, deleteBtn{}, closeBtn{}, restoreBtn{};
        float rowH{28.0f};
        float detailH{16.0f};
    };

    Geom  ComputeLayout(const D2D1_RECT_F& bounds) const;
    float ListContentH(const Geom& g) const;
    int   DetailLineCount(int idx) const;
    int   RowAtPoint(const Geom& g, float x, float y) const;
    void  EnsureVisible(const Geom& g, int idx);
    void  Select(int idx);
    void  ComputeDiffTables();
    void  DeleteSelected();
    void  PrepareRestore();
    void  DrawButton(const ViewContext& ctx, const D2D1_RECT_F& r, const wchar_t* label,
                     bool primary, bool hover) const;

    Environ::core::SnapshotStore& m_snapshots;

    // Snapshot data
    std::vector<Environ::core::SnapshotInfo>  m_snapshotList;
    std::vector<std::wstring>                 m_recordedTable;
    std::vector<std::wstring>                 m_currentTable;
    std::vector<Environ::core::EnvVariable>   m_curUser;
    std::vector<Environ::core::EnvVariable>   m_curMachine;

    // UI state
    int   m_selected{-1};
    int   m_rowHover{-1};
    int   m_btnHover{-1};
    float m_scroll{0.0f};
    D2D1_RECT_F m_lastBounds{};

    // Pending action for MainWindow
    Action      m_pendingAction{Action::None};
    RestoreData m_restoreData;
};

} // namespace ui
