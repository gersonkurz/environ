#include "precomp.h"
#include "FontPickerPanel.h"

namespace Environ::winui
{

using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace winrt::Microsoft::UI::Xaml::Input;
using namespace winrt::Microsoft::UI::Xaml::Media;

namespace
{

std::vector<std::wstring> enumerate_system_fonts()
{
    std::vector<std::wstring> families;

    winrt::com_ptr<IDWriteFactory> factory;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(factory.put()))))
        return families;

    winrt::com_ptr<IDWriteFontCollection> collection;
    if (FAILED(factory->GetSystemFontCollection(collection.put(), FALSE)) || !collection)
        return families;

    const UINT32 count{collection->GetFontFamilyCount()};
    families.reserve(count);

    for (UINT32 i{0}; i < count; ++i)
    {
        winrt::com_ptr<IDWriteFontFamily> family;
        if (FAILED(collection->GetFontFamily(i, family.put())) || !family) continue;

        winrt::com_ptr<IDWriteLocalizedStrings> names;
        if (FAILED(family->GetFamilyNames(names.put())) || !names) continue;

        UINT32 idx{0};
        BOOL exists{FALSE};
        names->FindLocaleName(L"en-us", &idx, &exists);
        if (!exists) idx = 0;

        UINT32 len{0};
        if (FAILED(names->GetStringLength(idx, &len)) || len == 0) continue;

        std::wstring name(len + 1, L'\0');
        if (SUCCEEDED(names->GetString(idx, name.data(), len + 1)))
        {
            name.resize(len);
            families.push_back(std::move(name));
        }
    }

    std::sort(families.begin(), families.end(), [](const std::wstring& a, const std::wstring& b) {
        return _wcsicmp(a.c_str(), b.c_str()) < 0;
    });
    families.erase(std::unique(families.begin(), families.end(), [](const std::wstring& a, const std::wstring& b) {
        return _wcsicmp(a.c_str(), b.c_str()) == 0;
    }), families.end());

    return families;
}

} // namespace

FontDialogContent create_font_dialog_content(
    const std::string& initial_font,
    std::function<void(const std::string& font_family)> on_preview)
{
    struct State
    {
        std::vector<std::wstring> fonts;
        std::shared_ptr<std::string> selected_font;
        ListView list_view{nullptr};
        std::function<void(const std::string&)> on_preview;
    };

    auto st{std::make_shared<State>()};
    st->fonts = enumerate_system_fonts();
    st->selected_font = std::make_shared<std::string>(initial_font.empty() ? "Segoe UI Variable Text" : initial_font);
    st->on_preview = std::move(on_preview);

    Grid root{};
    root.Width(420.0);
    root.Height(400.0);
    RowDefinition r0{}, r1{};
    r0.Height(GridLengthHelper::Auto());
    r1.Height(GridLengthHelper::FromValueAndType(1.0, GridUnitType::Star));
    root.RowDefinitions().Append(r0);
    root.RowDefinitions().Append(r1);
    root.RowSpacing(8.0);

    Grid top{};
    ColumnDefinition c0{}, c1{};
    c0.Width(GridLengthHelper::FromValueAndType(1.0, GridUnitType::Star));
    c1.Width(GridLengthHelper::Auto());
    top.ColumnDefinitions().Append(c0);
    top.ColumnDefinitions().Append(c1);
    Grid::SetRow(top, 0);

    TextBlock hint{};
    hint.Text(L"Press A\u2013Z to jump; grid previews live behind dialog.");
    hint.FontSize(12.0);
    hint.Opacity(0.75);
    hint.VerticalAlignment(VerticalAlignment::Center);
    Grid::SetColumn(hint, 0);

    Button def_btn{};
    def_btn.Content(winrt::box_value(L"Default font"));
    Grid::SetColumn(def_btn, 1);

    top.Children().Append(hint);
    top.Children().Append(def_btn);
    root.Children().Append(top);

    ListView lv{};
    lv.SelectionMode(ListViewSelectionMode::Single);
    Grid::SetRow(lv, 1);

    const std::wstring initial_w{pnq::unicode::to_utf16(*st->selected_font)};
    int initial_idx{-1};
    for (int i{0}; i < static_cast<int>(st->fonts.size()); ++i)
    {
        const auto& fam{st->fonts[static_cast<size_t>(i)]};
        if (_wcsicmp(fam.c_str(), initial_w.c_str()) == 0)
            initial_idx = i;

        TextBlock tb{};
        tb.Text(fam);
        tb.FontFamily(FontFamily{fam});
        tb.FontSize(13.5);
        tb.Padding(Thickness{2.0, 2.0, 2.0, 2.0});
        lv.Items().Append(tb);
    }

    st->list_view = lv;
    root.Children().Append(lv);

    lv.SelectionChanged([st](auto&&, SelectionChangedEventArgs const&)
    {
        const int idx{st->list_view.SelectedIndex()};
        if (idx < 0 || idx >= static_cast<int>(st->fonts.size())) return;
        *st->selected_font = pnq::unicode::to_utf8(st->fonts[static_cast<size_t>(idx)]);
        if (st->on_preview) st->on_preview(*st->selected_font);
    });

    def_btn.Click([st](auto&&, RoutedEventArgs const&)
    {
        *st->selected_font = "Segoe UI Variable Text";
        if (st->on_preview) st->on_preview(*st->selected_font);
        for (int i{0}; i < static_cast<int>(st->fonts.size()); ++i)
        {
            if (_wcsicmp(st->fonts[static_cast<size_t>(i)].c_str(), L"Segoe UI Variable Text") == 0)
            {
                st->list_view.SelectedIndex(i);
                st->list_view.ScrollIntoView(st->list_view.Items().GetAt(static_cast<uint32_t>(i)));
                break;
            }
        }
    });

    lv.PreviewKeyDown([st](auto&&, KeyRoutedEventArgs const& args)
    {
        const auto vk{args.Key()};
        if (vk >= winrt::Windows::System::VirtualKey::A && vk <= winrt::Windows::System::VirtualKey::Z)
        {
            args.Handled(true);
            const wchar_t ch{static_cast<wchar_t>(L'a' + (static_cast<int>(vk) - static_cast<int>(winrt::Windows::System::VirtualKey::A)))};
            const int n{static_cast<int>(st->fonts.size())};
            if (n == 0) return;
            const int start{std::max(0, st->list_view.SelectedIndex() + 1)};
            for (int off{0}; off < n; ++off)
            {
                const int idx{(start + off) % n};
                const auto& fam{st->fonts[static_cast<size_t>(idx)]};
                if (!fam.empty() && static_cast<wchar_t>(::towlower(fam[0])) == ch)
                {
                    st->list_view.SelectedIndex(idx);
                    st->list_view.ScrollIntoView(st->list_view.Items().GetAt(static_cast<uint32_t>(idx)));
                    break;
                }
            }
        }
    });

    if (initial_idx >= 0)
    {
        lv.SelectedIndex(initial_idx);
        lv.ScrollIntoView(lv.Items().GetAt(static_cast<uint32_t>(initial_idx)));
    }

    return FontDialogContent{root, st->selected_font};
}

} // namespace Environ::winui
