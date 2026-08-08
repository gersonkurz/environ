// FontSelectionView — single-column picker for the UI text family, listing the
// installed font families from the DirectWrite system collection.  Selecting a
// family live-previews it (the host rebuilds every text format, so the picker
// itself re-renders in the candidate font); Apply persists, Cancel/Esc restores
// the family that was active on open.
#pragma once

#include "view.h"

#include <vector>

namespace ui {

class FontSelectionView final : public View
{
public:
    // Apply commits + closes; Cancel restores the opening family + closes.
    enum class Action { None, Apply, Cancel };

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

    // Pending-action API (polled by MainWindow after input dispatch), mirroring
    // ThemeSelectionView: FontChanged drives the live preview, PendingAction closes the view.
    Action PendingAction() const { return m_pendingAction; }
    void   ClearPendingAction()  { m_pendingAction = Action::None; }
    bool   FontChanged() const   { return m_fontChanged; }
    void   ClearFontChanged()    { m_fontChanged = false; }

    // The family the host should currently be painting with (empty only before Activate).
    const std::wstring& SelectedFamily() const { return m_selected; }

private:
    struct Geom
    {
        D2D1_RECT_F card{}, list{}, sample{}, okBtn{}, cancelBtn{};
        float rowH{28.0f};
    };

    void  BuildFamilies(const ViewContext& ctx);
    void  Preview(int row); // select a row + flag the host to rebuild its text formats
    int   RowAtPoint(const Geom& g, float x, float y) const; // -1 if none
    int   FirstMatching(wchar_t initial, int from) const;    // type-to-jump; -1 if none
    Geom  ComputeLayout(const D2D1_RECT_F& bounds) const;
    void  ClampScroll(const Geom& g);
    void  EnsureVisible(const Geom& g, int row);

    std::vector<std::wstring> m_families;
    std::wstring m_openingFamily;
    std::wstring m_selected;

    int   m_sel{-1};
    int   m_hov{-1};
    int   m_btnHover{-1};
    float m_scroll{0.0f};
    bool  m_needScrollToSelected{false};
    bool  m_fontChanged{false};
    Action m_pendingAction{Action::None};
    D2D1_RECT_F m_lastBounds{};
};

} // namespace ui
