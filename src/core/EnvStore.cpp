#include "EnvStore.h"

#include <pnq/regis3.h>
#include <pnq/unicode.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cwctype>
#include <filesystem>

#include <windows.h>

namespace Environ::core {

namespace {

const char* registry_path(Scope scope) {
    switch (scope) {
    case Scope::User:
        return "HKEY_CURRENT_USER\\Environment";
    case Scope::Machine:
        return "HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment";
    }
    return "";
}

bool looks_like_path_segment(std::wstring_view segment) {
    if (segment.empty()) {
        return false;
    }

    // Environment variable reference: %USERPROFILE%, %SystemRoot%\foo
    if (segment.find(L'%') != std::wstring_view::npos) {
        return true;
    }

    // Contains a directory separator
    if (segment.find(L'\\') != std::wstring_view::npos || segment.find(L'/') != std::wstring_view::npos) {
        return true;
    }

    // Drive letter: C: or C:\...
    if (segment.size() >= 2 && std::iswalpha(segment[0]) != 0 && segment[1] == L':') {
        return true;
    }

    // Relative path starting with ./ or ../ (but not bare ".exe", ".COM", etc.)
    if (segment.starts_with(L"./") || segment.starts_with(L".\\") ||
        segment.starts_with(L"../") || segment.starts_with(L"..\\")) {
        return true;
    }

    return false;
}

std::vector<std::wstring> split_segments(std::wstring_view value) {
    std::vector<std::wstring> segments;
    std::wstring current_segment;

    for (const auto ch : value) {
        if (ch == L';') {
            if (!current_segment.empty()) {
                segments.push_back(current_segment);
                current_segment.clear();
            }
            continue;
        }

        current_segment.push_back(ch);
    }

    if (!current_segment.empty()) {
        segments.push_back(current_segment);
    }

    return segments;
}

EnvVariableKind classify_variable(std::wstring_view value, std::vector<std::wstring>& segments) {
    segments = split_segments(value);
    if (segments.size() <= 1) {
        return EnvVariableKind::Scalar;
    }

    std::size_t path_like_segments{0};
    for (const auto& segment : segments) {
        if (looks_like_path_segment(segment)) {
            ++path_like_segments;
        }
    }

    if (path_like_segments * 2 >= segments.size()) {
        return EnvVariableKind::PathList;
    }

    segments.clear();
    return EnvVariableKind::Scalar;
}

std::wstring expand_env_string(std::wstring const& input) {
    auto required{ExpandEnvironmentStringsW(input.c_str(), nullptr, 0)};
    if (required == 0) {
        return input;
    }
    std::wstring result(required, L'\0');
    ExpandEnvironmentStringsW(input.c_str(), result.data(), required);
    // Remove trailing null
    if (!result.empty() && result.back() == L'\0') {
        result.pop_back();
    }
    return result;
}

bool path_exists(std::wstring const& path) {
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

} // namespace

std::vector<EnvVariable> read_variables(Scope scope) {
    std::vector<EnvVariable> result;

    pnq::regis3::key reg{registry_path(scope)};
    if (!reg.open_for_reading()) {
        spdlog::warn("Failed to open registry key for {} scope",
                     scope == Scope::User ? "user" : "machine");
        return result;
    }

    for (const auto& val : reg.enum_values()) {
        if (val.is_default_value())
            continue;

        auto type{val.type()};
        if (!pnq::regis3::is_string_type(type))
            continue;

        auto name{pnq::unicode::to_utf16(val.name())};
        auto value{pnq::unicode::to_utf16(val.get_string())};
        std::vector<std::wstring> segments;
        auto kind{classify_variable(value, segments)};

        result.push_back(EnvVariable{
            .name{std::move(name)},
            .value{std::move(value)},
            .segments{std::move(segments)},
            .kind{kind},
            .is_expandable{type == REG_EXPAND_SZ},
        });
    }

    std::ranges::sort(result, [](const EnvVariable& a, const EnvVariable& b) {
        return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
    });

    spdlog::info("Read {} variables from {} scope",
                 result.size(), scope == Scope::User ? "user" : "machine");

    return result;
}

void expand_and_validate(std::vector<EnvVariable>& variables) {
    for (auto& var : variables) {
        var.expanded_value = var.is_expandable
            ? expand_env_string(var.value)
            : var.value;

        if (var.kind == EnvVariableKind::PathList) {
            var.expanded_segments.clear();
            var.segment_valid.clear();
            var.expanded_segments.reserve(var.segments.size());
            var.segment_valid.reserve(var.segments.size());

            for (const auto& seg : var.segments) {
                auto expanded{var.is_expandable ? expand_env_string(seg) : seg};
                var.segment_valid.push_back(path_exists(expanded));
                var.expanded_segments.push_back(std::move(expanded));
            }
        }
    }
}

void detect_duplicates(std::vector<EnvVariable>& user_vars,
                       std::vector<EnvVariable>& machine_vars) {
    // Build a map of expanded path → "Scope:VarName" for all path-list segments
    // Key: lowercased expanded path, Value: {scope label, var name, segment index}
    struct SegmentLocation {
        std::wstring scope;
        std::wstring var_name;
        std::size_t seg_index;
    };

    std::unordered_map<std::wstring, std::vector<SegmentLocation>> seen;

    auto process = [&](std::vector<EnvVariable>& vars, std::wstring const& scope_label) {
        for (auto& var : vars) {
            if (var.kind != EnvVariableKind::PathList) continue;

            var.segment_duplicate.resize(var.segments.size());

            for (std::size_t i{0}; i < var.expanded_segments.size(); ++i) {
                // Normalize: lowercase, remove trailing backslash
                auto key{var.expanded_segments[i]};
                std::ranges::transform(key, key.begin(), ::towlower);
                while (key.size() > 3 && (key.back() == L'\\' || key.back() == L'/')) {
                    key.pop_back();
                }

                seen[key].push_back(SegmentLocation{scope_label, var.name, i});
            }
        }
    };

    process(machine_vars, L"Machine");
    process(user_vars, L"User");

    // Now flag duplicates
    for (auto& [key, locations] : seen) {
        if (locations.size() < 2) continue;

        for (const auto& loc : locations) {
            // Find the other locations
            std::wstring others;
            for (const auto& other : locations) {
                if (&other == &loc) continue;
                if (!others.empty()) others += L", ";
                others += other.scope + L":" + other.var_name;
            }

            auto& vars{loc.scope == L"User" ? user_vars : machine_vars};
            for (auto& var : vars) {
                if (var.name == loc.var_name && loc.seg_index < var.segment_duplicate.size()) {
                    var.segment_duplicate[loc.seg_index] = L"duplicate in " + others;
                }
            }
        }
    }
}

bool is_elevated() {
    BOOL elevated{FALSE};
    HANDLE token{nullptr};
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        TOKEN_ELEVATION elevation{};
        DWORD size{sizeof(TOKEN_ELEVATION)};
        if (GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size)) {
            elevated = elevation.TokenIsElevated;
        }
        CloseHandle(token);
    }
    return elevated != FALSE;
}

} // namespace Environ::core
