// View abstraction for the environ host.
// MainWindow is a shell (title bar, footer) that hosts one active View at a time.
// GridView is the default; future phases add HistoryView, ReviewView, etc.
#pragma once

#include <d2d1.h>
#include <dwrite.h>
#include <string>

#include "theme.h"

namespace ui {

// Everything a View needs from the host to paint and respond to input.
// Passed by const-ref on each call — no View caches a ViewContext.
struct ViewContext
{
    ID2D1RenderTarget*    rt;
    ID2D1SolidColorBrush* brush;
    const theme::ColorScheme* scheme;
    HWND  hwnd;
    float zoom;
    float dipScale;

    IDWriteTextFormat* fmtSub;
    IDWriteTextFormat* fmtName;
    IDWriteTextFormat* fmtValue;
    IDWriteTextFormat* fmtHeader;
    IDWriteTextFormat* fmtButton;
    IDWriteTextFormat* fmtMono;
    IDWriteTextFormat* fmtGlyph;
};

class View
{
public:
    View() = default;
    virtual ~View() = default;
    View(const View&) = delete;
    View& operator=(const View&) = delete;

    virtual void Activate(const ViewContext& /*ctx*/)  {}
    virtual void Deactivate() {}

    virtual void Paint(const ViewContext& ctx, const D2D1_RECT_F& bounds) = 0;

    virtual bool OnMouseMove(const ViewContext& /*ctx*/, float /*x*/, float /*y*/) { return false; }
    virtual bool OnMouseLeave() { return false; }
    virtual bool OnLButtonDown(const ViewContext& /*ctx*/, float /*x*/, float /*y*/,
                               bool /*shift*/, bool /*ctrl*/) { return false; }
    virtual bool OnLButtonUp() { return false; }
    virtual bool OnLButtonDblClk(const ViewContext& /*ctx*/, float /*x*/, float /*y*/) { return false; }
    virtual bool OnRButtonDown(const ViewContext& /*ctx*/, float /*x*/, float /*y*/,
                               bool /*shift*/, bool /*ctrl*/) { return false; }
    virtual bool OnWheel(const ViewContext& /*ctx*/, int /*delta*/) { return false; }
    virtual bool OnKey(const ViewContext& /*ctx*/, int /*vk*/) { return false; }
    virtual bool OnSysKey(const ViewContext& /*ctx*/, int /*vk*/, LPARAM /*lp*/) { return false; }
    virtual bool OnContextMenu(const ViewContext& /*ctx*/, int /*screenX*/, int /*screenY*/) { return false; }

    virtual void OnSize(const ViewContext& /*ctx*/) {}
    virtual void OnDpiChanged(const ViewContext& /*ctx*/) {}

    virtual std::wstring GetStatusText(const ViewContext& /*ctx*/) const { return {}; }
};

// Free function so views can draw text without access to D2DWindow::DrawString.
inline void DrawString(const ViewContext& ctx, const std::wstring& s,
                       IDWriteTextFormat* fmt, const D2D1_RECT_F& box,
                       const D2D1_COLOR_F& c)
{
    ctx.brush->SetColor(c);
    ctx.rt->DrawTextW(s.c_str(), static_cast<UINT32>(s.size()), fmt, box,
                      ctx.brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

} // namespace ui
