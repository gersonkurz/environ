#pragma once

#include "EnvStore.h"

#include <string>
#include <vector>

namespace Environ::core {

// Serialize environment variables to TOML format.
// REG_SZ → plain string, REG_EXPAND_SZ → { value = "...", expand = true }
std::string export_toml(
    std::vector<EnvVariable> const& user_vars,
    std::vector<EnvVariable> const& machine_vars);

} // namespace Environ::core
