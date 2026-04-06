#pragma once

#include "pch.h"
#include "../core/AppSettings.h"
#include "../core/KnowledgeBase.h"
#include "../core/SnapshotStore.h"

#include <memory>

#include "EnvironmentPage.h"
#include "HistoryPage.h"
#include "SettingsPage.h"

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
    winrt::Microsoft::UI::Xaml::Controls::NavigationView m_navView;
    std::unique_ptr<EnvironmentPage> m_envPage;
    std::unique_ptr<HistoryPage> m_historyPage;
    std::unique_ptr<SettingsPage> m_settingsPage;
    Environ::core::AppSettings m_settings;
    Environ::core::SnapshotStore m_snapshotStore;
    Environ::core::KnowledgeBase m_knowledgeBase;

    void RestoreWindowPlacement();
    void SaveWindowPlacement();
    void ApplyTheme();

    winrt::Microsoft::UI::Xaml::Controls::TextBlock MakePlaceholder(winrt::hstring const& text);
};
