#include "precomp.h"
#include "KnowledgeBase.h"

#include <fstream>

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

        // [notes] — name = "editability/usage guidance". Merged like [variables].
        if (auto notes{tbl["notes"]}; notes.is_table()) {
            for (auto&& [key, val] : *notes.as_table()) {
                if (val.is_string()) {
                    m_notes[to_lower_key(key.str())] =
                        pnq::unicode::to_utf16(std::string{*val.value<std::string>()});
                }
            }
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

namespace {

std::wstring lower(std::wstring s) {
    std::ranges::transform(s, s.begin(), ::towlower);
    return s;
}

} // namespace

void KnowledgeBase::learn_folder(std::wstring const& variable_name) {
    const std::wstring key{lower(variable_name)};
    if (m_folders.contains(key) || m_files.contains(key)) return; // role already known
    m_folders.insert(key);
    m_learned_folders.push_back(variable_name);
}

void KnowledgeBase::learn_file(std::wstring const& variable_name) {
    const std::wstring key{lower(variable_name)};
    if (m_folders.contains(key) || m_files.contains(key)) return; // role already known
    m_files.insert(key);
    m_learned_files.push_back(variable_name);
}

void KnowledgeBase::learn_path_like(std::wstring const& variable_name) {
    const std::wstring key{lower(variable_name)};
    if (m_force_path.contains(key) || m_force_scalar.contains(key)) return; // kind already known
    m_force_path.insert(key);
    m_learned_path.push_back(variable_name);
}

bool KnowledgeBase::has_learned() const {
    return !m_learned_folders.empty() || !m_learned_files.empty() || !m_learned_path.empty();
}

bool KnowledgeBase::save_learned(std::string const& path) {
    if (!has_learned()) return true;

    // Preserve any existing user-file content; start fresh if absent or unparseable.
    toml::table root;
    try {
        root = toml::parse_file(path);
    } catch (const toml::parse_error&) {
        root = toml::table{};
    }

    if (!root.contains("classification") || !root["classification"].is_table())
        root.insert_or_assign("classification", toml::table{});
    toml::table& classification{*root["classification"].as_table()};

    const auto merge = [&](const char* key, std::vector<std::wstring> const& names) {
        if (names.empty()) return;
        if (!classification.contains(key) || !classification[key].is_array())
            classification.insert_or_assign(key, toml::array{});
        toml::array& arr{*classification[key].as_array()};
        std::unordered_set<std::wstring> have;
        for (auto&& el : arr)
            if (auto s{el.value<std::string>()})
                have.insert(to_lower_key(*s));
        for (auto const& name : names) {
            const std::wstring lk{lower(name)};
            if (have.contains(lk)) continue;
            have.insert(lk);
            arr.push_back(pnq::unicode::to_utf8(name));
        }
    };
    merge("folder", m_learned_folders);
    merge("file", m_learned_files);
    merge("path_like", m_learned_path);

    const std::filesystem::path fp{pnq::unicode::to_utf16(path)};
    std::error_code ec;
    if (fp.has_parent_path())
        std::filesystem::create_directories(fp.parent_path(), ec);

    std::ofstream out{fp, std::ios::binary | std::ios::trunc};
    if (!out) {
        spdlog::error("Failed to open knowledge file for writing: {}", path);
        return false;
    }
    out << root;
    if (!out) {
        spdlog::error("Write error saving learned knowledge to {}", path);
        return false;
    }
    spdlog::info("Saved learned classifications to {}: {} folder, {} file, {} path-like",
                 path, m_learned_folders.size(), m_learned_files.size(), m_learned_path.size());
    m_learned_folders.clear();
    m_learned_files.clear();
    m_learned_path.clear();
    return true;
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

std::wstring KnowledgeBase::note(std::wstring const& variable_name) const {
    std::wstring lower{variable_name};
    std::ranges::transform(lower, lower.begin(), ::towlower);
    auto it{m_notes.find(lower)};
    if (it != m_notes.end()) {
        return it->second;
    }
    return {};
}

} // namespace Environ::core
