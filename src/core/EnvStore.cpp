#include "EnvStore.h"

#include <pnq/regis3.h>
#include <pnq/unicode.h>
#include <spdlog/spdlog.h>

#include <algorithm>

namespace Environ::core {

static const char* registry_path(Scope scope) {
    switch (scope) {
    case Scope::User:
        return "HKEY_CURRENT_USER\\Environment";
    case Scope::Machine:
        return "HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment";
    }
    return "";
}

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

        result.push_back(EnvVariable{
            .name{pnq::unicode::to_utf16(val.name())},
            .value{pnq::unicode::to_utf16(val.get_string())},
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
