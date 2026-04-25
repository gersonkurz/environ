#pragma once

#include "EnvStore.h"

#include <string>
#include <vector>

namespace Environ::core {

struct EnvChange {
    enum class Kind { Add, Modify, Delete, Rename };
    Kind kind;
    std::wstring name;         // current name (or new name for Rename)
    std::wstring old_name;     // only meaningful for Rename
    std::wstring value;
    bool is_expandable{false};

    // Human-readable description for dry-run display
    std::wstring describe() const;
};

struct ScopeApplyResult {
    Scope scope;
    std::vector<EnvChange> changes;
    std::wstring error;
    bool attempted{false};

    [[nodiscard]] bool has_changes() const;
    [[nodiscard]] bool succeeded() const;
};

struct ApplyResult {
    ScopeApplyResult user{
        .scope{Scope::User},
    };
    ScopeApplyResult machine{
        .scope{Scope::Machine},
    };
    bool broadcast_sent{false};

    [[nodiscard]] bool has_changes() const;
    [[nodiscard]] bool succeeded() const;
};

// Compare original and current variable lists, producing a minimal set of changes.
// Detects adds, deletes, modifications, and renames (via original_name field).
std::vector<EnvChange> compute_diff(
    std::vector<EnvVariable> const& original,
    std::vector<EnvVariable> const& current);

// Apply changes to the registry for the given scope.
// Returns empty string on success, or an error message on failure.
std::wstring apply_changes(Scope scope, std::vector<EnvChange> const& changes);

// Compute and apply document-level changes for both scopes.
// Machine changes require elevation; if not elevated, they are reported as an error and skipped.
ApplyResult apply_document_changes(
    std::vector<EnvVariable> const& original_user,
    std::vector<EnvVariable> const& current_user,
    std::vector<EnvVariable> const& original_machine,
    std::vector<EnvVariable> const& current_machine,
    bool is_elevated);

// Generate a human-readable summary label from a list of changes.
// E.g. "Modify GOPATH" or "Modify PATH, Add NEWVAR (+1 more)"
std::wstring summarize_changes(std::vector<EnvChange> const& changes);

// Broadcast WM_SETTINGCHANGE so other processes pick up environment changes.
void broadcast_environment_change();

} // namespace Environ::core
