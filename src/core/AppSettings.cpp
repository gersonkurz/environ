#include "precomp.h"
#include "AppSettings.h"

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
    const auto parseResult = toml::parse_file(path);
    m_backend = std::make_unique<pnq::config::TomlBackend>(path);

    // Debug: test the backend directly before Section::load
    int32_t testX{-999};
    const bool readOk{m_backend->load("Window//X", testX)};
    spdlog::info("Direct backend read Window/X: ok={}, value={}", readOk, testX);
    spdlog::info("Section 'Window' exists: {}", m_backend->sectionExists("Window"));
    spdlog::info("Section child count: {}", getChildItems().size());

    const auto sectionLoadResult = Section::load(*m_backend);
 spdlog::info("AftersectionLoadResulty={}", sectionLoadResult);
    spdlog::info("After Section::load: x={}, y={}", window.x.get(), window.y.get());
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
