// Theme layer for the Win32 host.
// Inspired by ProAKT's ColorScheme/DataSet model: one central table of named,
// per-state styles that every painter reads from — translated to D2D-native types.
// Schemes are pure data (no device resources), so loading/swapping is trivial and
// device-loss only ever touches the single render-target brush in app.cpp.
#pragma once

#include <d2d1.h>
#include <string>
#include <vector>

namespace theme
{
    constexpr D2D1_COLOR_F Rgb(unsigned int c, float a = 1.0f)
    {
        return D2D1_COLOR_F{
            ((c >> 16) & 0xFF) / 255.0f,
            ((c >> 8) & 0xFF) / 255.0f,
            (c & 0xFF) / 255.0f,
            a};
    }

    // The atomic style unit — ProAKT's DataSet, minus the GDI handles.
    struct Style
    {
        D2D1_COLOR_F fill;
        D2D1_COLOR_F border;
        D2D1_COLOR_F text;
        float borderWidth;
    };

    // A named scheme. The grid-row states are the heart, mirroring ProAKT's Lv* set.
    struct ColorScheme
    {
        std::string name;
        D2D1_COLOR_F windowBg;
        D2D1_COLOR_F accent;
        D2D1_COLOR_F headerText;
        D2D1_COLOR_F headerSubtext;
        D2D1_COLOR_F readonlyText; // machine vars when unelevated
        bool darkTitleBar;
        Style card;
        Style header;       // grid column-header row
        Style row;
        Style rowHover;
        Style rowSelected;
        Style rowDirty;     // reserved for Phase 2 (editing)
        Style rowInvalid;   // path segment does not exist
        Style rowDuplicate; // path segment duplicated elsewhere
        Style captionCloseHover; // title-bar close button when hovered (fill + glyph text)
        Style edit;              // inline cell editor: fill + focus border + text
    };

    // Owns the available schemes. Always yields at least the built-in dark/light/blue
    // (so the app renders even with no theme.toml); a theme.toml overrides them.
    class ThemeSet
    {
    public:
        void LoadOrDefault(const std::wstring& tomlPath);
        bool SelectByName(const std::string& name);
        const ColorScheme& Current() const { return m_schemes[m_current]; }
        size_t Count() const { return m_schemes.size(); }

    private:
        std::vector<ColorScheme> m_schemes;
        size_t m_current{0};
    };
}
