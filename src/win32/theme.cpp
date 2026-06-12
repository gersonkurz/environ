#include "theme.h"

#include <windows.h>

// toml++ in no-throw mode: parse failures come back as a checkable result, not an
// exception. Keeps this host free of throw/catch in our own code.
#define TOML_EXCEPTIONS 0
#define TOML_ENABLE_FORMATTERS 0
#include <toml++/toml.hpp>

namespace theme
{
    namespace
    {
        D2D1_COLOR_F DecodeColor(std::string_view s, D2D1_COLOR_F fallback)
        {
            const auto hex = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            if ((s.size() == 7 || s.size() == 9) && s[0] == '#')
            {
                int v[8]{};
                bool ok{true};
                for (size_t i = 0; i < s.size() - 1; ++i)
                {
                    v[i] = hex(s[i + 1]);
                    if (v[i] < 0) { ok = false; break; }
                }
                if (ok)
                {
                    const unsigned int r = static_cast<unsigned int>(v[0] * 16 + v[1]);
                    const unsigned int g = static_cast<unsigned int>(v[2] * 16 + v[3]);
                    const unsigned int b = static_cast<unsigned int>(v[4] * 16 + v[5]);
                    const float a = (s.size() == 9)
                        ? static_cast<float>(v[6] * 16 + v[7]) / 255.0f
                        : 1.0f;
                    return Rgb((r << 16) | (g << 8) | b, a);
                }
            }
            return fallback;
        }

        D2D1_COLOR_F ReadColor(const toml::table& t, const char* key, D2D1_COLOR_F fb)
        {
            if (auto v = t[key].value<std::string>()) return DecodeColor(*v, fb);
            return fb;
        }

        Style ReadStyle(const toml::table& t, const char* key, Style fb)
        {
            const toml::table* st = t[key].as_table();
            if (!st) return fb;
            Style s{fb};
            if (auto v = (*st)["fill"].value<std::string>())   s.fill = DecodeColor(*v, fb.fill);
            if (auto v = (*st)["border"].value<std::string>()) s.border = DecodeColor(*v, fb.border);
            if (auto v = (*st)["text"].value<std::string>())   s.text = DecodeColor(*v, fb.text);
            if (auto v = (*st)["border_width"].value<double>()) s.borderWidth = static_cast<float>(*v);
            return s;
        }

        ColorScheme ReadScheme(std::string name, const toml::table& t, const ColorScheme& base)
        {
            ColorScheme s{base};
            s.name = std::move(name);
            s.windowBg = ReadColor(t, "window_bg", base.windowBg);
            s.accent = ReadColor(t, "accent", base.accent);
            s.accentText = ReadColor(t, "accent_text", base.accentText);
            s.scrim = ReadColor(t, "scrim", base.scrim);
            s.headerText = ReadColor(t, "header_text", base.headerText);
            s.headerSubtext = ReadColor(t, "header_subtext", base.headerSubtext);
            s.readonlyText = ReadColor(t, "readonly_text", base.readonlyText);
            if (auto v = t["dark_titlebar"].value<bool>()) s.darkTitleBar = *v;
            s.card = ReadStyle(t, "card", base.card);
            s.header = ReadStyle(t, "header", base.header);
            s.row = ReadStyle(t, "row", base.row);
            s.rowHover = ReadStyle(t, "row_hover", base.rowHover);
            s.rowSelected = ReadStyle(t, "row_selected", base.rowSelected);
            s.rowDirty = ReadStyle(t, "row_dirty", base.rowDirty);
            s.rowInvalid = ReadStyle(t, "row_invalid", base.rowInvalid);
            s.rowDuplicate = ReadStyle(t, "row_duplicate", base.rowDuplicate);
            s.captionCloseHover = ReadStyle(t, "caption_close_hover", base.captionCloseHover);
            s.edit = ReadStyle(t, "edit", base.edit);
            return s;
        }

        ColorScheme BuiltinDark()
        {
            return ColorScheme{
                .name{"dark"},
                .windowBg{Rgb(0x1F1F1F)},
                .accent{Rgb(0x4CC2FF)},
                .accentText{Rgb(0x102A33)}, // dark text on the light accent
                .scrim{Rgb(0x000000, 0.5f)},
                .headerText{Rgb(0xF3F3F3)},
                .headerSubtext{Rgb(0x9D9D9D)},
                .readonlyText{Rgb(0x6E6E6E)},
                .darkTitleBar{true},
                .card{.fill{Rgb(0x2B2B2B)}, .border{Rgb(0x3D3D3D)}, .text{Rgb(0xF3F3F3)}, .borderWidth{1.0f}},
                .header{.fill{Rgb(0x262626)}, .border{Rgb(0x3D3D3D)}, .text{Rgb(0x9D9D9D)}, .borderWidth{0.0f}},
                .row{.fill{Rgb(0x2B2B2B)}, .border{Rgb(0x2B2B2B)}, .text{Rgb(0xF3F3F3)}, .borderWidth{0.0f}},
                .rowHover{.fill{Rgb(0x313131)}, .border{Rgb(0x313131)}, .text{Rgb(0xF3F3F3)}, .borderWidth{0.0f}},
                .rowSelected{.fill{Rgb(0x333333)}, .border{Rgb(0x333333)}, .text{Rgb(0xFFFFFF)}, .borderWidth{0.0f}},
                .rowDirty{.fill{Rgb(0x2B2B2B)}, .border{Rgb(0x2B2B2B)}, .text{Rgb(0xFFD479)}, .borderWidth{0.0f}},
                .rowInvalid{.fill{Rgb(0x3A2323)}, .border{Rgb(0x5A2D2D)}, .text{Rgb(0xFF99A0)}, .borderWidth{1.0f}},
                .rowDuplicate{.fill{Rgb(0x2B2B2B)}, .border{Rgb(0x2B2B2B)}, .text{Rgb(0xFFB454)}, .borderWidth{0.0f}},
                .captionCloseHover{.fill{Rgb(0xC42B1C)}, .border{Rgb(0xC42B1C)}, .text{Rgb(0xFFFFFF)}, .borderWidth{0.0f}},
                .edit{.fill{Rgb(0x1A1A1A)}, .border{Rgb(0x4CC2FF)}, .text{Rgb(0xF3F3F3)}, .borderWidth{1.0f}}};
        }

        ColorScheme BuiltinLight()
        {
            return ColorScheme{
                .name{"light"},
                .windowBg{Rgb(0xF3F3F3)},
                .accent{Rgb(0x005FB8)},
                .accentText{Rgb(0xFFFFFF)}, // white text on the dark accent
                .scrim{Rgb(0x000000, 0.4f)},
                .headerText{Rgb(0x1A1A1A)},
                .headerSubtext{Rgb(0x616161)},
                .readonlyText{Rgb(0x9A9A9A)},
                .darkTitleBar{false},
                .card{.fill{Rgb(0xFFFFFF)}, .border{Rgb(0xE0E0E0)}, .text{Rgb(0x1A1A1A)}, .borderWidth{1.0f}},
                .header{.fill{Rgb(0xECECEC)}, .border{Rgb(0xE0E0E0)}, .text{Rgb(0x616161)}, .borderWidth{0.0f}},
                .row{.fill{Rgb(0xFFFFFF)}, .border{Rgb(0xFFFFFF)}, .text{Rgb(0x1A1A1A)}, .borderWidth{0.0f}},
                .rowHover{.fill{Rgb(0xF0F0F0)}, .border{Rgb(0xF0F0F0)}, .text{Rgb(0x1A1A1A)}, .borderWidth{0.0f}},
                .rowSelected{.fill{Rgb(0xEAF3FB)}, .border{Rgb(0xEAF3FB)}, .text{Rgb(0x003A6E)}, .borderWidth{0.0f}},
                .rowDirty{.fill{Rgb(0xFFFFFF)}, .border{Rgb(0xFFFFFF)}, .text{Rgb(0x8A5A00)}, .borderWidth{0.0f}},
                .rowInvalid{.fill{Rgb(0xFCEBEC)}, .border{Rgb(0xF1C7CB)}, .text{Rgb(0xB42330)}, .borderWidth{1.0f}},
                .rowDuplicate{.fill{Rgb(0xFFFFFF)}, .border{Rgb(0xFFFFFF)}, .text{Rgb(0x9A6A00)}, .borderWidth{0.0f}},
                .captionCloseHover{.fill{Rgb(0xC42B1C)}, .border{Rgb(0xC42B1C)}, .text{Rgb(0xFFFFFF)}, .borderWidth{0.0f}},
                .edit{.fill{Rgb(0xFFFFFF)}, .border{Rgb(0x005FB8)}, .text{Rgb(0x1A1A1A)}, .borderWidth{1.0f}}};
        }

        ColorScheme BuiltinBlue()
        {
            return ColorScheme{
                .name{"blue"},
                .windowBg{Rgb(0x0A2540)},
                .accent{Rgb(0x59B0FF)},
                .accentText{Rgb(0x06243B)}, // dark text on the light accent
                .scrim{Rgb(0x000000, 0.5f)},
                .headerText{Rgb(0xEAF2FB)},
                .headerSubtext{Rgb(0x8FB3D9)},
                .readonlyText{Rgb(0x6E8FB5)},
                .darkTitleBar{true},
                .card{.fill{Rgb(0x103459)}, .border{Rgb(0x1E4B7A)}, .text{Rgb(0xEAF2FB)}, .borderWidth{1.0f}},
                .header{.fill{Rgb(0x0D2D4D)}, .border{Rgb(0x1E4B7A)}, .text{Rgb(0x8FB3D9)}, .borderWidth{0.0f}},
                .row{.fill{Rgb(0x103459)}, .border{Rgb(0x103459)}, .text{Rgb(0xEAF2FB)}, .borderWidth{0.0f}},
                .rowHover{.fill{Rgb(0x14416E)}, .border{Rgb(0x14416E)}, .text{Rgb(0xEAF2FB)}, .borderWidth{0.0f}},
                .rowSelected{.fill{Rgb(0x1B4E80)}, .border{Rgb(0x1B4E80)}, .text{Rgb(0xFFFFFF)}, .borderWidth{0.0f}},
                .rowDirty{.fill{Rgb(0x103459)}, .border{Rgb(0x103459)}, .text{Rgb(0xFFD479)}, .borderWidth{0.0f}},
                .rowInvalid{.fill{Rgb(0x3D1F2A)}, .border{Rgb(0x6B3040)}, .text{Rgb(0xFF9FB0)}, .borderWidth{1.0f}},
                .rowDuplicate{.fill{Rgb(0x103459)}, .border{Rgb(0x103459)}, .text{Rgb(0xFFC773)}, .borderWidth{0.0f}},
                .captionCloseHover{.fill{Rgb(0xC42B1C)}, .border{Rgb(0xC42B1C)}, .text{Rgb(0xFFFFFF)}, .borderWidth{0.0f}},
                .edit{.fill{Rgb(0x07203A)}, .border{Rgb(0x59B0FF)}, .text{Rgb(0xEAF2FB)}, .borderWidth{1.0f}}};
        }

        // Read the theme file with a Unicode-safe Win32 call. (Deliberately NOT pnq's
        // text_file: including it pulls toml++ in exceptions mode, conflicting with our
        // TOML_EXCEPTIONS=0 setup below.)
        std::string ReadAll(const std::wstring& path)
        {
            const HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                         nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
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
    }

    void ThemeSet::LoadOrDefault(const std::wstring& tomlPath)
    {
        ColorScheme dark{BuiltinDark()};
        ColorScheme light{BuiltinLight()};
        ColorScheme blue{BuiltinBlue()};

        const std::string content = ReadAll(tomlPath);
        if (!content.empty())
        {
            auto result = toml::parse(content, std::string_view{"theme.toml"});
            if (result)
            {
                const toml::table& root = result.table();
                if (const toml::table* t = root["dark"].as_table())  dark = ReadScheme("dark", *t, dark);
                if (const toml::table* t = root["light"].as_table()) light = ReadScheme("light", *t, light);
                if (const toml::table* t = root["blue"].as_table())  blue = ReadScheme("blue", *t, blue);
            }
        }

        m_schemes = {dark, light, blue};
        m_current = 0;
    }

    bool ThemeSet::SelectByName(const std::string& name)
    {
        for (size_t i = 0; i < m_schemes.size(); ++i)
        {
            if (m_schemes[i].name == name)
            {
                m_current = i;
                return true;
            }
        }
        return false;
    }
}
