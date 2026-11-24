#include "precomp.h"
#include "AppSettings.h"

#include <sstream>

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
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return (dir / "environ.toml").string();
}

void AppSettings::load(const std::string& override_path) {
    auto path{override_path.empty() ? config_path() : override_path};
    if (path.empty()) {
        spdlog::warn("Could not resolve config path");
        return;
    }

    m_path = path;
    m_backend = std::make_unique<pnq::config::TomlBackend>(path);
    Section::load(*m_backend);
    spdlog::info("Loaded settings from {}", path);
}

bool AppSettings::save(const std::string& override_path) {
    std::string path{override_path.empty() ? m_path : override_path};
    if (path.empty()) {
        path = config_path();
        if (path.empty()) return false;
    }
    m_path = path;

    toml::table root;
    toml::table win;
    win.insert("X", static_cast<int64_t>(window.x.get()));
    win.insert("Y", static_cast<int64_t>(window.y.get()));
    win.insert("Width", static_cast<int64_t>(window.width.get()));
    win.insert("Height", static_cast<int64_t>(window.height.get()));
    win.insert("Maximized", window.maximized.get());
    root.insert("Window", std::move(win));

    toml::table app;
    app.insert("Theme", appearance.theme.get());
    app.insert("Zoom", static_cast<int64_t>(appearance.zoom.get()));
    app.insert("UiFont", appearance.uiFont.get());
    app.insert("MonoFont", appearance.monoFont.get());
    app.insert("FontScale", static_cast<int64_t>(appearance.fontScale.get()));
    root.insert("Appearance", std::move(app));

    std::ostringstream oss;
    oss << root << "\n";
    const std::string payload{oss.str()};

    const std::filesystem::path dst_path{pnq::unicode::to_utf16(path)};
    std::error_code ec;
    if (dst_path.has_parent_path())
        std::filesystem::create_directories(dst_path.parent_path(), ec);

    std::wstring tmp_path{dst_path.wstring() + L".tmp"};
    const HANDLE h{CreateFileW(tmp_path.c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr)};
    if (h == INVALID_HANDLE_VALUE) {
        spdlog::error("Failed to create temp settings file '{}' (err={})",
            pnq::unicode::to_utf8(tmp_path), GetLastError());
        return false;
    }

    DWORD written{0};
    const BOOL write_ok{WriteFile(h, payload.data(), static_cast<DWORD>(payload.size()), &written, nullptr)};
    const BOOL flush_ok{write_ok ? FlushFileBuffers(h) : FALSE};
    const DWORD write_err{(!write_ok || !flush_ok) ? GetLastError() : ERROR_SUCCESS};
    CloseHandle(h);

    if (!write_ok || !flush_ok || written != payload.size()) {
        DeleteFileW(tmp_path.c_str());
        spdlog::error("Failed to write temp settings file '{}' (err={})",
            pnq::unicode::to_utf8(tmp_path), write_err);
        return false;
    }

    if (!MoveFileExW(tmp_path.c_str(), dst_path.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const DWORD move_err{GetLastError()};
        DeleteFileW(tmp_path.c_str());
        spdlog::error("Failed to atomically replace settings file '{}' (err={})",
            path, move_err);
        return false;
    }

    return true;
}

} // namespace Environ::core
