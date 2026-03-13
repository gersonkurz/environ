#pragma once

#include <string>
#include <vector>

namespace Environ::core {

enum class Scope { User, Machine };

struct EnvVariable {
    std::wstring name;
    std::wstring value;
    bool is_expandable;  // REG_EXPAND_SZ vs REG_SZ
};

// Read all environment variables from the given registry scope.
// User  = HKCU\Environment
// Machine = HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Environment
// Returns sorted by name (case-insensitive).
std::vector<EnvVariable> read_variables(Scope scope);

} // namespace Environ::core
