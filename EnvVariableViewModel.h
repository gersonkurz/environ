#pragma once

#include "EnvVariableViewModel.g.h"

namespace winrt::Environ::implementation
{
    struct EnvVariableViewModel : EnvVariableViewModelT<EnvVariableViewModel>
    {
        EnvVariableViewModel() = default;

        hstring Name() const { return m_name; }
        void Name(hstring const& value)
        {
            if (m_name != value) {
                m_name = value;
                RaisePropertyChanged(L"Name");
                RaisePropertyChanged(L"IsDirty");
            }
        }

        hstring OriginalName() const { return m_originalName; }
        void OriginalName(hstring const& value) { m_originalName = value; }

        hstring Value() const { return m_value; }
        void Value(hstring const& value)
        {
            if (m_value != value) {
                m_value = value;
                RaisePropertyChanged(L"Value");
                RaisePropertyChanged(L"IsDirty");
            }
        }

        hstring OriginalValue() const { return m_originalValue; }
        void OriginalValue(hstring const& value) { m_originalValue = value; }

        hstring Scope() const { return m_scope; }
        void Scope(hstring const& value) { m_scope = value; RaisePropertyChanged(L"Scope"); }

        hstring ScopeGlyph() const { return m_scopeGlyph; }
        void ScopeGlyph(hstring const& value) { m_scopeGlyph = value; }

        bool IsReadOnly() const { return m_isReadOnly; }
        void IsReadOnly(bool value) { m_isReadOnly = value; }

        double RowOpacity() const { return m_isReadOnly ? 0.45 : 1.0; }

        bool IsDirty() const { return m_value != m_originalValue || m_name != m_originalName; }

        bool IsPathSegment() const { return m_name.empty(); }

        // Name column is read-only for path segments (empty name) and read-only variables
        bool IsNameReadOnly() const { return m_isReadOnly || m_name.empty(); }

        // Name column is disabled entirely for path segments (prevents focus/cursor)
        bool IsNameEditable() const { return !m_name.empty(); }

        int32_t ValidationSeverity() const { return m_validationSeverity; }
        void ValidationSeverity(int32_t value)
        {
            if (m_validationSeverity != value) {
                m_validationSeverity = value;
                RaisePropertyChanged(L"ValidationSeverity");
            }
        }

        hstring ValidationTooltip() const { return m_validationTooltip; }
        void ValidationTooltip(hstring const& value)
        {
            if (m_validationTooltip != value) {
                m_validationTooltip = value;
                RaisePropertyChanged(L"ValidationTooltip");
            }
        }

        hstring Description() const { return m_description; }
        void Description(hstring const& value)
        {
            if (m_description != value) {
                m_description = value;
                RaisePropertyChanged(L"Description");
                RaisePropertyChanged(L"HasDescription");
            }
        }

        bool HasDescription() const { return !m_description.empty(); }

        // INotifyPropertyChanged
        winrt::event_token PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler)
        {
            return m_propertyChanged.add(handler);
        }
        void PropertyChanged(winrt::event_token const& token) noexcept
        {
            m_propertyChanged.remove(token);
        }

    private:
        void RaisePropertyChanged(hstring const& name)
        {
            m_propertyChanged(*this, Microsoft::UI::Xaml::Data::PropertyChangedEventArgs{name});
        }

        hstring m_name;
        hstring m_originalName;
        hstring m_value;
        hstring m_originalValue;
        hstring m_scope;
        hstring m_scopeGlyph;
        bool m_isReadOnly{false};
        int32_t m_validationSeverity{0};
        hstring m_validationTooltip;
        hstring m_description;
        winrt::event<Microsoft::UI::Xaml::Data::PropertyChangedEventHandler> m_propertyChanged;
    };
}

namespace winrt::Environ::factory_implementation
{
    struct EnvVariableViewModel : EnvVariableViewModelT<EnvVariableViewModel, implementation::EnvVariableViewModel>
    {
    };
}
