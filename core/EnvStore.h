#pragma once

#include <optional>
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
    bool is_expandable;                          // REG_EXPAND_SZ vs REG_SZ
    std::optional<std::wstring> original_name;   // set when user renames

    // Populated by expand_and_validate()
    std::wstring expanded_value;
    std::vector<std::wstring> expanded_segments;
    std::vector<bool> segment_valid;

    // Populated by detect_duplicates() — non-empty string = duplicate description
    std::vector<std::wstring> segment_duplicate;
};

// Read all environment variables from the given registry scope.
// User  = HKCU\Environment
// Machine = HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Environment
// Returns sorted by name (case-insensitive).
std::vector<EnvVariable> read_variables(Scope scope);

// Expand environment variable references and validate path segments.
void expand_and_validate(std::vector<EnvVariable>& variables);

// Detect duplicate path segments across and within variables.
// Populates segment_duplicate with a message for each duplicate, empty if unique.
void detect_duplicates(std::vector<EnvVariable>& user_vars,
                       std::vector<EnvVariable>& machine_vars);

// Returns true if the current process is running elevated (admin).
bool is_elevated();

} // namespace Environ::core
