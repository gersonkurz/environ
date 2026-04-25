#pragma once

#include <optional>
#include <string>
#include <unordered_map>

namespace Environ::core {

class VarDescriptions {
public:
    void load();
    std::optional<std::wstring> find(std::wstring_view name) const;

private:
    void load_file(std::wstring const& path);

    static std::wstring exe_dir();
    static std::wstring local_app_data_dir();

    std::unordered_map<std::wstring, std::wstring> m_map;
};

VarDescriptions& var_descriptions();

} // namespace Environ::core
