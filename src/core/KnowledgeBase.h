#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Environ::core {

// A user/community-curated source of per-variable knowledge: descriptions plus an
// optional classification override that forces a variable to be treated as a path-list
// or a scalar, regardless of the content-based heuristic in classify_variable().
class KnowledgeBase {
public:
    enum class ClassHint { None, ForcePath, ForceScalar };

    // What a variable's value (or, for a path-list, each segment) points at. Drives the
    // browse affordance: a directory picker vs a file picker. None = no hint.
    enum class PathRole { None, Folder, File };

    // Load and MERGE a TOML file into the current contents (later loads win per key),
    // so a shipped file can be layered under a user override. Returns false on parse error.
    bool load(std::string const& path);

    // Look up a variable description. Returns empty string if not found.
    // Case-insensitive lookup.
    std::wstring describe(std::wstring const& variable_name) const;

    // Look up editability/usage guidance for a variable (the [notes] section) — e.g. why a
    // read-only variable can't be edited here. Empty if none. Case-insensitive.
    std::wstring note(std::wstring const& variable_name) const;

    // Classification override for a variable name, or None if unlisted (case-insensitive).
    ClassHint classify_override(std::wstring const& variable_name) const;

    // Whether the variable's value points at a folder or a file, or None if unknown
    // (case-insensitive). Folder and File are mutually exclusive.
    PathRole path_role(std::wstring const& variable_name) const;

    // Record a classification discovered at runtime (e.g. by probing what a value points
    // to on disk). Updates the in-memory tables so the current session reflects it, and
    // queues the name for save_learned(). No-op if the name is already classified.
    void learn_folder(std::wstring const& variable_name);
    void learn_file(std::wstring const& variable_name);
    void learn_path_like(std::wstring const& variable_name);

    // True if anything was learned since the last successful save_learned().
    bool has_learned() const;

    // Merge the queued learnings into the TOML file at `path` (preserving its existing
    // [variables]/[classification] entries), creating parent directories as needed, and
    // clear the queue on success. Comments in the file are not preserved. Returns false on
    // a write error; true (no-op) when nothing is queued.
    bool save_learned(std::string const& path);

private:
    std::unordered_map<std::wstring, std::wstring> m_descriptions;
    std::unordered_map<std::wstring, std::wstring> m_notes; // [notes]: editability guidance
    std::unordered_set<std::wstring> m_force_path;   // lowercased names
    std::unordered_set<std::wstring> m_force_scalar; // lowercased names
    std::unordered_set<std::wstring> m_folders;      // lowercased names
    std::unordered_set<std::wstring> m_files;        // lowercased names

    // Names learned at runtime since load (original case), pending persistence.
    std::vector<std::wstring> m_learned_folders;
    std::vector<std::wstring> m_learned_files;
    std::vector<std::wstring> m_learned_path;
};

} // namespace Environ::core
