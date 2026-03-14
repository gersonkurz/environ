#include "EnvStore.h"

#include <pnq/regis3.h>
#include <pnq/unicode.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cwctype>

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

} // namespace Environ::core
