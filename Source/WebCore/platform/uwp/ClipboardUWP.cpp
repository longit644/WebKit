// WinRT clipboard transport for WebKitWebViewUWP.
#include "config.h"
#include "ClipboardUWP.h"

#if PLATFORM(UWP)

#include <memory>
#include <wtf/MainThread.h>
#include <wtf/NeverDestroyed.h>
#include <wtf/RunLoop.h>
#include <windows.applicationmodel.datatransfer.h>
#include <wrl/client.h>
#include <wrl/event.h>
#include <wrl/wrappers/corewrappers.h>

namespace WebCore::ClipboardUWP {

namespace DataTransfer = ABI::Windows::ApplicationModel::DataTransfer;
namespace Foundation = ABI::Windows::Foundation;
using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Wrappers::HString;
using Microsoft::WRL::Wrappers::HStringReference;

static Function<bool(Function<void()>&&)>& uiDispatcher()
{
    static NeverDestroyed<Function<bool(Function<void()>&&)>> dispatcher;
    return dispatcher.get();
}

void setUIDispatcher(Function<bool(Function<void()>&&)>&& dispatcher)
{
    ASSERT(isMainThread());
    uiDispatcher() = WTF::move(dispatcher);
}

struct PendingClipboardOperation {
    Ref<RunLoop> origin;
    CompletionHandler<void(HRESULT, String)> completion;
};

static void deliver(const std::shared_ptr<PendingClipboardOperation>& pending, HRESULT result, const String& text)
{
    pending->origin->dispatch([pending, result, text = text.isolatedCopy()]() mutable {
        if (pending->completion)
            pending->completion(result, WTF::move(text));
    });
}

static void enqueue(Function<void(CompletionHandler<void(HRESULT, String)>&&)>&& operation, CompletionHandler<void(HRESULT, String)>&& completion)
{
    ASSERT(isMainThread());
    auto pending = std::make_shared<PendingClipboardOperation>(PendingClipboardOperation { Ref { RunLoop::currentSingleton() }, WTF::move(completion) });
    if (!uiDispatcher()) {
        deliver(pending, CO_E_NOTINITIALIZED, { });
        return;
    }
    bool accepted = uiDispatcher()([pending, operation = WTF::move(operation)]() mutable {
        operation(CompletionHandler<void(HRESULT, String)> { [pending](HRESULT result, String text) {
            deliver(pending, result, text);
        }, CompletionHandlerCallThread::AnyThread });
    });
    if (!accepted)
        deliver(pending, E_ABORT, { });
}

void clearAsync(CompletionHandler<void(HRESULT)>&& completion)
{
    enqueue([](auto&& done) { done(clear(), { }); }, [completion = WTF::move(completion)](HRESULT result, String) mutable { completion(result); });
}

void writeTextAsync(const String& text, CompletionHandler<void(HRESULT)>&& completion)
{
    enqueue([text = text.isolatedCopy()](auto&& done) { done(writeText(text), { }); }, [completion = WTF::move(completion)](HRESULT result, String) mutable { completion(result); });
}

void readTextAsync(CompletionHandler<void(HRESULT, String)>&& completion)
{
    enqueue([](auto&& done) { readText(WTF::move(done)); }, WTF::move(completion));
}

static HRESULT clipboardStatics(ComPtr<DataTransfer::IClipboardStatics>& clipboard)
{
    return RoGetActivationFactory(HStringReference(RuntimeClass_Windows_ApplicationModel_DataTransfer_Clipboard).Get(), IID_PPV_ARGS(clipboard.GetAddressOf()));
}

HRESULT clear()
{
    ComPtr<DataTransfer::IClipboardStatics> clipboard;
    HRESULT result = clipboardStatics(clipboard);
    return FAILED(result) ? result : clipboard->Clear();
}

HRESULT writeText(const String& text)
{
    ComPtr<DataTransfer::IClipboardStatics> clipboard;
    HRESULT result = clipboardStatics(clipboard);
    if (FAILED(result))
        return result;

    ComPtr<IInspectable> instance;
    result = RoActivateInstance(HStringReference(RuntimeClass_Windows_ApplicationModel_DataTransfer_DataPackage).Get(), instance.GetAddressOf());
    if (FAILED(result))
        return result;

    ComPtr<DataTransfer::IDataPackage> package;
    result = instance.As(&package);
    if (FAILED(result))
        return result;

    auto characters = text.wideCharacters();
    HString value;
    result = value.Set(characters.span().data(), text.length());
    if (FAILED(result))
        return result;
    result = package->SetText(value.Get());
    if (FAILED(result))
        return result;
    result = package->put_RequestedOperation(DataTransfer::DataPackageOperation_Copy);
    if (FAILED(result))
        return result;
    return clipboard->SetContent(package.Get());
}

void readText(CompletionHandler<void(HRESULT, String)>&& completion)
{
    ComPtr<DataTransfer::IClipboardStatics> clipboard;
    HRESULT result = clipboardStatics(clipboard);
    if (FAILED(result)) {
        completion(result, { });
        return;
    }

    ComPtr<DataTransfer::IDataPackageView> content;
    result = clipboard->GetContent(content.GetAddressOf());
    if (FAILED(result)) {
        completion(result, { });
        return;
    }

    boolean containsText = false;
    result = content->Contains(HStringReference(L"Text").Get(), &containsText);
    if (FAILED(result) || !containsText) {
        completion(FAILED(result) ? result : S_FALSE, { });
        return;
    }

    ComPtr<Foundation::IAsyncOperation<HSTRING>> operation;
    result = content->GetTextAsync(operation.GetAddressOf());
    if (FAILED(result)) {
        completion(result, { });
        return;
    }

    // The COM callback owns the completion until delivery; it never waits on
    // the UI thread. No capture of operation, so there is no reference cycle.
    auto pending = std::make_shared<CompletionHandler<void(HRESULT, String)>>(WTF::move(completion));
    auto callback = Microsoft::WRL::Callback<Foundation::IAsyncOperationCompletedHandler<HSTRING>>(
        [pending](Foundation::IAsyncOperation<HSTRING>* completed, Foundation::AsyncStatus status) -> HRESULT {
            HString value;
            HRESULT result = completed->GetResults(value.GetAddressOf());
            if (status == Foundation::AsyncStatus::Canceled && SUCCEEDED(result))
                result = E_ABORT;
            String text;
            if (SUCCEEDED(result)) {
                UINT32 length = 0;
                const wchar_t* characters = WindowsGetStringRawBuffer(value.Get(), &length);
                text = String(std::span<const char16_t>(reinterpret_cast<const char16_t*>(characters), length));
            }
            if (*pending)
                (*pending)(result, WTF::move(text));
            return S_OK;
        });
    if (!callback) {
        (*pending)(E_OUTOFMEMORY, { });
        return;
    }
    result = operation->put_Completed(callback.Get());
    if (FAILED(result) && *pending)
        (*pending)(result, { });
}

} // namespace WebCore::ClipboardUWP

#endif // PLATFORM(UWP)
