#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Environ::core
{

/// UI-agnostic RGBA color (0.0 .. 1.0 per channel).
struct Color4
{
    float r{0.0f};
    float g{0.0f};
    float b{0.0f};
    float a{1.0f};

    constexpr uint8_t r8() const { return static_cast<uint8_t>(r * 255.0f + 0.5f); }
    constexpr uint8_t g8() const { return static_cast<uint8_t>(g * 255.0f + 0.5f); }
    constexpr uint8_t b8() const { return static_cast<uint8_t>(b * 255.0f + 0.5f); }
    constexpr uint8_t a8() const { return static_cast<uint8_t>(a * 255.0f + 0.5f); }
};

/// Build a Color4 from a 0xRRGGBB integer and optional alpha.
constexpr Color4 rgb(unsigned int c, float a = 1.0f)
{
    return Color4{
        ((c >> 16) & 0xFF) / 255.0f,
        ((c >> 8) & 0xFF) / 255.0f,
        (c & 0xFF) / 255.0f,
        a};
}

/// Relative luminance (Rec. 709) in 0.0 .. 1.0.
constexpr float luminance(const Color4& c)
{
    return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
}

/// Linear interpolation between two colors.
constexpr Color4 lerp_color(const Color4& a, const Color4& b, float t)
{
    return Color4{
        a.r + (b.r - a.r) * t,
        a.g + (b.g - a.g) * t,
        a.b + (b.b - a.b) * t,
        a.a + (b.a - a.a) * t};
}

/// Copy a color with a new alpha channel.
constexpr Color4 with_alpha(const Color4& c, float a)
{
    return Color4{c.r, c.g, c.b, a};
}

/// Parse "#RRGGBB" into a Color4; returns opaque black on malformed input.
Color4 hex_to_color(const std::string& hex);

/// Atomic surface style: fill, border, foreground text, and border stroke width.
struct ThemeStyle
{
    Color4 fill;
    Color4 border;
    Color4 text;
    float border_width{0.0f};
};

/// Named Base16 color scheme mapped to semantic UI slots.
struct Base16Scheme
{
    std::string name;
    std::array<Color4, 16> base16{};
    Color4 window_bg;
    Color4 accent;
    Color4 accent_text;
    Color4 scrim;
    Color4 header_text;
    Color4 header_subtext;
    Color4 readonly_text;
    Color4 inherited_text;
    bool dark_title_bar{true};
    bool is_dark{true};
    ThemeStyle card;
    ThemeStyle header;
    ThemeStyle row;
    ThemeStyle row_hover;
    ThemeStyle row_selected;
    ThemeStyle row_dirty;
    ThemeStyle row_invalid;
    ThemeStyle row_duplicate;
    ThemeStyle caption_close_hover;
    ThemeStyle edit;
};

/// Summary metadata for theme picker listings and 16-color swatches.
struct Base16ThemeInfo
{
    std::string name;
    bool dark{true};
    std::array<Color4, 16> base16{};
};

/// Owns the available Base16 YAML schemes loaded from a themes/ directory.
/// Falls back to a built-in dark scheme (Catppuccin Mocha) if none are found.
class Base16ThemeSet
{
public:
    /// Load all *.yaml Base16 themes from `themes_dir`.
    void load_from_directory(const std::wstring& themes_dir);

    /// Select a scheme by exact display name. Returns false if not found.
    bool select_by_name(const std::string& name);

    /// Currently active scheme (always valid after construction or load).
    const Base16Scheme& current() const;

    /// Number of loaded schemes.
    size_t count() const { return m_schemes.size(); }

    /// Look up a scheme by name without changing selection.
    const Base16Scheme* find_by_name(const std::string& name) const;

    /// Display names in sorted load order.
    std::vector<std::string> names() const;

    /// Theme summaries (name, dark/light, 16-color palette) in sorted load order.
    std::vector<Base16ThemeInfo> themes() const;

    /// Built-in Catppuccin Mocha fallback scheme.
    static Base16Scheme fallback_scheme();

private:
    std::vector<Base16Scheme> m_schemes{fallback_scheme()};
    size_t m_current{0};
};

} // namespace Environ::core
