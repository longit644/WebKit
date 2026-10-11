// Component entry points delegate to the MSVC-generated factory map, including
// parameterized activation and static-property interfaces. No public IID or XAML
// inheritance is reimplemented here.
#include <activation.h>
#include <cwchar>
#include <new>
#include <winstring.h>

// Serve only public activation classes through MSVC's actual generated factory map.
STDAPI DllGetActivationFactory(HSTRING classId, ::IActivationFactory** factory)
{
    if (!factory)
        return E_POINTER;
    *factory = nullptr;
    try {
        unsigned int length = 0;
        auto name = WindowsGetStringRawBuffer(classId, &length);
        if (!length)
            return CLASS_E_CLASSNOTAVAILABLE;
        // Internal-only constructors are absent from metadata. Also refuse
        // direct factory requests, regardless of compiler factory-map entries.
        // Exact activatable class name of the authored control.
        constexpr wchar_t controlName[] = L"WebKit.WebKitWebView";
        // Exact activatable class name of the application-owned options bag.
        constexpr wchar_t optionsName[] = L"WebKit.WebKitWebViewOptions";
        auto control = length == (sizeof(controlName) / sizeof(wchar_t) - 1) && !wmemcmp(name, controlName, length);
        auto options = length == (sizeof(optionsName) / sizeof(wchar_t) - 1) && !wmemcmp(name, optionsName, length);
        if (!control && !options)
            return CLASS_E_CLASSNOTAVAILABLE;
        auto result = Platform::Module::GetActivationFactory(ref new Platform::String(name, length));
        if (!result)
            return CLASS_E_CLASSNOTAVAILABLE;
        *factory = reinterpret_cast<::IActivationFactory*>(result);
        (*factory)->AddRef();
        return S_OK;
    } catch (Platform::Exception^ exception) {
        return exception->HResult;
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    } catch (...) {
        return E_FAIL;
    }
}

// Native callback code and cleanup tickets can outlive a visual control.
STDAPI DllCanUnloadNow() { return S_FALSE; }

#pragma warning(push)
#pragma warning(disable : 4447)
// Leave loader-lock initialization to the supported Store CRT/CX runtime.
BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }
#pragma warning(pop)
