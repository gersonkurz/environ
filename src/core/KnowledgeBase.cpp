#include "precomp.h"
#include "KnowledgeBase.h"

namespace Environ::core {

namespace {

std::wstring to_lower_key(std::string_view utf8_name) {
    std::wstring name{pnq::unicode::to_utf16(std::string{utf8_name})};
    std::ranges::transform(name, name.begin(), ::towlower);
    return name;
}

} // namespace

bool KnowledgeBase::load(std::string const& path) {
    try {
        auto tbl{toml::parse_file(path)};

        // [variables] — name = "description". Merge; a later load overrides per key.
        if (auto variables{tbl["variables"]}; variables.is_table()) {
            for (auto&& [key, val] : *variables.as_table()) {
                if (val.is_string()) {
                    m_descriptions[to_lower_key(key.str())] =
                        pnq::unicode::to_utf16(std::string{*val.value<std::string>()});
                }
            }
        } else {
            spdlog::warn("Knowledge file missing [variables] section: {}", path);
        }

        // [classification] — path_like = [...], scalar = [...]. An explicit kind in one
        // list removes the name from the other, so a later (user) load wins cleanly.
        if (auto classification{tbl["classification"]}; classification.is_table()) {
            const auto ingest = [](toml::node_view<toml::node> arr,
                                   std::unordered_set<std::wstring>& into,
                                   std::unordered_set<std::wstring>& remove_from) {
                if (!arr.is_array()) return;
                for (auto&& elem : *arr.as_array()) {
                    if (auto s{elem.value<std::string>()}) {
                        auto key{to_lower_key(*s)};
                        remove_from.erase(key);
                        into.insert(std::move(key));
                    }
                }
            };
            ingest(classification["path_like"], m_force_path, m_force_scalar);
            ingest(classification["scalar"], m_force_scalar, m_force_path);
            ingest(classification["folder"], m_folders, m_files);
            ingest(classification["file"], m_files, m_folders);
        }

        spdlog::info("Knowledge from {}: {} descriptions, {} path / {} scalar, {} folder / {} file",
            path, m_descriptions.size(), m_force_path.size(), m_force_scalar.size(),
            m_folders.size(), m_files.size());
        return true;
    } catch (const toml::parse_error& err) {
        spdlog::error("Failed to parse knowledge file {}: {}", path, err.what());
        return false;
    }
}

KnowledgeBase::ClassHint KnowledgeBase::classify_override(std::wstring const& variable_name) const {
    std::wstring lower{variable_name};
    std::ranges::transform(lower, lower.begin(), ::towlower);
    if (m_force_path.contains(lower)) return ClassHint::ForcePath;
    if (m_force_scalar.contains(lower)) return ClassHint::ForceScalar;
    return ClassHint::None;
}

KnowledgeBase::PathRole KnowledgeBase::path_role(std::wstring const& variable_name) const {
    std::wstring lower{variable_name};
    std::ranges::transform(lower, lower.begin(), ::towlower);
    if (m_folders.contains(lower)) return PathRole::Folder;
    if (m_files.contains(lower)) return PathRole::File;
    return PathRole::None;
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
