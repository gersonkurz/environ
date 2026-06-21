#include "precomp.h"
#include "EnvExport.h"

#include <algorithm>

namespace Environ::core {

namespace {

bool is_bare_key(std::string const& k) {
    if (k.empty()) return false;
    for (unsigned char c : k)
        if (!(std::isalnum(c) || c == '_' || c == '-')) return false;
    return true;
}

void write_key(std::string& out, std::string const& name) {
    if (is_bare_key(name)) {
        out += name;
    } else {
        // Quoted (basic) key: escape backslash and quote.
        out += '"';
        for (char ch : name) {
            if (ch == '\\' || ch == '"') out += '\\';
            out += ch;
        }
        out += '"';
    }
}

// TOML literal strings (single-quoted) need no backslash escaping -- perfect for Windows
// paths. Only ' itself can't appear, so fall back to an escaped basic string then.
void write_value(std::string& out, std::string const& value) {
    if (value.find('\'') == std::string::npos) {
        out += '\'';
        out += value;
        out += '\'';
    } else {
        out += '"';
        for (char ch : value) {
            if (ch == '\\') out += "\\\\";
            else if (ch == '"') out += "\\\"";
            else out += ch;
        }
        out += '"';
    }
}

void write_section(std::string& out, std::string_view section_name,
                   std::vector<EnvVariable> const& vars) {
    out += '[';
    out += section_name;
    out += "]\n";

    for (const auto& var : vars) {
        write_key(out, pnq::unicode::to_utf8(var.name));
        out += " = ";
        if (var.is_expandable) {
            out += "{ value = ";
            write_value(out, pnq::unicode::to_utf8(var.value));
            out += ", expand = true }";
        } else {
            write_value(out, pnq::unicode::to_utf8(var.value));
        }
        out += '\n';
    }
}

void parse_section(toml::node_view<toml::node> section, std::vector<EnvVariable>& out) {
    if (!section.is_table()) return;
    for (auto&& [key, val] : *section.as_table()) {
        std::wstring name{pnq::unicode::to_utf16(std::string{key.str()})};
        std::wstring value;
        bool expand{false};

        if (val.is_string()) {
            value = pnq::unicode::to_utf16(*val.value<std::string>());
        } else if (val.is_table()) {
            auto* t{val.as_table()};
            if (auto v{(*t)["value"].value<std::string>()})
                value = pnq::unicode::to_utf16(*v);
            expand = (*t)["expand"].value_or(false);
        } else {
            continue; // unsupported value shape
        }

        std::vector<std::wstring> segments;
        const EnvVariableKind kind{classify_variable(value, segments)};
        out.push_back(EnvVariable{
            .name{std::move(name)},
            .value{std::move(value)},
            .segments{std::move(segments)},
            .kind{kind},
            .is_expandable{expand},
        });
    }
}

} // namespace

std::string export_toml(
    std::vector<EnvVariable> const& user_vars,
    std::vector<EnvVariable> const& machine_vars) {

    auto now{std::chrono::system_clock::now()};
    std::string out;
    out += std::format("# Environ export — {:%F %T}\n",
                       std::chrono::floor<std::chrono::seconds>(now));
    out += "# Expandable (REG_EXPAND_SZ) vars use { value = '...', expand = true }\n\n";

    if (!user_vars.empty()) {
        write_section(out, "user", user_vars);
        out += '\n';
    }
    if (!machine_vars.empty()) {
        write_section(out, "machine", machine_vars);
    }
    return out;
}

std::optional<ImportResult> import_toml(std::string const& content) {
    try {
        auto tbl{toml::parse(content)};
        ImportResult result;
        parse_section(tbl["user"], result.user);
        parse_section(tbl["machine"], result.machine);
        return result;
    } catch (const toml::parse_error& err) {
        spdlog::error("Failed to parse TOML import: {}", err.description());
        return std::nullopt;
    }
}

std::vector<EnvVariable> merge_variables(
    std::vector<EnvVariable> base,
    std::vector<EnvVariable> const& incoming) {

    for (auto const& in : incoming) {
        auto it{std::ranges::find_if(base, [&](EnvVariable const& b) {
            return _wcsicmp(b.name.c_str(), in.name.c_str()) == 0;
        })};
        if (it != base.end()) *it = in; // overwrite value/kind/expandability
        else base.push_back(in);        // new variable
    }
    return base;
}

} // namespace Environ::core
