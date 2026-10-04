// XAML identity feasibility probe. Design: control-contract.md gate 5.
//
// Answers one question on Windows 10 Mobile 15254: can a native component
// built here present itself as a real XAML UserControl element? The earlier
// WRL composition attempt (device 1.0.0.73-75) answered neither
// IContentControl nor inner-object QI, which left open whether the platform
// forbids it or only that approach failed.
//
// This probe is deliberately browser-free: it links no engine code, so no
// failure can be blamed on WebCore or on the WebKitWebView prototype shape. It
// derives from the SDK UserControl through C++/WinRT, writes a visible child,
// and reports interface identity plus measured layout for a host to log.
//
// Toolchain findings so far (see also CMakeLists.txt):
//  - The SDK ships C++/WinRT projection headers under
//    Include/<ver>/cppwinrt/winrt, so Windows.UI.Xaml.Controls is usable.
//  - cppwinrt.exe IS installed in the SDK (x64 and arm), contrary to the
//    earlier design note, so an authoring projection for our own IDL can be
//    generated instead of hand-written.
//  - C++/WinRT requires exceptions (winrt/base.h throws) and C++20 (clang
//    rejects the SDK's <experimental/coroutine>). The port builds with
//    exceptions disabled, so a target using C++/WinRT must enable both
//    locally; it must not link engine code compiled the other way.
//
// Still open: whether a projection-generated class deriving from UserControl
// activates and lays out correctly on the phone. That needs a device run.
//
// STATUS: this implementation does NOT compile yet and is deliberately not
// wired into any build target (see CMakeLists.txt). It predates the working
// MIDLRT/cppwinrt pipeline and still contains a first-attempt hand-rolled
// factory with known errors (a non-virtual MeasureOverride, wrong HSTRING
// handling). It is kept only as a record of the WRL/C++/WinRT attempt. Reject
// it in favour of an implementation derived from the generated projection;
// see the "Remaining step" section of xaml-probe-findings.md.

#include <unknwn.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Media.h>
#include <winrt/Windows.UI.Xaml.h>
#include <winrt/base.h>

#include <algorithm>

namespace WebKitProbe {

using namespace winrt;
using namespace winrt::Windows::UI;
using namespace winrt::Windows::UI::Xaml;
using namespace winrt::Windows::UI::Xaml::Controls;
using namespace winrt::Windows::UI::Xaml::Media;

// Fixed width/height requested from layout, in device-independent pixels.
// The host compares measured size against these to prove layout ran.
constexpr double kProbeRequestedWidth = 240.0;
constexpr double kProbeRequestedHeight = 160.0;

// Background brush applied to the probe's own child so the host can also
// confirm visually that the control actually rendered something.
constexpr wchar_t kProbeColor[] = L"#FF3060C0";

// Native control derived from the SDK XAML UserControl. Deriving through
// C++/WinRT rather than WRL is the variable under test: WRL cannot express
// inheritance from a WinRT class without hand-implementing every inherited
// interface, which is the most likely reason the earlier attempt failed.
struct XamlProbe : UserControl {
    // Creates the child content in the constructor so the probe has something
    // measurable to lay out.
    XamlProbe()
    {
        auto block = Block {};
        block.Background(BrushConverter{}.ConvertFromString(kProbeColor).as<Brush>());
        Content(block);
    }

    // Requests a fixed size; the host arranges a larger area and checks that
    // the measured size follows this request rather than collapsing to zero.
    Windows::Foundation::Size MeasureOverride(Size const& available)
    {
        auto desired = Size { kProbeRequestedWidth, kProbeRequestedHeight };
        auto constrained = ::winrt::Windows::Foundation::Size {
            ::std::min(desired.Width, available.Width),
            ::std::min(desired.Height, available.Height) };
        return constrained;
    }
};

// Activation factory for the probe. Written by hand rather than generated
// because this toolchain has no cppwinrt.exe; the class deliberately
// implements no custom interface, so identity is judged only by QI for the
// inherited XAML interfaces this probe actually cares about.
struct XamlProbeFactory : winrt::implements<XamlProbeFactory, IActivationFactory> {
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable** instance) override
    {
        if (!instance)
            return E_POINTER;
        *instance = nullptr;
        try {
            *instance = winrt::make<XamlProbe>().as<IInspectable>().detach();
            return S_OK;
        } catch (winrt::hresult_error const& error) {
            return error.code();
        }
    }

    HRESULT STDMETHODCALLTYPE GetIids(ULONG* count, IID** iids) override
    {
        if (!count || !iids)
            return E_POINTER;
        *count = 0;
        *iids = nullptr;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HSTRING* name) override
    {
        if (!name)
            return E_POINTER;
        return winrt::put_hstring(name, L"WebKitProbe.XamlProbe");
    }

    HRESULT STDMETHODCALLTYPE GetTrustLevel(TrustLevel* level) override
    {
        if (!level)
            return E_POINTER;
        *level = BaseTrust;
        return S_OK;
    }
};

} // namespace WebKitProbe

// Serves WebKitProbe.XamlProbe only; every other class ID is unavailable so the
// probe cannot be mistaken for a shipping component.
STDAPI DllGetActivationFactory(HSTRING classId, IActivationFactory** factory)
{
    if (!factory)
        return E_POINTER;
    *factory = nullptr;
    if (winrt::compare_hstring(classId, L"WebKitProbe.XamlProbe") != 0)
        return CLASS_E_CLASSNOTAVAILABLE;
    *factory = winrt::make<WebKitProbe::XamlProbeFactory>().detach();
    return S_OK;
}

// The probe must stay resident for the duration of the host's checks.
STDAPI DllCanUnloadNow()
{
    return S_FALSE;
}

BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID)
{
    return TRUE;
}