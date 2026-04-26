#include "pch.h"
#include "AboutPage.xaml.h"
#if __has_include("AboutPage.g.cpp")
#include "AboutPage.g.cpp"
#endif

#include "Version.h"

namespace winrt::Environ::implementation
{
    AboutPage::AboutPage()
    {
        InitializeComponent();
        VersionText().Text(L"Version " ENVIRON_VERSION_WSTRING);
    }
}
