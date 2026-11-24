#include "precomp.h"
#include "ShellHelpers.h"

#include <shobjidl.h>
#include <filesystem>

namespace Environ::winui
{

namespace
{

void set_initial_folder(IFileDialog* dialog, const std::wstring& path)
{
    if (!dialog || path.empty()) return;

    std::wstring expanded(32768, L'\0');
    const DWORD n{ExpandEnvironmentStringsW(path.c_str(), expanded.data(), static_cast<DWORD>(expanded.size()))};
    if (n > 0 && n < expanded.size())
        expanded.resize(n - 1);
    else
        expanded = path;

    std::error_code ec;
    std::filesystem::path p{expanded};
    if (!std::filesystem::is_directory(p, ec))
        p = p.parent_path();
    if (p.empty() || !std::filesystem::is_directory(p, ec))
        return;

    winrt::com_ptr<IShellItem> folder;
    if (SUCCEEDED(SHCreateItemFromParsingName(p.c_str(), nullptr, IID_PPV_ARGS(folder.put()))))
        dialog->SetFolder(folder.get());
}

} // namespace

std::optional<std::wstring> browse_for_path(HWND owner, bool pick_folder, const std::wstring& initial_path)
{
    winrt::com_ptr<IFileOpenDialog> dialog;
    HRESULT hr{CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(dialog.put()))};
    if (FAILED(hr))
    {
        spdlog::error("CoCreateInstance(FileOpenDialog) failed: 0x{:08X}", static_cast<uint32_t>(hr));
        return std::nullopt;
    }

    DWORD options{0};
    if (SUCCEEDED(dialog->GetOptions(&options)))
    {
        options |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST;
        if (pick_folder)
            options |= FOS_PICKFOLDERS;
        else
            options |= FOS_FILEMUSTEXIST;
        dialog->SetOptions(options);
    }

    set_initial_folder(dialog.get(), initial_path);

    hr = dialog->Show(owner);
    if (FAILED(hr))
        return std::nullopt;

    winrt::com_ptr<IShellItem> item;
    if (FAILED(dialog->GetResult(item.put())) || !item)
        return std::nullopt;

    PWSTR raw_path{nullptr};
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &raw_path)) || !raw_path)
        return std::nullopt;

    std::wstring result{raw_path};
    CoTaskMemFree(raw_path);
    return result;
}

std::optional<std::wstring> browse_open_toml(HWND owner)
{
    winrt::com_ptr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(dialog.put()))))
        return std::nullopt;

    static constexpr COMDLG_FILTERSPEC kFilters[] = {
        {L"TOML files (*.toml)", L"*.toml"},
        {L"All files (*.*)", L"*.*"},
    };
    dialog->SetFileTypes(2, kFilters);
    dialog->SetTitle(L"Import Environment Variables");

    if (FAILED(dialog->Show(owner)))
        return std::nullopt;

    winrt::com_ptr<IShellItem> item;
    if (FAILED(dialog->GetResult(item.put())) || !item)
        return std::nullopt;

    PWSTR raw_path{nullptr};
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &raw_path)) || !raw_path)
        return std::nullopt;

    std::wstring result{raw_path};
    CoTaskMemFree(raw_path);
    return result;
}

std::optional<std::wstring> browse_save_toml(HWND owner)
{
    winrt::com_ptr<IFileSaveDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(dialog.put()))))
        return std::nullopt;

    static constexpr COMDLG_FILTERSPEC kFilters[] = {
        {L"TOML files (*.toml)", L"*.toml"},
        {L"All files (*.*)", L"*.*"},
    };
    dialog->SetFileTypes(2, kFilters);
    dialog->SetDefaultExtension(L"toml");
    dialog->SetFileName(L"environ-export.toml");
    dialog->SetTitle(L"Export Environment Variables");

    if (FAILED(dialog->Show(owner)))
        return std::nullopt;

    winrt::com_ptr<IShellItem> item;
    if (FAILED(dialog->GetResult(item.put())) || !item)
        return std::nullopt;

    PWSTR raw_path{nullptr};
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &raw_path)) || !raw_path)
        return std::nullopt;

    std::wstring result{raw_path};
    CoTaskMemFree(raw_path);
    return result;
}

bool reveal_in_explorer(const std::wstring& expanded_path)
{
    if (expanded_path.empty()) return false;

    std::error_code ec;
    const std::filesystem::path p{expanded_path};
    if (std::filesystem::is_directory(p, ec))
    {
        const HINSTANCE h{ShellExecuteW(nullptr, L"open", p.c_str(), nullptr, nullptr, SW_SHOWNORMAL)};
        return reinterpret_cast<intptr_t>(h) > 32;
    }

    if (std::filesystem::exists(p, ec))
    {
        const std::wstring args{L"/select,\"" + p.wstring() + L"\""};
        const HINSTANCE h{ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL)};
        return reinterpret_cast<intptr_t>(h) > 32;
    }

    return false;
}

bool relaunch_as_admin(HWND owner)
{
    wchar_t exe_path[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, exe_path, MAX_PATH) == 0)
        return false;

    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.hwnd = owner;
    sei.lpVerb = L"runas";
    sei.lpFile = exe_path;
    sei.nShow = SW_SHOWNORMAL;

    if (!ShellExecuteExW(&sei))
    {
        const DWORD err{GetLastError()};
        if (err != ERROR_CANCELLED)
            spdlog::error("ShellExecuteExW(runas) failed: {}", err);
        return false;
    }
    return true;
}

bool copy_text_to_clipboard(HWND owner, const std::wstring& text)
{
    if (!OpenClipboard(owner)) return false;
    EmptyClipboard();

    const size_t bytes{(text.size() + 1) * sizeof(wchar_t)};
    HGLOBAL hmem{GlobalAlloc(GMEM_MOVEABLE, bytes)};
    if (!hmem)
    {
        CloseClipboard();
        return false;
    }

    if (void* ptr{GlobalLock(hmem)})
    {
        std::memcpy(ptr, text.c_str(), bytes);
        GlobalUnlock(hmem);
        SetClipboardData(CF_UNICODETEXT, hmem);
    }
    else
    {
        GlobalFree(hmem);
    }
    CloseClipboard();
    return true;
}

std::wstring executable_directory()
{
    wchar_t buf[MAX_PATH]{};
    const DWORD len{GetModuleFileNameW(nullptr, buf, MAX_PATH)};
    if (len == 0) return L".";
    std::filesystem::path p{buf};
    return p.parent_path().wstring();
}

} // namespace Environ::winui
