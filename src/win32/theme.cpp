#include "precomp.h"
#include "theme.h"

// fkYAML is only used in this translation unit — not in precomp.
// C5311: deprecated literal-operator-id form — fkYAML's user-defined literals use it;
// not our code, so suppress.
#pragma warning(push)
#pragma warning(disable: 5311)
#include <fkYAML/node.hpp>
#pragma warning(pop)

#include <algorithm>
#include <optional>

namespace theme
{
    namespace
    {
        // --- File I/O -------------------------------------------------------

        std::string ReadAll(const std::wstring& path)
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

        // --- Color helpers --------------------------------------------------

        D2D1_COLOR_F HexToColor(const std::string& s)
        {
            // Accepts "#RRGGBB" — returns black on malformed input.
            const auto hex = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            if (s.size() == 7 && s[0] == '#')
            {
                int v[6]{};
                bool ok{true};
                for (int i{0}; i < 6; ++i)
                {
                    v[i] = hex(s[static_cast<size_t>(i) + 1]);
                    if (v[i] < 0) { ok = false; break; }
                }
                if (ok)
                {
                    const auto r{static_cast<unsigned int>(v[0] * 16 + v[1])};
                    const auto g{static_cast<unsigned int>(v[2] * 16 + v[3])};
                    const auto b{static_cast<unsigned int>(v[4] * 16 + v[5])};
                    return Rgb((r << 16) | (g << 8) | b);
                }
            }
            return Rgb(0x000000);
        }

        float Luminance(const D2D1_COLOR_F& c)
        {
            return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
        }

        D2D1_COLOR_F Lerp(const D2D1_COLOR_F& a, const D2D1_COLOR_F& b, float t)
        {
            return D2D1_COLOR_F{
                a.r + (b.r - a.r) * t,
                a.g + (b.g - a.g) * t,
                a.b + (b.b - a.b) * t,
                a.a + (b.a - a.a) * t};
        }

        D2D1_COLOR_F WithAlpha(const D2D1_COLOR_F& c, float a)
        {
            return D2D1_COLOR_F{c.r, c.g, c.b, a};
        }

        // --- Base16 → ColorScheme mapping -----------------------------------

        struct Base16Palette
        {
            D2D1_COLOR_F base00, base01, base02, base03, base04, base05, base06, base07;
            D2D1_COLOR_F base08, base09, base0A, base0B, base0C, base0D, base0E, base0F;
        };

        ColorScheme Base16ToScheme(const std::string& name, const Base16Palette& p)
        {
            // accentText must contrast against the accent fill (base0D).
            // Derive from luminance: dark accent -> white text, light accent -> dark text.
            const D2D1_COLOR_F accentText{Luminance(p.base0D) > 0.5f ? p.base00 : Rgb(0xFFFFFF)};

            return ColorScheme{
                .name{name},
                .windowBg{p.base00},
                .accent{p.base0D},
                .accentText{accentText},
                .scrim{WithAlpha(p.base00, 0.5f)},
                .headerText{p.base05},
                .headerSubtext{p.base04},
                .readonlyText{p.base03},
                .darkTitleBar{Luminance(p.base00) < 0.5f},
                .card{.fill{p.base01}, .border{p.base02}, .text{p.base05}, .borderWidth{1.0f}},
                .header{.fill{p.base01}, .border{p.base02}, .text{p.base04}, .borderWidth{0.0f}},
                .row{.fill{p.base01}, .border{p.base01}, .text{p.base05}, .borderWidth{0.0f}},
                .rowHover{.fill{Lerp(p.base01, p.base02, 0.4f)}, .border{Lerp(p.base01, p.base02, 0.4f)}, .text{p.base05}, .borderWidth{0.0f}},
                .rowSelected{.fill{p.base02}, .border{p.base02}, .text{p.base07}, .borderWidth{0.0f}},
                .rowDirty{.fill{p.base01}, .border{p.base01}, .text{p.base0A}, .borderWidth{0.0f}},
                .rowInvalid{.fill{Lerp(p.base00, p.base08, 0.15f)}, .border{Lerp(p.base00, p.base08, 0.35f)}, .text{p.base08}, .borderWidth{1.0f}},
                .rowDuplicate{.fill{p.base01}, .border{p.base01}, .text{p.base09}, .borderWidth{0.0f}},
                .captionCloseHover{.fill{Rgb(0xC42B1C)}, .border{Rgb(0xC42B1C)}, .text{Rgb(0xFFFFFF)}, .borderWidth{0.0f}},
                .edit{.fill{p.base00}, .border{p.base0D}, .text{p.base05}, .borderWidth{1.0f}}};
        }

        // --- YAML parsing ---------------------------------------------------

        std::optional<ColorScheme> LoadYamlScheme(const std::wstring& path)
        {
            const std::string content{ReadAll(path)};
            if (content.empty()) return std::nullopt;

            try
            {
                auto root{fkyaml::node::deserialize(content)};

                const auto getString = [&](const char* key) -> std::string {
                    if (root.contains(key))
                        return root[key].get_value<std::string>();
                    return {};
                };

                const std::string name{getString("name")};
                if (name.empty()) return std::nullopt;

                if (!root.contains("palette")) return std::nullopt;
                auto& palette{root["palette"]};

                const char* keys[]{"base00","base01","base02","base03",
                                   "base04","base05","base06","base07",
                                   "base08","base09","base0A","base0B",
                                   "base0C","base0D","base0E","base0F"};
                D2D1_COLOR_F colors[16]{};
                for (int i{0}; i < 16; ++i)
                {
                    if (!palette.contains(keys[i])) return std::nullopt;
                    colors[i] = HexToColor(palette[keys[i]].get_value<std::string>());
                }

                const Base16Palette p{
                    colors[0],  colors[1],  colors[2],  colors[3],
                    colors[4],  colors[5],  colors[6],  colors[7],
                    colors[8],  colors[9],  colors[10], colors[11],
                    colors[12], colors[13], colors[14], colors[15]};

                return Base16ToScheme(name, p);
            }
            catch (const fkyaml::exception& e)
            {
                spdlog::error("Failed to parse YAML theme '{}': {}",
                    pnq::unicode::to_utf8(path), e.what());
                return std::nullopt;
            }
        }

        // --- Hardcoded fallback (Catppuccin Mocha) --------------------------

        ColorScheme FallbackScheme()
        {
            const Base16Palette mocha{
                Rgb(0x1e1e2e), Rgb(0x181825), Rgb(0x313244), Rgb(0x45475a),
                Rgb(0x585b70), Rgb(0xcdd6f4), Rgb(0xf5e0dc), Rgb(0xb4befe),
                Rgb(0xf38ba8), Rgb(0xfab387), Rgb(0xf9e2af), Rgb(0xa6e3a1),
                Rgb(0x94e2d5), Rgb(0x89b4fa), Rgb(0xcba6f7), Rgb(0xf2cdcd)};
            return Base16ToScheme("Catppuccin Mocha", mocha);
        }
    }

    // --- ThemeSet public API ------------------------------------------------

    void ThemeSet::LoadFromDirectory(const std::wstring& themesDir)
    {
        m_schemes.clear();
        m_current = 0;

        const std::wstring pattern{themesDir + L"\\*.yaml"};
        WIN32_FIND_DATAW fd{};
        const HANDLE hFind{FindFirstFileW(pattern.c_str(), &fd)};
        if (hFind != INVALID_HANDLE_VALUE)
        {
            do
            {
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                const std::wstring filePath{themesDir + L"\\" + fd.cFileName};
                auto scheme{LoadYamlScheme(filePath)};
                if (scheme)
                    m_schemes.push_back(std::move(*scheme));
            } while (FindNextFileW(hFind, &fd));
            FindClose(hFind);
        }

        // Sort alphabetically by name for stable ordering.
        std::sort(m_schemes.begin(), m_schemes.end(),
            [](const ColorScheme& a, const ColorScheme& b) { return a.name < b.name; });

        if (m_schemes.empty())
        {
            spdlog::warn("No themes loaded from '{}'; using built-in fallback",
                pnq::unicode::to_utf8(themesDir));
            m_schemes.push_back(FallbackScheme());
        }

        spdlog::info("Loaded {} theme(s) from '{}'", m_schemes.size(),
            pnq::unicode::to_utf8(themesDir));
    }

    bool ThemeSet::SelectByName(const std::string& name)
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

    std::vector<std::string> ThemeSet::Names() const
    {
        std::vector<std::string> names;
        names.reserve(m_schemes.size());
        for (const auto& s : m_schemes)
            names.push_back(s.name);
        return names;
    }
}
