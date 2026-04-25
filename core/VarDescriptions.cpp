#include "pch.h"
#include "VarDescriptions.h"

// json.hpp is large — deliberately NOT in pch.h
#include "../Extern/tomlplusplus/vendor/json.hpp"

#include <pnq/unicode.h>
#include <spdlog/spdlog.h>

#include <filesystem>
#include <fstream>

#include <windows.h>
#include <shlobj.h>

namespace Environ::core {

std::wstring VarDescriptions::exe_dir()
{
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::filesystem::path p{path};
    return p.parent_path().wstring();
}

std::wstring VarDescriptions::local_app_data_dir()
{
    wchar_t* appdata{nullptr};
    if (SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &appdata) != S_OK) {
        CoTaskMemFree(appdata);
        return {};
    }
    std::wstring result{appdata};
    CoTaskMemFree(appdata);
    return result;
}

void VarDescriptions::load_file(std::wstring const& path)
{
    if (!std::filesystem::exists(path)) return;

    try {
        std::ifstream f{path, std::ios::binary};
        if (!f) return;

        auto j{nlohmann::json::parse(f, nullptr, false)};
        if (j.is_discarded() || !j.is_object()) {
            spdlog::warn("VarDescriptions: invalid JSON in {}", std::filesystem::path{path}.string());
            return;
        }

        for (auto& [key, val] : j.items()) {
            if (!val.is_string()) continue;

            // Lowercase the key for case-insensitive lookup
            auto wkey{pnq::unicode::to_utf16(key)};
            std::transform(wkey.begin(), wkey.end(), wkey.begin(), ::towlower);

            auto wval{pnq::unicode::to_utf16(val.get<std::string>())};
            m_map.insert_or_assign(std::move(wkey), std::move(wval));
        }

        spdlog::info("VarDescriptions: loaded {}", std::filesystem::path{path}.string());
    }
    catch (std::exception const& e) {
        spdlog::warn("VarDescriptions: error loading file: {}", e.what());
    }
}

void VarDescriptions::load()
{
    m_map.clear();

    // Tier 1: Built-in (next to .exe)
    auto exe{exe_dir()};
    if (!exe.empty()) {
        load_file(exe + L"\\variables.json");
    }

    // Tier 2: Community packs
    auto local{local_app_data_dir()};
    if (!local.empty()) {
        auto defs_dir{local + L"\\environ\\definitions"};
        if (std::filesystem::exists(defs_dir)) {
            for (auto& entry : std::filesystem::directory_iterator(defs_dir)) {
                if (entry.is_regular_file() && entry.path().extension() == L".json") {
                    load_file(entry.path().wstring());
                }
            }
        }

        // Tier 3: User overrides
        load_file(local + L"\\environ\\variables.user.json");
    }

    spdlog::info("VarDescriptions: {} entries loaded", m_map.size());
}

std::optional<std::wstring> VarDescriptions::find(std::wstring_view name) const
{
    std::wstring lower{name};
    std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);

    auto it{m_map.find(lower)};
    if (it != m_map.end()) {
        return it->second;
    }
    return std::nullopt;
}

VarDescriptions& var_descriptions()
{
    static VarDescriptions instance;
    static bool loaded{false};
    if (!loaded) {
        loaded = true;
        instance.load();
    }
    return instance;
}

} // namespace Environ::core
