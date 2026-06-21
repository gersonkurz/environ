#pragma once

#include "EnvStore.h"

#include <optional>
#include <string>
#include <vector>

namespace Environ::core {

// Serialize environment variables to round-trippable TOML. Keys are quoted when not a
// legal bare key; REG_EXPAND_SZ is written as { value = '...', expand = true }, plain
// REG_SZ as a literal string.
std::string export_toml(
    std::vector<EnvVariable> const& user_vars,
    std::vector<EnvVariable> const& machine_vars);

struct ImportResult {
    std::vector<EnvVariable> user;
    std::vector<EnvVariable> machine;
};

// Parse the TOML produced by export_toml. Returns nullopt on parse error.
std::optional<ImportResult> import_toml(std::string const& content);

// Overlay `incoming` onto `base`: variables matching by name (case-insensitive) take the
// incoming value/kind/expandability; new names are appended; others are left untouched.
std::vector<EnvVariable> merge_variables(
    std::vector<EnvVariable> base,
    std::vector<EnvVariable> const& incoming);

} // namespace Environ::core
