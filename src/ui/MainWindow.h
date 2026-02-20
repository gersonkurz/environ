#pragma once

#include "pch.h"

struct MainWindow
    : winrt::Microsoft::UI::Xaml::WindowT<
          MainWindow,
          winrt::Microsoft::UI::Xaml::Markup::IXamlMetadataProvider> {

    MainWindow();

    void InitializeComponent();

    winrt::Microsoft::UI::Xaml::Markup::IXamlType
    GetXamlType(winrt::Windows::UI::Xaml::Interop::TypeName const& type);

    winrt::Microsoft::UI::Xaml::Markup::IXamlType
    GetXamlType(winrt::hstring const& fullname);

    winrt::com_array<winrt::Microsoft::UI::Xaml::Markup::XmlnsDefinition>
    GetXmlnsDefinitions();

private:
    winrt::Microsoft::UI::Xaml::XamlTypeInfo::XamlControlsXamlMetaDataProvider m_provider;
    winrt::Microsoft::UI::Xaml::Controls::TextBlock m_contentText;
};
