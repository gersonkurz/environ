#pragma once

#include <pnq/config/section.h>
#include <pnq/config/toml_backend.h>
#include <pnq/config/typed_value.h>

#include <cstdint>
#include <memory>
#include <string>

namespace Environ::core {

using pnq::config::Section;
using pnq::config::TypedValue;

struct AppSettings : public Section {
    AppSettings()
        : Section{}
    {
    }

    void load();
    void save();

    struct WindowSettings : public Section {
        WindowSettings(Section* parent)
            : Section{parent, "Window"}
        {
        }

        TypedValue<int32_t> x{this, "X", -1};
        TypedValue<int32_t> y{this, "Y", -1};
        TypedValue<int32_t> width{this, "Width", 1100};
        TypedValue<int32_t> height{this, "Height", 700};
        TypedValue<bool> maximized{this, "Maximized", false};

    } window{this};

    struct AppearanceSettings : public Section {
        AppearanceSettings(Section* parent)
            : Section{parent, "Appearance"}
        {
        }

        // Base16 scheme name (file stem in themes/, e.g. "nord"); empty = built-in fallback.
        TypedValue<std::string> theme{this, "Theme", "System"};
        TypedValue<int32_t> zoom{this, "Zoom", 100}; // percentage: 50–200

        // Typography (host applies these; glyph/icon font stays fixed in code).
        TypedValue<std::string> uiFont{this, "UiFont", "Segoe UI Variable Text"};
        TypedValue<std::string> monoFont{this, "MonoFont", "Consolas"};
        TypedValue<int32_t> fontScale{this, "FontScale", 100}; // percentage: 75–150

    } appearance{this};

private:
    std::unique_ptr<pnq::config::TomlBackend> m_backend;

    static std::string config_path();
};

} // namespace Environ::core
