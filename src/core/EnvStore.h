#pragma once

#include <algorithm>
#include <cwctype>
#include <optional>
#include <string>
#include <vector>

namespace Environ::core {

class KnowledgeBase; // classification overrides; see read_variables

// Lowercase a wide string in place and return it (case-insensitive name keys).
inline std::wstring to_wlower(std::wstring s) {
    std::ranges::transform(s, s.begin(), ::towlower);
    return s;
}

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

    // A persistent (User/Machine) variable whose effective value is overridden by Windows at
    // sign-in (shadowed). `effective_value` holds the value actually in effect. Set by a
    // post-pass; only ever true for KB-volatile, non-composed names (see docs/plan-process-scope.md).
    bool shadowed{false};
    std::wstring effective_value;
};

// Read all environment variables from the given registry scope.
// User  = HKCU\Environment
// Machine = HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Environment
// Returns sorted by name (case-insensitive).
// If `kb` is non-null, its classification overrides win over the content heuristic
// (a variable forced to PathList/Scalar regardless of how its value looks).
std::vector<EnvVariable> read_variables(Scope scope, const KnowledgeBase* kb = nullptr);

// Read the effective process environment (GetEnvironmentStringsW) and return the variables that
// are NOT already represented by an effective User/Machine row — the read-only "process-env
// extras" (USERPROFILE, LOCALAPPDATA, ProgramFiles, ...). Pass the already-read persistent vars
// to avoid re-reading the registry. Sorted by name. (Shadow detection for persistent names whose
// effective value differs is a separate post-pass; see docs/plan-process-scope.md.)
std::vector<EnvVariable> read_process_extras(
    std::vector<EnvVariable> const& userVars,
    std::vector<EnvVariable> const& machineVars);

// Expand environment variable references and validate path segments.
void expand_and_validate(std::vector<EnvVariable>& variables);

// Learn folder/file/path-list classifications for variables the knowledge base does not
// already classify, by probing what each value points to on disk:
//   - a ';'-separated value whose first segment is an existing folder -> path-list;
//   - a value that is itself an existing folder / file -> that role.
// Updates `kb` in-memory (so the current session reflects it) and re-splits any value
// newly recognized as a path-list. Persist the learnings with KnowledgeBase::save_learned().
void learn_classifications(KnowledgeBase& kb, std::vector<EnvVariable>& variables);

// Detect duplicate path segments across and within variables.
// Populates segment_duplicate with a message for each duplicate, empty if unique.
void detect_duplicates(std::vector<EnvVariable>& user_vars,
                       std::vector<EnvVariable>& machine_vars);

// Flag persistent variables whose effective value is overridden by Windows at sign-in
// ("shadowed"). A row is flagged only when ALL hold: the name is in the KB volatile set,
// it is a Scalar (composed path-lists like Path are never flagged), and its EXPANDED value
// differs (case-insensitively) from the live process value. Sets `shadowed` + carries the
// `effective_value`. Requires expand_and_validate() to have run first.
void detect_shadowed(std::vector<EnvVariable>& user_vars,
                     std::vector<EnvVariable>& machine_vars,
                     const KnowledgeBase& kb);

// Join path-list segments back into a single ';'-separated value — the inverse of the
// segment split performed by read_variables. Kept here so split and join stay symmetric.
std::wstring join_segments(std::vector<std::wstring> const& segments);

// Re-serialize an edited path-list while preserving the original separator structure
// (empty/trailing entries that the display split drops). `edited_segments` are the visible
// (non-empty) segments in order; each replaces the corresponding non-empty entry of
// `original_value` in place. Falls back to join_segments() if the counts don't line up.
std::wstring apply_segment_edits(std::wstring const& original_value,
                                 std::vector<std::wstring> const& edited_segments);

// Classify a value as Scalar or PathList and split into segments (if path-list).
// Used by SnapshotStore to reconstruct variables from snapshot data.
EnvVariableKind classify_variable(std::wstring_view value,
                                   std::vector<std::wstring>& segments);

// Preserve a variable's %VAR% form across a folder/file browse: if `picked` (an absolute
// path from the picker) expands to the same location as `original`, return `original`
// unchanged so the %USERPROFILE%-style reference is kept; otherwise return `picked`.
std::wstring preserve_env_form(std::wstring const& original, std::wstring const& picked);

// Returns true if the current process is running elevated (admin).
bool is_elevated();

} // namespace Environ::core
