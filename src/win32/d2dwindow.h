#pragma once

#include "theme.h"

namespace ui {

class D2DWindow
{
    PNQ_DECLARE_NON_COPYABLE(D2DWindow)

public:
    D2DWindow() = default;
    virtual ~D2DWindow();

protected:
    // Window creation
    bool CreateD2DWindow(const wchar_t* className, const wchar_t* title,
                         int x, int y, int w, int h, int showCmd);

    // D2D lifecycle
    bool    InitGraphics();
    void    DiscardDeviceResources();
    void    ReleaseGraphics();
    HRESULT EnsureRenderTarget();

    // Text
    IDWriteTextFormat* MakeFormat(const wchar_t* family, float size,
                                  DWRITE_FONT_WEIGHT weight, bool vcenter,
                                  bool hcenter = false);
    void DrawString(const std::wstring& s, IDWriteTextFormat* fmt,
                    const D2D1_RECT_F& box, const D2D1_COLOR_F& c);

    // Caption
    void DrawCaption(const theme::ColorScheme& s, float widthDip,
                     const wchar_t* title);
    void ApplyDarkTitleBar(bool dark);

    // Caption interaction helpers (for subclass message handlers)
    void SetupMouseTracking();
    bool UpdateCaptionHover(float xDip, float yDip, float widthDip);
    bool ResetCaptionTracking();
    bool HandleCaptionClick(float xDip, float yDip, float widthDip);

    // Convenience
    float DipScale() const;
    void  Repaint(bool needed);

    // Message dispatch
    static LRESULT CALLBACK StaticWndProc(HWND, UINT, WPARAM, LPARAM);
    virtual LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp);

    // Virtual hooks
    virtual void OnPaint(const D2D1_SIZE_F& sz) = 0;
    virtual void OnSize() {}
    virtual void OnDpiChanged() {}
    virtual void OnDestroy() {}

    // === Members ===
    HINSTANCE m_hInst{nullptr};
    int       m_nCmdShow{SW_SHOWDEFAULT};
    HWND      m_hwnd{nullptr};

    ID2D1Factory*          m_d2d{nullptr};
    IDWriteFactory*        m_dw{nullptr};
    ID2D1HwndRenderTarget* m_rt{nullptr};
    ID2D1SolidColorBrush*  m_brush{nullptr};

    IDWriteTextFormat* m_fmtCaption{nullptr};
    IDWriteTextFormat* m_fmtGlyph{nullptr};

    bool m_tracking{false};
    int  m_capHover{-1};
};

} // namespace ui
