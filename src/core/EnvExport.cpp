#include "EnvExport.h"

#include <pnq/unicode.h>

#include <chrono>
#include <format>

namespace Environ::core {

namespace {

// TOML literal strings (single-quoted) need no backslash escaping,
// perfect for Windows paths. Only ' itself needs escaping (use multi-line).
// For simplicity, if a value contains ', we fall back to double-quoted.
void write_value(std::string& out, std::string const& value) {
    if (value.find('\'') == std::string::npos) {
        out += '\'';
        out += value;
        out += '\'';
    } else {
        // Double-quoted: must escape \ and "
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
        auto name{pnq::unicode::to_utf8(var.name)};
        auto value{pnq::unicode::to_utf8(var.value)};

        if (var.is_expandable) {
            out += '*';
        }
        out += name;
        out += " = ";
        write_value(out, value);
        out += '\n';
    }
}

} // namespace

std::string export_toml(
    std::vector<EnvVariable> const& user_vars,
    std::vector<EnvVariable> const& machine_vars) {

    auto now{std::chrono::system_clock::now()};
    std::string out;
    out += std::format("# Environ export \u2014 {:%F %T}\n",
                       std::chrono::floor<std::chrono::seconds>(now));
    out += "# Variables prefixed with * are REG_EXPAND_SZ\n\n";

    if (!user_vars.empty()) {
        write_section(out, "user", user_vars);
        out += '\n';
    }

    if (!machine_vars.empty()) {
        write_section(out, "machine", machine_vars);
    }

    return out;
}

} // namespace Environ::core
