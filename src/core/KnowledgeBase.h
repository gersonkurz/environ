#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>

namespace Environ::core {

// A user/community-curated source of per-variable knowledge: descriptions plus an
// optional classification override that forces a variable to be treated as a path-list
// or a scalar, regardless of the content-based heuristic in classify_variable().
class KnowledgeBase {
public:
    enum class ClassHint { None, ForcePath, ForceScalar };

    // Load and MERGE a TOML file into the current contents (later loads win per key),
    // so a shipped file can be layered under a user override. Returns false on parse error.
    bool load(std::string const& path);

    // Look up a variable description. Returns empty string if not found.
    // Case-insensitive lookup.
    std::wstring describe(std::wstring const& variable_name) const;

    // Classification override for a variable name, or None if unlisted (case-insensitive).
    ClassHint classify_override(std::wstring const& variable_name) const;

private:
    std::unordered_map<std::wstring, std::wstring> m_descriptions;
    std::unordered_set<std::wstring> m_force_path;   // lowercased names
    std::unordered_set<std::wstring> m_force_scalar; // lowercased names
};

} // namespace Environ::core
