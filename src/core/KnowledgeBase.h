#pragma once

#include <string>
#include <unordered_map>

namespace Environ::core {

class KnowledgeBase {
public:
    // Load from a TOML file. Returns false if file not found or parse error.
    bool load(std::string const& path);

    // Look up a variable description. Returns empty string if not found.
    // Case-insensitive lookup.
    std::wstring describe(std::wstring const& variable_name) const;

private:
    std::unordered_map<std::wstring, std::wstring> m_descriptions;
};

} // namespace Environ::core
