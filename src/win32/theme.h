// Theme layer for the Win32 host.
// Loads Base16 YAML schemes from a themes/ directory beside the exe and maps
// the 16-color palette deterministically to a ColorScheme used by all painters.
// Schemes are pure data (no device resources), so loading/swapping is trivial.
#pragma once

#include <d2d1.h>
#include <string>
#include <vector>

// Point-in-rect hit test, shared by every painter (right-open/bottom-open).
inline bool Contains(const D2D1_RECT_F& r, float x, float y)
{
    return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
}

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
        D2D1_COLOR_F accentText; // text drawn on an accent fill (e.g. primary button)
        D2D1_COLOR_F scrim;      // translucent backdrop behind a modal
        D2D1_COLOR_F headerText;
        D2D1_COLOR_F headerSubtext;
        D2D1_COLOR_F readonlyText; // machine vars when unelevated
        bool darkTitleBar;
        bool isDark; // dark vs light scheme (from background luminance); used for grouping
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

    // A theme's name plus whether it is a dark scheme — for grouped listings.
    struct ThemeInfo
    {
        std::string name;
        bool dark;
    };

    // Owns the available schemes loaded from Base16 YAML files in a themes/ directory.
    // Falls back to a hardcoded dark scheme if the directory is empty or missing.
    class ThemeSet
    {
    public:
        void LoadFromDirectory(const std::wstring& themesDir);
        bool SelectByName(const std::string& name);
        const ColorScheme& Current() const { return m_schemes[m_current]; }
        size_t Count() const { return m_schemes.size(); }
        std::vector<std::string> Names() const;
        std::vector<ThemeInfo> Themes() const; // names + dark/light, in load order

    private:
        std::vector<ColorScheme> m_schemes;
        size_t m_current{0};
    };
}
