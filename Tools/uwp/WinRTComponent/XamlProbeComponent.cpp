// XAML identity feasibility probe. Design: control-contract.md gate 5.
//
// This probe uses the C++/CX ARM32 authoring path deliberately. C++/WinRT's
// SDK base.h rejects 32-bit ARM, while C++/CX still provides a native
// UserControl projection through /ZW on the Windows 10 Mobile toolchain.
//
// Browser-free by design: no engine code is linked, so any failure is
// attributable to XAML identity rather than to WebCore or WebKitWebView.

#include <activation.h>
#include <cwchar>
#include <wrl.h>

using namespace Platform;
using namespace Windows::UI;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Media;

namespace WebKitProbe {

constexpr unsigned int kProbeId = 0x0001U;
constexpr wchar_t kProbeClassName[] = L"WebKitProbe.XamlProbe";
constexpr unsigned char kProbeFillAlpha = 0xFF;
constexpr unsigned char kProbeFillRed = 0x30;
constexpr unsigned char kProbeFillGreen = 0x60;
constexpr unsigned char kProbeFillBlue = 0xC0;
constexpr double kProbeRequestedWidth = 240.0;
constexpr double kProbeRequestedHeight = 160.0;

// Keep the C++/CX interface identity aligned with XamlProbe.idl. The runtime
// class then exposes this contract alongside the inherited UserControl chain.
[uuid(6d1c8f44-2b90-4a37-8c65-1f9d3b7a5e20)]
public interface class IXamlProbe {
    property unsigned int ProbeId {
        unsigned int get();
    }
};

// C++/CX emits the UserControl runtime identity and the default interface
// from this public ref class. The host uses ProbeId to confirm the instance.
public ref class XamlProbe sealed : UserControl, IXamlProbe {
public:
    XamlProbe()
    {
        auto fill = ref new SolidColorBrush();
        fill->Color = ColorHelper::FromArgb(kProbeFillAlpha, kProbeFillRed, kProbeFillGreen, kProbeFillBlue);
        auto block = ref new Border();
        block->Background = fill;
        block->Width = kProbeRequestedWidth;
        block->Height = kProbeRequestedHeight;
        Content = block;
        Width = kProbeRequestedWidth;
        Height = kProbeRequestedHeight;
    }

    virtual property unsigned int ProbeId {
        unsigned int get() { return kProbeId; }
    }
};

// WRL supplies the ABI activation-factory shell; the activated object itself
// is the C++/CX UserControl above.
class XamlProbeFactory final : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::WinRtClassicComMix>, ::IActivationFactory> {
public:
    // Reports no public interfaces: the factory itself exposes only
    // IActivationFactory, and the probe's own surface comes from its metadata.
    IFACEMETHODIMP GetIids(ULONG* count, IID** iids) override
    {
        if (!count || !iids)
            return E_POINTER;
        *count = 0;
        *iids = nullptr;
        return S_OK;
    }

    // Returns the single runtime class name this factory serves.
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

    // Creates one probe instance with a caller-owned reference.
    IFACEMETHODIMP ActivateInstance(IInspectable** instance) override
    {
        if (!instance)
            return E_POINTER;
        *instance = nullptr;
        XamlProbe^ probe = ref new XamlProbe();
        if (!probe)
            return E_OUTOFMEMORY;
        *instance = reinterpret_cast<IInspectable*>(probe);
        (*instance)->AddRef();
        return S_OK;
    }
};

} // namespace WebKitProbe

// Serves WebKitProbe.XamlProbe only; other class IDs are unavailable.
STDAPI DllGetActivationFactory(HSTRING classId, ::IActivationFactory** factory)
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

// The probe stays resident while the host runs its activation and layout checks.
STDAPI DllCanUnloadNow()
{
    return S_FALSE;
}

BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID)
{
    return TRUE;
}
