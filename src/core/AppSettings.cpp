#include "AppSettings.h"

#include <spdlog/spdlog.h>

#include <filesystem>

#include <windows.h>
#include <shlobj.h>

namespace Environ::core {

std::string AppSettings::config_path() {
    wchar_t* appdata{nullptr};
    if (SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &appdata) != S_OK) {
        CoTaskMemFree(appdata);
        return {};
    }
    std::filesystem::path dir{appdata};
    CoTaskMemFree(appdata);
    dir /= L"environ";
    std::filesystem::create_directories(dir);
    return (dir / "environ.toml").string();
}

void AppSettings::load() {
    auto path{config_path()};
    if (path.empty()) {
        spdlog::warn("Could not resolve config path");
        return;
    }

    m_backend = std::make_unique<pnq::config::TomlBackend>(path);
    Section::load(*m_backend);
    spdlog::info("Loaded settings from {}", path);
}

void AppSettings::save() {
    if (!m_backend) {
        auto path{config_path()};
        if (path.empty()) return;
        m_backend = std::make_unique<pnq::config::TomlBackend>(path);
    }

    Section::save(*m_backend);
}

} // namespace Environ::core
