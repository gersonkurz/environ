#include "KnowledgeBase.h"

#include <pnq/unicode.h>
#include <spdlog/spdlog.h>
#include <toml++/toml.hpp>

#include <algorithm>

namespace Environ::core {

bool KnowledgeBase::load(std::string const& path) {
    try {
        auto tbl{toml::parse_file(path)};
        auto variables{tbl["variables"]};
        if (!variables.is_table()) {
            spdlog::warn("Knowledge file missing [variables] section: {}", path);
            return false;
        }

        for (auto&& [key, val] : *variables.as_table()) {
            if (val.is_string()) {
                auto name{pnq::unicode::to_utf16(std::string{key.str()})};
                auto desc{pnq::unicode::to_utf16(std::string{*val.value<std::string>()})};

                // Store with lowercase key for case-insensitive lookup
                std::wstring lower_name{name};
                std::ranges::transform(lower_name, lower_name.begin(), ::towlower);
                m_descriptions[std::move(lower_name)] = std::move(desc);
            }
        }

        spdlog::info("Loaded {} variable descriptions from {}", m_descriptions.size(), path);
        return true;
    } catch (const toml::parse_error& err) {
        spdlog::error("Failed to parse knowledge file {}: {}", path, err.what());
        return false;
    }
}

std::wstring KnowledgeBase::describe(std::wstring const& variable_name) const {
    std::wstring lower{variable_name};
    std::ranges::transform(lower, lower.begin(), ::towlower);
    auto it{m_descriptions.find(lower)};
    if (it != m_descriptions.end()) {
        return it->second;
    }
    return {};
}

} // namespace Environ::core
