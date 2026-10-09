#include "ContractPage.g.hpp"
#include "Diagnostics.h"
#include "pch.h"

#include <cstdio>
#include <string>

using namespace Platform;
using namespace Windows::Foundation;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Markup;

namespace WebKitCompiledProbe {

// Construct actual WebKit elements from XBF and retain no page/Loaded delegate cycle.
ContractPage::ContractPage()
{
    InitializeComponent();
    m_markupSourceAssigned = SourceView->Source && SourceView->Source->AbsoluteUri == L"https://example.test/compiled-target";
    SourceView->Source = nullptr;
    SourceView->Close();
    WeakReference weak(this);
    m_loaded = Loaded += ref new RoutedEventHandler([weak](Object^, RoutedEventArgs^) {
        auto page = weak.Resolve<ContractPage>();
        if (!page) return;
        page->Loaded -= page->m_loaded;
        try { page->RunChecks(); }
        catch (Exception^ exception) { page->Status->Text = L"WEBKIT COMPILED FAIL"; SaveContractFailure(exception->HResult, exception->Message); }
    });
}

// Assert generated public members, URI markup, binding and owner-thread lifetime behavior.
void ContractPage::RunChecks()
{
    unsigned checks = 0, failures = 0;
    std::string lines;
    // Each assertion is recorded independently; one failure fails the whole package run.
    auto check = [&](bool passed, const char* name) {
        ++checks;
        if (!passed) ++failures;
        lines += passed ? "PASS: " : "FAIL: ";
        lines += name;
        lines += "\r\n";
    };
    auto provider = safe_cast<IXamlMetadataProvider^>(Application::Current);
    auto type = provider->GetXamlType(WebKit::WebKitWebView::typeid);
    check(type && type->FullName == L"WebKit.WebKitWebView" && type->BaseType
        && type->BaseType->FullName == L"Windows.UI.Xaml.Controls.UserControl", "generated XAML type/base resolves new WebKit metadata");
    auto source = type ? type->GetMember(L"Source") : nullptr;
    auto title = type ? type->GetMember(L"DocumentTitle") : nullptr;
    auto loading = type ? type->GetMember(L"IsLoading") : nullptr;
    auto back = type ? type->GetMember(L"CanGoBack") : nullptr;
    check(source && !source->IsReadOnly && source->Type->FullName == L"Windows.Foundation.Uri", "compiled Source member is writable URI not historical HSTRING");
    check(title && title->IsReadOnly && loading && loading->IsReadOnly && back && back->IsReadOnly, "generated observable members are read-only in compiled provider");
    check(m_markupSourceAssigned, "compiled URI attribute invokes actual validated public Source setter");
    check(View && !View->Source && View->DocumentTitle->IsEmpty() && !View->IsLoading && !View->CanGoBack && !View->CanGoForward,
        "compiled null-Source element constructs real initial snapshot");
    check(TitleBinding->Text->IsEmpty() && !BackBinding->IsEnabled && !LoadingBinding->IsActive,
        "compiled title history loading bindings read actual public getters");
    auto control = safe_cast<Control^>(View);
    auto framework = safe_cast<FrameworkElement^>(View);
    auto element = safe_cast<UIElement^>(View);
    check(control && framework && element, "compiled outer object exposes inherited XAML interfaces");
    Size available;
    available.Width = 300; available.Height = 150;
    View->Measure(available);
    Rect bounds;
    bounds.X = 0; bounds.Y = 0; bounds.Width = 300; bounds.Height = 150;
    View->Arrange(bounds);
    check(View->DesiredSize.Width >= 260 && View->DesiredSize.Height >= 100 && View->ActualWidth >= 260 && View->ActualHeight >= 100,
        "compiled public element participates in measured XAML layout");
    auto original = ViewHost->Content;
    ViewHost->Content = nullptr;
    auto removed = !ViewHost->Content;
    ViewHost->Content = original;
    check(removed && ViewHost->Content == original && !View->IsLoading, "temporary remove reinsert preserves unclosed public control");
    auto options = ref new WebKit::WebKitWebViewOptions();
    check(options->MaximumConcurrentRequests == 8 && options->MaximumResourceBytes == 32ULL * 1024 * 1024
            && options->MaximumBufferedBytes == 16ULL * 1024 * 1024 && options->RequestTimeoutMilliseconds == 30000,
        "external generated WinMD consumer sees exact copied-option defaults");
    View->Close(); View->Close();
    check(!View->Source && View->DocumentTitle->IsEmpty() && !View->IsLoading, "compiled control Close is idempotent with readable final values");
    bool closed = false;
    try { View->Stop(); } catch (Exception^ exception) { closed = exception->HResult == RO_E_CLOSED; }
    check(closed, "external compiled consumer observes RO_E_CLOSED on mutation");
    unsigned expired = 0;
    // A bounded repeat oracle verifies explicit Close releases ordinary unattached objects.
    constexpr unsigned lifetimeIterations = 16;
    for (unsigned index = 0; index < lifetimeIterations; ++index) {
        WeakReference weak;
        {
            auto temporary = ref new WebKit::WebKitWebView();
            weak = temporary;
            temporary->Close();
        }
        if (!weak.Resolve<Object>()) ++expired;
    }
    check(expired == lifetimeIterations, "new metadata activation plus Close returns ordinary control lifetime to baseline");
    char header[256];
    sprintf_s(header, "version=" COMPILED_PROBE_VERSION "\r\nresult=%s\r\nchecks=%u\r\nfailures=%u\r\n", failures ? "FAIL" : "PASS", checks, failures);
    lines.insert(0, header);
    SaveContractReport(lines.c_str());
    Status->Text = failures ? L"WEBKIT COMPILED FAIL " COMPILED_PROBE_VERSION_W : L"WEBKIT COMPILED PASS " COMPILED_PROBE_VERSION_W;
}

} // namespace WebKitCompiledProbe
