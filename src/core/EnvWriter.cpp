#include "EnvWriter.h"

#include <pnq/unicode.h>
#include <spdlog/spdlog.h>

#include <format>

#include <windows.h>

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
        std::wstring lower{original[i].name};
        for (auto& ch : lower) ch = towlower(ch);
        original_by_name[lower] = i;
    }

    // Track which originals are accounted for
    std::vector<bool> original_seen(original.size(), false);

    for (const auto& var : current) {
        // Check if this is a rename
        if (var.original_name.has_value()) {
            std::wstring lower_old{*var.original_name};
            for (auto& ch : lower_old) ch = towlower(ch);

            auto it{original_by_name.find(lower_old)};
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
        std::wstring lower{var.name};
        for (auto& ch : lower) ch = towlower(ch);

        auto it{original_by_name.find(lower)};
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

std::wstring apply_changes(Scope scope, std::vector<EnvChange> const& changes) {
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

void broadcast_environment_change() {
    DWORD_PTR result{0};
    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0,
                        reinterpret_cast<LPARAM>(L"Environment"),
                        SMTO_ABORTIFHUNG, 5000, &result);
    spdlog::info("Broadcast WM_SETTINGCHANGE for Environment");
}

} // namespace Environ::core
