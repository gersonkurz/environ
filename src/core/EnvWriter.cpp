
#include "precomp.h"
#include "EnvWriter.h"

namespace Environ::core {

namespace {

const wchar_t* registry_path_w(Scope scope) {
    switch (scope) {
    case Scope::User:
        return L"Environment";
    case Scope::Machine:
        return L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment";
    }
    return L"";
}

HKEY root_key(Scope scope) {
    return scope == Scope::User ? HKEY_CURRENT_USER : HKEY_LOCAL_MACHINE;
}

std::wstring set_registry_value(HKEY hkey, std::wstring const& name,
                                std::wstring const& value, bool is_expandable) {
    const auto type{is_expandable ? REG_EXPAND_SZ : REG_SZ};
    const auto byte_size{static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t))};
    auto result{RegSetValueExW(hkey, name.c_str(), 0, type,
                               reinterpret_cast<const BYTE*>(value.c_str()), byte_size)};
    if (result != ERROR_SUCCESS) {
        return std::format(L"Failed to set '{}': error {}", name, result);
    }
    return {};
}

std::wstring delete_registry_value(HKEY hkey, std::wstring const& name) {
    auto result{RegDeleteValueW(hkey, name.c_str())};
    if (result != ERROR_SUCCESS && result != ERROR_FILE_NOT_FOUND) {
        return std::format(L"Failed to delete '{}': error {}", name, result);
    }
    return {};
}

} // namespace

std::wstring EnvChange::describe() const {
    // Truncate long values for display
    auto truncate = [](std::wstring_view s, std::size_t max_len = 60) -> std::wstring {
        if (s.size() <= max_len) return std::wstring{s};
        return std::wstring{s.substr(0, max_len)} + L"\u2026";
    };

    switch (kind) {
    case Kind::Add:
        return std::format(L"Add '{}' = '{}'", name, truncate(value));
    case Kind::Modify:
        return std::format(L"Modify '{}' to '{}'", name, truncate(value));
    case Kind::Delete:
        return std::format(L"Delete '{}'", name);
    case Kind::Rename:
        return std::format(L"Rename '{}' \u2192 '{}' = '{}'", old_name, name, truncate(value));
    }
    return {};
}

bool ScopeApplyResult::has_changes() const {
    return !changes.empty();
}

bool ScopeApplyResult::succeeded() const {
    return error.empty();
}

bool ApplyResult::has_changes() const {
    return user.has_changes() || machine.has_changes();
}

bool ApplyResult::succeeded() const {
    return user.succeeded() && machine.succeeded();
}

std::wstring summarize_changes(std::vector<EnvChange> const& changes) {
    if (changes.empty()) return L"No changes";

    auto truncate = [](std::wstring_view s, std::size_t max_len = 40) -> std::wstring {
        if (s.size() <= max_len) return std::wstring{s};
        return std::wstring{s.substr(0, max_len)} + L"\u2026";
    };

    std::vector<std::wstring> parts;
    for (const auto& c : changes) {
        switch (c.kind) {
        case EnvChange::Kind::Add:
            parts.push_back(std::format(L"Add {}", c.name));
            break;
        case EnvChange::Kind::Modify:
            parts.push_back(std::format(L"Modify {}", c.name));
            break;
        case EnvChange::Kind::Delete:
            parts.push_back(std::format(L"Delete {}", c.name));
            break;
        case EnvChange::Kind::Rename:
            parts.push_back(std::format(L"Rename {} \u2192 {}", c.old_name, c.name));
            break;
        }
    }

    // For a single change, include the value
    if (parts.size() == 1) {
        const auto& c{changes[0]};
        if (c.kind == EnvChange::Kind::Modify) {
            return std::format(L"Modify {} to '{}'", c.name, truncate(c.value));
        }
        if (c.kind == EnvChange::Kind::Add) {
            return std::format(L"Add {} = '{}'", c.name, truncate(c.value));
        }
        return parts[0];
    }
    if (parts.size() == 2) return parts[0] + L", " + parts[1];
    return std::format(L"{}, {} (+{} more)", parts[0], parts[1], parts.size() - 2);
}

std::vector<EnvChange> compute_diff(
    std::vector<EnvVariable> const& original,
    std::vector<EnvVariable> const& current) {

    std::vector<EnvChange> changes;

    // Build a lookup from original names (case-insensitive) to their entries
    std::unordered_map<std::wstring, std::size_t> original_by_name;
    for (std::size_t i{0}; i < original.size(); ++i) {
        original_by_name[to_wlower(original[i].name)] = i;
    }

    // Track which originals are accounted for
    std::vector<bool> original_seen(original.size(), false);

    for (const auto& var : current) {
        // Check if this is a rename
        if (var.original_name.has_value()) {
            auto it{original_by_name.find(to_wlower(*var.original_name))};
            if (it != original_by_name.end()) {
                original_seen[it->second] = true;
                changes.push_back(EnvChange{
                    .kind{EnvChange::Kind::Rename},
                    .name{var.name},
                    .old_name{*var.original_name},
                    .value{var.value},
                    .is_expandable{var.is_expandable},
                });
                continue;
            }
        }

        // Look up by current name
        auto it{original_by_name.find(to_wlower(var.name))};
        if (it == original_by_name.end()) {
            // New variable
            changes.push_back(EnvChange{
                .kind{EnvChange::Kind::Add},
                .name{var.name},
                .value{var.value},
                .is_expandable{var.is_expandable},
            });
        } else {
            original_seen[it->second] = true;
            const auto& orig{original[it->second]};
            // Check if value or type changed
            if (orig.value != var.value || orig.is_expandable != var.is_expandable) {
                changes.push_back(EnvChange{
                    .kind{EnvChange::Kind::Modify},
                    .name{var.name},
                    .value{var.value},
                    .is_expandable{var.is_expandable},
                });
            }
        }
    }

    // Any original not seen in current is a delete
    for (std::size_t i{0}; i < original.size(); ++i) {
        if (!original_seen[i]) {
            changes.push_back(EnvChange{
                .kind{EnvChange::Kind::Delete},
                .name{original[i].name},
            });
        }
    }

    return changes;
}

std::wstring validate_variables(std::vector<EnvVariable> const& vars) {
    std::unordered_set<std::wstring> seen; // case-insensitive (lowercased) name keys
    for (const auto& v : vars) {
        if (v.name.empty()) {
            return L"A variable name is empty. Names cannot be blank.";
        }
        if (v.name.find(L'=') != std::wstring::npos) {
            return std::format(L"Variable name '{}' contains '=', which is not allowed.", v.name);
        }
        if (!seen.insert(to_wlower(v.name)).second) {
            return std::format(L"Two variables are named '{}' in the same scope.", v.name);
        }
    }
    return {};
}

std::wstring apply_changes(Scope scope, std::vector<EnvChange> const& changes) {
    // Defensive: never touch HKLM unelevated, even if called directly (not via
    // apply_document_changes, which also checks).
    if (scope == Scope::Machine && !is_elevated()) {
        return L"Machine (HKLM) changes require administrator elevation.";
    }

    HKEY hkey{nullptr};
    auto result{RegOpenKeyExW(root_key(scope), registry_path_w(scope),
                              0, KEY_SET_VALUE, &hkey)};
    if (result != ERROR_SUCCESS) {
        return std::format(L"Failed to open registry key: error {}", result);
    }

    std::wstring errors;

    for (const auto& change : changes) {
        std::wstring err;
        switch (change.kind) {
        case EnvChange::Kind::Add:
        case EnvChange::Kind::Modify:
            err = set_registry_value(hkey, change.name, change.value, change.is_expandable);
            break;
        case EnvChange::Kind::Delete:
            err = delete_registry_value(hkey, change.name);
            break;
        case EnvChange::Kind::Rename:
            err = delete_registry_value(hkey, change.old_name);
            if (err.empty()) {
                err = set_registry_value(hkey, change.name, change.value, change.is_expandable);
            }
            break;
        }
        if (!err.empty()) {
            spdlog::error("Registry write error: {}", pnq::unicode::to_utf8(err));
            if (!errors.empty()) errors += L"\n";
            errors += err;
        }
    }

    RegCloseKey(hkey);
    return errors;
}

ApplyResult apply_document_changes(
    std::vector<EnvVariable> const& original_user,
    std::vector<EnvVariable> const& current_user,
    std::vector<EnvVariable> const& original_machine,
    std::vector<EnvVariable> const& current_machine,
    bool const is_elevated) {

    ApplyResult result;

    // Defensive: refuse to write if any name is invalid (empty / contains '=' / duplicate),
    // even if a caller skipped the host-side check. No diffs, no writes, no broadcast.
    result.user.error = validate_variables(current_user);
    result.machine.error = validate_variables(current_machine);
    if (!result.succeeded()) {
        return result;
    }

    result.user.changes = compute_diff(original_user, current_user);
    result.machine.changes = compute_diff(original_machine, current_machine);

    auto any_writes_attempted{false};

    if (result.user.has_changes()) {
        result.user.attempted = true;
        result.user.error = apply_changes(Scope::User, result.user.changes);
        any_writes_attempted = true;
    }

    if (result.machine.has_changes()) {
        if (!is_elevated) {
            result.machine.error = L"Administrator privileges are required to apply machine variable changes.";
        } else {
            result.machine.attempted = true;
            result.machine.error = apply_changes(Scope::Machine, result.machine.changes);
            any_writes_attempted = true;
        }
    }

    if (any_writes_attempted) {
        broadcast_environment_change();
        result.broadcast_sent = true;
    }

    return result;
}

std::vector<std::wstring> build_diff_table(
    const wchar_t* left_label,
    const wchar_t* right_label,
    std::vector<EnvVariable> const& left_user,
    std::vector<EnvVariable> const& left_machine,
    std::vector<EnvVariable> const& right_user,
    std::vector<EnvVariable> const& right_machine) {

    struct TableRow { std::wstring name, left, right; };
    std::vector<TableRow> rows;

    const auto addScope = [&](const wchar_t* scope,
                               const std::vector<EnvVariable>& lv,
                               const std::vector<EnvVariable>& rv) {
        // Build name->variable maps.
        std::map<std::wstring, const EnvVariable*> leftMap, rightMap;
        for (const auto& v : lv) leftMap[to_wlower(v.name)] = &v;
        for (const auto& v : rv) rightMap[to_wlower(v.name)] = &v;

        // Collect all names, sorted case-insensitively.
        std::set<std::wstring> keys;
        for (const auto& [k, _] : leftMap) keys.insert(k);
        for (const auto& [k, _] : rightMap) keys.insert(k);

        for (const auto& key : keys)
        {
            auto itL{leftMap.find(key)};
            auto itR{rightMap.find(key)};
            const EnvVariable* left{itL != leftMap.end() ? itL->second : nullptr};
            const EnvVariable* right{itR != rightMap.end() ? itR->second : nullptr};

            // Skip unchanged.
            if (left && right && left->value == right->value) continue;

            const std::wstring dispName{std::wstring{scope} + L" " +
                (right ? right->name : left->name)};

            const bool leftPath{left && left->kind == EnvVariableKind::PathList && !left->segments.empty()};
            const bool rightPath{right && right->kind == EnvVariableKind::PathList && !right->segments.empty()};

            if (leftPath || rightPath)
            {
                // Path-list: show all segments side by side.
                const auto& lSegs{leftPath ? left->segments : std::vector<std::wstring>{}};
                const auto& rSegs{rightPath ? right->segments : std::vector<std::wstring>{}};
                const size_t maxSegs{std::max(lSegs.size(), rSegs.size())};

                rows.push_back({dispName,
                                left ? L"" : L"(not set)",
                                right ? L"" : L"(not set)"});
                for (size_t s{0}; s < maxSegs; ++s)
                {
                    rows.push_back({L"",
                                    s < lSegs.size() ? lSegs[s] : L"",
                                    s < rSegs.size() ? rSegs[s] : L""});
                }
            }
            else
            {
                // Scalar: one row.
                rows.push_back({dispName,
                                left ? left->value : L"(not set)",
                                right ? right->value : L"(not set)"});
            }
        }
    };

    addScope(L"[User]", left_user, right_user);
    addScope(L"[Machine]", left_machine, right_machine);

    if (rows.empty())
        return {L"  No differences"};

    // Compute column widths from content.
    size_t nameW{wcslen(L"Variable")};
    size_t leftW{wcslen(left_label)};
    size_t rightW{wcslen(right_label)};
    for (const auto& r : rows)
    {
        nameW = std::max(nameW, r.name.size());
        leftW = std::max(leftW, r.left.size());
        rightW = std::max(rightW, r.right.size());
    }
    // Cap to keep things reasonable; clipping handles overflow.
    nameW = std::min(nameW, size_t{26});
    leftW = std::min(leftW, size_t{52});
    rightW = std::min(rightW, size_t{52});

    const auto pad = [](const std::wstring& s, size_t w) {
        if (s.size() >= w) return s.substr(0, w);
        return s + std::wstring(w - s.size(), L' ');
    };

    std::vector<std::wstring> lines;
    lines.reserve(rows.size() + 2);

    // Header.
    lines.push_back(L" " + pad(L"Variable", nameW) + L" \x2502 " +
                     pad(std::wstring{left_label}, leftW) + L" \x2502 " +
                     pad(std::wstring{right_label}, rightW));
    // Separator.
    lines.push_back(L" " + std::wstring(nameW, L'\x2500') + L"\x2500\x253C\x2500" +
                     std::wstring(leftW, L'\x2500') + L"\x2500\x253C\x2500" +
                     std::wstring(rightW, L'\x2500'));
    // Data rows.
    for (const auto& r : rows)
    {
        lines.push_back(L" " + pad(r.name, nameW) + L" \x2502 " +
                         pad(r.left, leftW) + L" \x2502 " +
                         pad(r.right, rightW));
    }
    return lines;
}

void broadcast_environment_change() {
    DWORD_PTR result{0};
    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0,
                        reinterpret_cast<LPARAM>(L"Environment"),
                        SMTO_ABORTIFHUNG, 5000, &result);
    spdlog::info("Broadcast WM_SETTINGCHANGE for Environment");
}

} // namespace Environ::core
