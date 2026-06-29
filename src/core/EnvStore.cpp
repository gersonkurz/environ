#include "precomp.h"
#include "EnvStore.h"
#include "KnowledgeBase.h"


namespace Environ::core {

namespace {

const char* registry_path(Scope scope) {
    switch (scope) {
    case Scope::User:
        return "HKEY_CURRENT_USER\\Environment";
    case Scope::Machine:
        return "HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment";
    }
    return "";
}

bool looks_like_path_segment(std::wstring_view segment) {
    if (segment.empty()) {
        return false;
    }

    // Environment variable reference: %USERPROFILE%, %SystemRoot%\foo
    if (segment.find(L'%') != std::wstring_view::npos) {
        return true;
    }

    // Contains a directory separator
    if (segment.find(L'\\') != std::wstring_view::npos || segment.find(L'/') != std::wstring_view::npos) {
        return true;
    }

    // Drive letter: C: or C:\...
    if (segment.size() >= 2 && std::iswalpha(segment[0]) != 0 && segment[1] == L':') {
        return true;
    }

    // Relative path starting with ./ or ../ (but not bare ".exe", ".COM", etc.)
    if (segment.starts_with(L"./") || segment.starts_with(L".\\") ||
        segment.starts_with(L"../") || segment.starts_with(L"..\\")) {
        return true;
    }

    return false;
}

std::vector<std::wstring> split_segments(std::wstring_view value) {
    std::vector<std::wstring> segments;
    std::wstring current_segment;

    for (const auto ch : value) {
        if (ch == L';') {
            if (!current_segment.empty()) {
                segments.push_back(current_segment);
                current_segment.clear();
            }
            continue;
        }

        current_segment.push_back(ch);
    }

    if (!current_segment.empty()) {
        segments.push_back(current_segment);
    }

    return segments;
}

std::wstring expand_env_string(std::wstring const& input) {
    auto required{ExpandEnvironmentStringsW(input.c_str(), nullptr, 0)};
    if (required == 0) {
        return input;
    }
    std::wstring result(required, L'\0');
    ExpandEnvironmentStringsW(input.c_str(), result.data(), required);
    // Remove trailing null
    if (!result.empty() && result.back() == L'\0') {
        result.pop_back();
    }
    return result;
}

bool path_exists(std::wstring const& path) {
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

} // namespace

std::wstring preserve_env_form(std::wstring const& original, std::wstring const& picked) {
    // For each %VAR% reference in `original`, find the one whose expansion is the longest
    // path-prefix of `picked`, then re-apply that %VAR% form to picked. This keeps the
    // %USERPROFILE%-style relationship even when the user browses to a subfolder.
    std::wstring best_token;
    size_t best_len{0};

    for (size_t pos{0};;) {
        const size_t a{original.find(L'%', pos)};
        if (a == std::wstring::npos) break;
        const size_t b{original.find(L'%', a + 1)};
        if (b == std::wstring::npos) break;

        const std::wstring token{original.substr(a, b - a + 1)}; // "%VAR%"
        std::wstring expanded{expand_env_string(token)};
        if (expanded == token) { pos = b + 1; continue; } // undefined var: didn't expand
        while (!expanded.empty() && (expanded.back() == L'\\' || expanded.back() == L'/'))
            expanded.pop_back();

        // picked must start with expanded at a path boundary (whole match or next char a sep).
        const bool prefix{expanded.size() <= picked.size() &&
                          _wcsnicmp(picked.c_str(), expanded.c_str(),
                                    static_cast<int>(expanded.size())) == 0 &&
                          (picked.size() == expanded.size() ||
                           picked[expanded.size()] == L'\\' || picked[expanded.size()] == L'/')};
        if (prefix && expanded.size() > best_len) {
            best_len = expanded.size();
            best_token = token;
        }
        pos = b + 1;
    }

    if (best_token.empty()) return picked;            // no env reference matched
    return best_token + picked.substr(best_len);      // %VAR% + the remaining tail
}

EnvVariableKind classify_variable(std::wstring_view value, std::vector<std::wstring>& segments) {
    segments = split_segments(value);
    if (segments.size() <= 1) {
        return EnvVariableKind::Scalar;
    }

    std::size_t path_like_segments{0};
    for (const auto& segment : segments) {
        if (looks_like_path_segment(segment)) {
            ++path_like_segments;
        }
    }

    if (path_like_segments * 2 >= segments.size()) {
        return EnvVariableKind::PathList;
    }

    segments.clear();
    return EnvVariableKind::Scalar;
}

std::vector<EnvVariable> read_variables(Scope scope, const KnowledgeBase* kb) {
    std::vector<EnvVariable> result;

    pnq::regis3::key reg{registry_path(scope)};
    if (!reg.open_for_reading()) {
        spdlog::warn("Failed to open registry key for {} scope",
                     scope == Scope::User ? "user" : "machine");
        return result;
    }

    for (const auto& val : reg.enum_values()) {
        if (val.is_default_value())
            continue;

        auto type{val.type()};
        if (!pnq::regis3::is_string_type(type))
            continue;

        auto name{pnq::unicode::to_utf16(val.name())};
        auto value{pnq::unicode::to_utf16(val.get_string())};
        std::vector<std::wstring> segments;
        auto kind{classify_variable(value, segments)};

        // A knowledge-base classification override wins over the content heuristic.
        if (kb) {
            switch (kb->classify_override(name)) {
            case KnowledgeBase::ClassHint::ForcePath:
                if (kind != EnvVariableKind::PathList) {
                    kind = EnvVariableKind::PathList;
                    segments = split_segments(value);
                }
                break;
            case KnowledgeBase::ClassHint::ForceScalar:
                kind = EnvVariableKind::Scalar;
                segments.clear();
                break;
            case KnowledgeBase::ClassHint::None:
                break;
            }
        }

        result.push_back(EnvVariable{
            .name{std::move(name)},
            .value{std::move(value)},
            .segments{std::move(segments)},
            .kind{kind},
            .is_expandable{type == REG_EXPAND_SZ},
        });
    }

    std::ranges::sort(result, [](const EnvVariable& a, const EnvVariable& b) {
        return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
    });

    spdlog::info("Read {} variables from {} scope",
                 result.size(), scope == Scope::User ? "user" : "machine");

    return result;
}

std::vector<EnvVariable> read_process_extras(
    std::vector<EnvVariable> const& userVars,
    std::vector<EnvVariable> const& machineVars) {

    // Names already shown as User/Machine (case-insensitive); excluded here.
    std::unordered_set<std::wstring> persistent;
    const auto add_names = [&](std::vector<EnvVariable> const& vars) {
        for (auto const& v : vars) {
            persistent.insert(to_wlower(v.name));
        }
    };
    add_names(userVars);
    add_names(machineVars);

    std::vector<EnvVariable> result;
    LPWCH block{GetEnvironmentStringsW()};
    if (!block) return result;

    for (LPWCH p{block}; *p; p += wcslen(p) + 1) {
        std::wstring entry{p};
        if (entry.front() == L'=') continue; // skip "=C:=..." per-drive working-dir entries
        const auto eq{entry.find(L'=')};
        if (eq == std::wstring::npos || eq == 0) continue;

        std::wstring name{entry.substr(0, eq)};
        std::wstring value{entry.substr(eq + 1)};
        if (persistent.contains(to_wlower(name))) continue; // effective User/Machine row already (shadowing TBD)

        std::vector<std::wstring> segments;
        const EnvVariableKind kind{classify_variable(value, segments)};
        result.push_back(EnvVariable{
            .name{std::move(name)},
            .value{std::move(value)},
            .segments{std::move(segments)},
            .kind{kind},
            .is_expandable{false}, // process values are already expanded
        });
    }
    FreeEnvironmentStringsW(block);

    std::ranges::sort(result, [](const EnvVariable& a, const EnvVariable& b) {
        return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
    });
    spdlog::info("Read {} process-env extras", result.size());
    return result;
}

void learn_classifications(KnowledgeBase& kb, std::vector<EnvVariable>& variables) {
    for (auto& var : variables) {
        // Skip anything the knowledge base already classifies (shipped or learned earlier).
        if (kb.classify_override(var.name) != KnowledgeBase::ClassHint::None) continue;
        if (kb.path_role(var.name) != KnowledgeBase::PathRole::None) continue;

        const std::wstring expanded{expand_env_string(var.value)};
        if (expanded.empty()) continue;

        // A ';'-separated value whose first segment is an existing folder is a path-list.
        if (const auto semi{expanded.find(L';')}; semi != std::wstring::npos) {
            std::error_code ec;
            if (std::filesystem::is_directory(expanded.substr(0, semi), ec)) {
                if (var.kind != EnvVariableKind::PathList) { // new info the heuristic missed
                    kb.learn_path_like(var.name);
                    var.kind = EnvVariableKind::PathList;
                    var.segments = split_segments(var.value);
                }
                continue; // a path-list isn't a single folder/file target
            }
        }

        // A single value that is itself an existing folder or file.
        std::error_code ec;
        if (std::filesystem::is_directory(expanded, ec))
            kb.learn_folder(var.name);
        else if (std::filesystem::is_regular_file(expanded, ec))
            kb.learn_file(var.name);
    }
}

void expand_and_validate(std::vector<EnvVariable>& variables) {
    for (auto& var : variables) {
        var.expanded_value = expand_env_string(var.value);

        if (var.kind == EnvVariableKind::PathList) {
            var.expanded_segments.clear();
            var.segment_valid.clear();
            var.expanded_segments.reserve(var.segments.size());
            var.segment_valid.reserve(var.segments.size());

            for (const auto& seg : var.segments) {
                auto expanded{expand_env_string(seg)};
                var.segment_valid.push_back(path_exists(expanded));
                var.expanded_segments.push_back(std::move(expanded));
            }
        }
    }
}

void detect_duplicates(std::vector<EnvVariable>& user_vars,
                       std::vector<EnvVariable>& machine_vars) {
    // Build a map of expanded path → "Scope:VarName" for all path-list segments
    // Key: lowercased expanded path, Value: {scope label, var name, segment index}
    struct SegmentLocation {
        std::wstring scope;
        std::wstring var_name;
        std::size_t seg_index;
    };

    std::unordered_map<std::wstring, std::vector<SegmentLocation>> seen;

    auto process = [&](std::vector<EnvVariable>& vars, std::wstring const& scope_label) {
        for (auto& var : vars) {
            if (var.kind != EnvVariableKind::PathList) continue;

            var.segment_duplicate.resize(var.segments.size());

            for (std::size_t i{0}; i < var.expanded_segments.size(); ++i) {
                // Normalize: lowercase, remove trailing backslash
                auto key{to_wlower(var.expanded_segments[i])};
                while (key.size() > 3 && (key.back() == L'\\' || key.back() == L'/')) {
                    key.pop_back();
                }

                seen[key].push_back(SegmentLocation{scope_label, var.name, i});
            }
        }
    };

    process(machine_vars, L"Machine");
    process(user_vars, L"User");

    // Now flag duplicates
    for (auto& [key, locations] : seen) {
        if (locations.size() < 2) continue;

        for (const auto& loc : locations) {
            // Find the other locations
            std::wstring others;
            for (const auto& other : locations) {
                if (&other == &loc) continue;
                if (!others.empty()) others += L", ";
                others += other.scope + L":" + other.var_name;
            }

            auto& vars{loc.scope == L"User" ? user_vars : machine_vars};
            for (auto& var : vars) {
                if (var.name == loc.var_name && loc.seg_index < var.segment_duplicate.size()) {
                    var.segment_duplicate[loc.seg_index] = L"duplicate in " + others;
                }
            }
        }
    }
}

void detect_shadowed(std::vector<EnvVariable>& user_vars,
                     std::vector<EnvVariable>& machine_vars,
                     const KnowledgeBase& kb) {
    // Snapshot the effective process environment: lowercased name -> value.
    std::unordered_map<std::wstring, std::wstring> effective;
    if (LPWCH block{GetEnvironmentStringsW()}) {
        for (LPWCH p{block}; *p; p += wcslen(p) + 1) {
            std::wstring entry{p};
            if (entry.front() == L'=') continue; // "=C:=..." per-drive working-dir entries
            const auto eq{entry.find(L'=')};
            if (eq == std::wstring::npos || eq == 0) continue;
            effective.emplace(to_wlower(entry.substr(0, eq)), entry.substr(eq + 1));
        }
        FreeEnvironmentStringsW(block);
    }

    const auto mark = [&](std::vector<EnvVariable>& vars) {
        for (auto& v : vars) {
            v.shadowed = false;
            v.effective_value.clear();

            // Only KB-curated volatile/system-computed scalars are shadow-eligible. Composed
            // path-lists (Path, PSModulePath) and ordinary variables are never flagged.
            if (v.kind != EnvVariableKind::Scalar) continue;
            if (!kb.is_volatile(v.name)) continue;

            const auto it{effective.find(to_wlower(v.name))};
            if (it == effective.end()) continue;

            // Compare the EXPANDED persistent value, so a REG_EXPAND_SZ that expands to the
            // effective value (or a case-only difference) is not a false positive.
            if (_wcsicmp(v.expanded_value.c_str(), it->second.c_str()) != 0) {
                v.shadowed = true;
                v.effective_value = it->second;
            }
        }
    };
    mark(user_vars);
    mark(machine_vars);
}

std::wstring join_segments(std::vector<std::wstring> const& segments) {
    std::wstring result;
    for (std::size_t i{0}; i < segments.size(); ++i) {
        if (i != 0) {
            result.push_back(L';');
        }
        result += segments[i];
    }
    return result;
}

std::wstring apply_segment_edits(std::wstring const& original_value,
                                 std::vector<std::wstring> const& edited_segments) {
    // Split the original preserving structure (keep empty entries and a trailing separator).
    std::vector<std::wstring> raw_parts;
    std::wstring current;
    for (const auto ch : original_value) {
        if (ch == L';') {
            raw_parts.push_back(current);
            current.clear();
        } else {
            current.push_back(ch);
        }
    }
    raw_parts.push_back(current);

    std::size_t non_empty{0};
    for (const auto& part : raw_parts) {
        if (!part.empty()) {
            ++non_empty;
        }
    }
    if (non_empty != edited_segments.size()) {
        // Structure doesn't line up with the visible segments; normalize as a fallback.
        return join_segments(edited_segments);
    }

    std::size_t k{0};
    for (auto& part : raw_parts) {
        if (!part.empty()) {
            part = edited_segments[k++];
        }
    }

    std::wstring result;
    for (std::size_t i{0}; i < raw_parts.size(); ++i) {
        if (i != 0) {
            result.push_back(L';');
        }
        result += raw_parts[i];
    }
    return result;
}

bool is_elevated() {
    BOOL elevated{FALSE};
    HANDLE token{nullptr};
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        TOKEN_ELEVATION elevation{};
        DWORD size{sizeof(TOKEN_ELEVATION)};
        if (GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size)) {
            elevated = elevation.TokenIsElevated;
        }
        CloseHandle(token);
    }
    return elevated != FALSE;
}

} // namespace Environ::core
