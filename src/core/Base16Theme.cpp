#include "precomp.h"
#include "Base16Theme.h"

// fkYAML is only used in this translation unit — not in precomp.
// C5311: deprecated literal-operator-id form in fkYAML's user-defined literals.
#pragma warning(push)
#pragma warning(disable: 5311)
#include <fkYAML/node.hpp>
#pragma warning(pop)

#include <algorithm>
#include <optional>

namespace Environ::core
{
namespace
{

std::string read_all_bytes(const std::wstring& path)
{
    const HANDLE h{CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
    if (h == INVALID_HANDLE_VALUE) return {};

    LARGE_INTEGER size{};
    std::string out;
    if (GetFileSizeEx(h, &size) && size.QuadPart > 0 && size.QuadPart < (16 << 20))
    {
        out.resize(static_cast<size_t>(size.QuadPart));
        DWORD read{0};
        if (!ReadFile(h, out.data(), static_cast<DWORD>(out.size()), &read, nullptr))
            out.clear();
        else
            out.resize(read);
    }
    CloseHandle(h);
    return out;
}

struct Base16Palette
{
    Color4 base00, base01, base02, base03, base04, base05, base06, base07;
    Color4 base08, base09, base0A, base0B, base0C, base0D, base0E, base0F;
};

Base16Scheme base16_to_scheme(const std::string& name, const Base16Palette& p)
{
    const Color4 accent_text{luminance(p.base0D) > 0.5f ? p.base00 : rgb(0xFFFFFF)};
    const bool is_dark{luminance(p.base00) < 0.5f};

    return Base16Scheme{
        .name{name},
        .base16{p.base00, p.base01, p.base02, p.base03, p.base04, p.base05, p.base06, p.base07,
                p.base08, p.base09, p.base0A, p.base0B, p.base0C, p.base0D, p.base0E, p.base0F},
        .window_bg{p.base00},
        .accent{p.base0D},
        .accent_text{accent_text},
        .scrim{with_alpha(p.base00, 0.5f)},
        .header_text{p.base05},
        .header_subtext{p.base04},
        .readonly_text{p.base03},
        .inherited_text{lerp_color(p.base03, p.base05, 0.30f)},
        .dark_title_bar{is_dark},
        .is_dark{is_dark},
        .card{.fill{p.base01}, .border{p.base02}, .text{p.base05}, .border_width{1.0f}},
        .header{.fill{p.base01}, .border{p.base02}, .text{p.base04}, .border_width{0.0f}},
        .row{.fill{p.base01}, .border{p.base01}, .text{p.base0D}, .border_width{0.0f}},
        .row_hover{.fill{lerp_color(p.base01, p.base02, 0.4f)},
                   .border{lerp_color(p.base01, p.base02, 0.4f)},
                   .text{p.base05},
                   .border_width{0.0f}},
        .row_selected{.fill{p.base02}, .border{p.base02}, .text{p.base07}, .border_width{0.0f}},
        .row_dirty{.fill{p.base01}, .border{p.base01}, .text{p.base0A}, .border_width{0.0f}},
        .row_invalid{.fill{lerp_color(p.base00, p.base08, 0.15f)},
                     .border{lerp_color(p.base00, p.base08, 0.35f)},
                     .text{p.base08},
                     .border_width{1.0f}},
        .row_duplicate{.fill{p.base01}, .border{p.base01}, .text{p.base09}, .border_width{0.0f}},
        .caption_close_hover{.fill{rgb(0xC42B1C)}, .border{rgb(0xC42B1C)}, .text{rgb(0xFFFFFF)}, .border_width{0.0f}},
        .edit{.fill{p.base00}, .border{p.base0D}, .text{p.base05}, .border_width{1.0f}}};
}

std::optional<Base16Scheme> load_yaml_scheme(const std::wstring& path)
{
    const std::string content{read_all_bytes(path)};
    if (content.empty()) return std::nullopt;

    try
    {
        auto root{fkyaml::node::deserialize(content)};

        const auto get_string = [&](const char* key) -> std::string {
            if (root.contains(key))
                return root[key].get_value<std::string>();
            return {};
        };

        const std::string name{get_string("name")};
        if (name.empty()) return std::nullopt;

        if (!root.contains("palette")) return std::nullopt;
        auto& palette{root["palette"]};

        static constexpr const char* kKeys[16]{
            "base00", "base01", "base02", "base03",
            "base04", "base05", "base06", "base07",
            "base08", "base09", "base0A", "base0B",
            "base0C", "base0D", "base0E", "base0F"};
        Color4 colors[16]{};
        for (int i{0}; i < 16; ++i)
        {
            if (!palette.contains(kKeys[i])) return std::nullopt;
            colors[i] = hex_to_color(palette[kKeys[i]].get_value<std::string>());
        }

        const Base16Palette p{
            colors[0],  colors[1],  colors[2],  colors[3],
            colors[4],  colors[5],  colors[6],  colors[7],
            colors[8],  colors[9],  colors[10], colors[11],
            colors[12], colors[13], colors[14], colors[15]};

        return base16_to_scheme(name, p);
    }
    catch (const fkyaml::exception& e)
    {
        spdlog::error("Failed to parse YAML theme '{}': {}",
            pnq::unicode::to_utf8(path), e.what());
        return std::nullopt;
    }
}

} // namespace

Color4 hex_to_color(const std::string& hex)
{
    const auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };

    if (hex.size() == 7 && hex[0] == '#')
    {
        int v[6]{};
        bool ok{true};
        for (int i{0}; i < 6; ++i)
        {
            v[i] = nibble(hex[static_cast<size_t>(i) + 1]);
            if (v[i] < 0)
            {
                ok = false;
                break;
            }
        }
        if (ok)
        {
            const auto r{static_cast<unsigned int>(v[0] * 16 + v[1])};
            const auto g{static_cast<unsigned int>(v[2] * 16 + v[3])};
            const auto b{static_cast<unsigned int>(v[4] * 16 + v[5])};
            return rgb((r << 16) | (g << 8) | b);
        }
    }
    return rgb(0x000000);
}

Base16Scheme Base16ThemeSet::fallback_scheme()
{
    const Base16Palette mocha{
        rgb(0x1e1e2e), rgb(0x181825), rgb(0x313244), rgb(0x45475a),
        rgb(0x585b70), rgb(0xcdd6f4), rgb(0xf5e0dc), rgb(0xb4befe),
        rgb(0xf38ba8), rgb(0xfab387), rgb(0xf9e2af), rgb(0xa6e3a1),
        rgb(0x94e2d5), rgb(0x89b4fa), rgb(0xcba6f7), rgb(0xf2cdcd)};
    return base16_to_scheme("Catppuccin Mocha", mocha);
}

void Base16ThemeSet::load_from_directory(const std::wstring& themes_dir)
{
    m_schemes.clear();
    m_current = 0;

    const std::wstring pattern{themes_dir + L"\\*.yaml"};
    WIN32_FIND_DATAW fd{};
    const HANDLE hFind{FindFirstFileW(pattern.c_str(), &fd)};
    if (hFind != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            const std::wstring file_path{themes_dir + L"\\" + fd.cFileName};
            if (auto scheme{load_yaml_scheme(file_path)})
                m_schemes.push_back(std::move(*scheme));
        } while (FindNextFileW(hFind, &fd));
        FindClose(hFind);
    }

    std::sort(m_schemes.begin(), m_schemes.end(),
        [](const Base16Scheme& a, const Base16Scheme& b) { return a.name < b.name; });

    if (m_schemes.empty())
    {
        spdlog::warn("No themes loaded from '{}'; using built-in fallback",
            pnq::unicode::to_utf8(themes_dir));
        m_schemes.push_back(fallback_scheme());
    }

    spdlog::info("Loaded {} theme(s) from '{}'", m_schemes.size(),
        pnq::unicode::to_utf8(themes_dir));
}

bool Base16ThemeSet::select_by_name(const std::string& name)
{
    for (size_t i{0}; i < m_schemes.size(); ++i)
    {
        if (m_schemes[i].name == name)
        {
            m_current = i;
            return true;
        }
    }
    return false;
}

const Base16Scheme& Base16ThemeSet::current() const
{
    return m_schemes[m_current];
}

const Base16Scheme* Base16ThemeSet::find_by_name(const std::string& name) const
{
    for (const auto& s : m_schemes)
    {
        if (s.name == name) return &s;
    }
    return nullptr;
}

std::vector<std::string> Base16ThemeSet::names() const
{
    std::vector<std::string> out;
    out.reserve(m_schemes.size());
    for (const auto& s : m_schemes)
        out.push_back(s.name);
    return out;
}

std::vector<Base16ThemeInfo> Base16ThemeSet::themes() const
{
    std::vector<Base16ThemeInfo> infos;
    infos.reserve(m_schemes.size());
    for (const auto& s : m_schemes)
        infos.push_back(Base16ThemeInfo{s.name, s.is_dark, s.base16});
    return infos;
}

} // namespace Environ::core
