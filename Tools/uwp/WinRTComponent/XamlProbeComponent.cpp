// XAML identity feasibility probe. Design: control-contract.md gate 5.
//
// Answers one question on Windows 10 Mobile 15254: can a native component
// present itself as a real XAML UserControl, constructable from XAML markup?
// The earlier WRL attempt (device 1.0.0.73-75) answered neither
// IContentControl nor inner-object QI, leaving open whether the platform forbids
// declarative construction or only that approach failed.
//
// Browser-free by design: no engine code is linked, so any failure is
// attributable to XAML identity rather than to WebCore or to the
// WebKitWebView prototype shape.
//
// Implementation shape, and why
// ----------------------------
// cppwinrt emits no XamlProbeT<T> template for this class (verified in
// winrt/impl/WebKitProbe.0.h), so deriving a local struct from the generated
// winrt::WebKitProbe::XamlProbe would not dispatch get_ProbeId: the generated
// impl::base binds its shim to the generated type. Instead this uses C++/WinRT
// composition: implements<IXamlProbe, UserControl> implements the probe
// interface and forwards every other interface request to a composed
// UserControl. That is exactly the delegation the WRL attempt failed to
// complete, which is why the earlier object answered neither IContentControl
// nor inner-object QI.
//
// Toolchain facts this target depends on are recorded in
// openspec/changes/wkview-m9-uwp-webview-navigation/xaml-probe-findings.md:
// cppwinrt and the C++/WinRT headers ship with the SDK, but C++/WinRT needs
// exceptions and C++20 enabled locally while the port builds without them.

#include <unknwn.h>
#include <wrl.h>

#include <winrt/WebKitProbe.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Media.h>
#include <winrt/Windows.UI.Xaml.h>
#include <winrt/base.h>

namespace WebKitProbe {

using namespace winrt;
using namespace winrt::Windows::UI::Xaml;
using namespace winrt::Windows::UI::Xaml::Controls;
using namespace winrt::Windows::UI::Xaml::Media;

// Identifier returned by get_ProbeId so a host can prove the activated object
// is this probe rather than another registered class.
constexpr uint32_t kProbeId = 0x0001U;

// Runtime class name the probe's activation factory serves.
constexpr wchar_t kProbeClassName[] = L"WebKitProbe.XamlProbe";

// Opaque ARGB fill for the probe's child so layout is also visible on screen.
// Channels are named because a packed literal would be an unexplained number.
constexpr uint8_t kProbeFillAlpha = 0xFF;
constexpr uint8_t kProbeFillRed = 0x30;
constexpr uint8_t kProbeFillGreen = 0x60;
constexpr uint8_t kProbeFillBlue = 0xC0;

// Size the probe requests from layout, in device-independent pixels. The host
// arranges a larger area and checks the measured size follows this request
// instead of collapsing to zero.
constexpr double kProbeRequestedWidth = 240.0;
constexpr double kProbeRequestedHeight = 160.0;

// Probe implementation base. cppwinrt would emit this as XamlProbeT<T> if the
// class were declared with an implementation stub; the port's MIDLRT dialect
// has no such attribute, so it is written out.
//
// implements<> takes the implementing type first and the interfaces to produce;
// the SDK UserControl is then a real base class rather than an implements<>
// argument, so its members (Content, Width, Height) and every inherited XAML
// interface come from the SDK implementation instead of being reimplemented
// here. Passing UserControl to implements<> instead would ask this type to
// implement the whole IUserControl surface, which is what made the probe fail
// to find Content.
template <typename T>
struct XamlProbeT : UserControl, winrt::implements<T, winrt::WebKitProbe::IXamlProbe> {
    // Required so the inherited base is default-constructible.
    XamlProbeT() = default;

    // winrt::implements deletes operator new so instances must come from
    // winrt::make<>. When this type also inherits a non-implements base,
    // heap_implements' allocation lookup finds that deleted declaration instead
    // of its own. Re-exposing the global allocator here gives make<> an
    // accessible one without changing the C++/WinRT allocation policy.
    static void* operator new(size_t size)
    {
        return ::operator new(size);
    }
};

// Probe object: a real SDK UserControl that additionally implements the probe's
// own interface. This is the shape the public control intends to use, so a
// successful build and activation means declarative XAML is reachable.
struct XamlProbe : XamlProbeT<XamlProbe> {
    // Creates a visible, measurable child on the inherited UserControl.
    XamlProbe()
    {
        auto fill = SolidColorBrush {};
        fill.Color(winrt::Windows::UI::ColorHelper::FromArgb(kProbeFillAlpha, kProbeFillRed, kProbeFillGreen, kProbeFillBlue));
        auto block = Border {};
        block.Background(fill);
        block.Width(kProbeRequestedWidth);
        block.Height(kProbeRequestedHeight);
        Content(block);
        Width(kProbeRequestedWidth);
        Height(kProbeRequestedHeight);
    }

    // Returns the fixed probe identifier; the host uses it to confirm identity.
    uint32_t get_ProbeId() const noexcept
    {
        return kProbeId;
    }
};

// Activation factory for the probe only. Implemented through WRL because that
// is the pattern already proven to compile for WebKitWebViewComponent.cpp in
// this toolchain; the C++/WinRT IActivationFactory projection expects a
// signature this base.h does not provide. Every other class ID is refused so
// the probe cannot be mistaken for a shipping component.
class XamlProbeFactory final : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::WinRtClassicComMix>, IActivationFactory> {
public:
    // Reports IActivationFactory as the factory's only interface.
    IFACEMETHODIMP GetIids(ULONG* count, IID** iids) override
    {
        if (!count || !iids)
            return E_POINTER;
        *count = 0;
        *iids = nullptr;
        return S_OK;
    }

    // Returns the runtime class name this factory serves.
    IFACEMETHODIMP GetRuntimeClassName(HSTRING* name) override
    {
        if (!name)
            return E_POINTER;
        return WindowsCreateString(kProbeClassName, static_cast<UINT32>(wcslen(kProbeClassName)), name);
    }

    // Reports BaseTrust because the probe uses only OS APIs.
    IFACEMETHODIMP GetTrustLevel(TrustLevel* level) override
    {
        if (!level)
            return E_POINTER;
        *level = BaseTrust;
        return S_OK;
    }

    // Creates one probe instance for the activator.
    IFACEMETHODIMP ActivateInstance(IInspectable** instance) override
    {
        if (!instance)
            return E_POINTER;
        *instance = winrt::make<XamlProbe>().as<IInspectable>().detach();
        return *instance ? S_OK : E_OUTOFMEMORY;
    }
};

} // namespace WebKitProbe

// Serves WebKitProbe.XamlProbe only. The class ID is compared against the
// raw buffer because constructing an hstring from an HSTRING is not part of
// this base.h's supported surface.
STDAPI DllGetActivationFactory(HSTRING classId, IActivationFactory** factory)
{
    if (!factory)
        return E_POINTER;
    *factory = nullptr;
    UINT32 length = 0;
    const wchar_t* requested = WindowsGetStringRawBuffer(classId, &length);
    if (!requested || length != static_cast<UINT32>(wcslen(WebKitProbe::kProbeClassName))
        || wcsncmp(requested, WebKitProbe::kProbeClassName, length) != 0)
        return CLASS_E_CLASSNOTAVAILABLE;
    return Microsoft::WRL::MakeAndInitialize<WebKitProbe::XamlProbeFactory>(factory);
}

// The probe must stay resident while the host runs its checks.
STDAPI DllCanUnloadNow()
{
    return S_FALSE;
}

BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID)
{
    return TRUE;
}