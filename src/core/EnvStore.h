#pragma once

#include <string>
#include <vector>

namespace Environ::core {

enum class Scope { User, Machine };
enum class EnvVariableKind { Scalar, PathList };

struct EnvVariable {
    std::wstring name;
    std::wstring value;
    std::vector<std::wstring> segments;
    EnvVariableKind kind;
    bool is_expandable;  // REG_EXPAND_SZ vs REG_SZ

    // Populated by expand_and_validate()
    std::wstring expanded_value;
    std::vector<std::wstring> expanded_segments;
    std::vector<bool> segment_valid;
};

// Read all environment variables from the given registry scope.
// User  = HKCU\Environment
// Machine = HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Environment
// Returns sorted by name (case-insensitive).
std::vector<EnvVariable> read_variables(Scope scope);

// Expand environment variable references and validate path segments.
void expand_and_validate(std::vector<EnvVariable>& variables);

// Returns true if the current process is running elevated (admin).
bool is_elevated();

} // namespace Environ::core
