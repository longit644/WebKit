#include "ClipboardPlatformUWP.h"

#include <agile.h>
#include <new>

using namespace Platform;
using namespace Windows::ApplicationModel::DataTransfer;
using namespace Windows::Foundation;
using namespace Windows::UI::Core;

namespace WebKit {
namespace {

// Strict UTF-8 validation shared by both transport directions.
bool validUtf8(const char* data, size_t length)
{
    size_t index = 0;
    while (index < length) {
        unsigned char lead = static_cast<unsigned char>(data[index]);
        size_t extra = 0;
        uint32_t minimum = 0;
        if (lead < 0x80) {
            ++index;
            continue;
        } else if ((lead >> 5) == 0x6) {
            extra = 1;
            minimum = 0x80;
            ++index;
        } else if ((lead >> 4) == 0xE) {
            extra = 2;
            minimum = 0x800;
            ++index;
        } else if ((lead >> 3) == 0x1E) {
            extra = 3;
            minimum = 0x10000;
            ++index;
        } else
            return false;
        if (index + extra > length)
            return false;
        uint32_t value = lead & ((1 << (6 - extra)) - 1);
        for (size_t i = 0; i < extra; ++i) {
            unsigned char trail = static_cast<unsigned char>(data[index++]);
            if ((trail >> 6) != 0x2)
                return false;
            value = (value << 6) | (trail & 0x3F);
        }
        if (value < minimum || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF))
            return false;
    }
    return true;
}

// Convert retained UTF-8 bytes into an immutable WinRT string.
String^ toPlatformString(const std::string& utf8)
{
    if (utf8.empty())
        return ref new String();
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (!count)
        throw Exception::CreateException(HRESULT_FROM_WIN32(GetLastError()));
    std::wstring wide(count, L'\0');
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()), &wide[0], count))
        throw Exception::CreateException(HRESULT_FROM_WIN32(GetLastError()));
    return ref new String(wide.data(), static_cast<unsigned int>(wide.size()));
}

// Convert a WinRT string into retained UTF-8 bytes.
std::string fromPlatformString(String^ value)
{
    if (!value || !value->Length())
        return { };
    int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value->Data(), value->Length(), nullptr, 0, nullptr, nullptr);
    if (!count)
        throw Exception::CreateException(HRESULT_FROM_WIN32(GetLastError()));
    std::string utf8(count, '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value->Data(), value->Length(), &utf8[0], count, nullptr, nullptr))
        throw Exception::CreateException(HRESULT_FROM_WIN32(GetLastError()));
    return utf8;
}

// Native CoreDispatcher hop behind completion delivery.
class CoreDispatcherPortUWP final : public IServiceDispatcherUWP {
public:
    // Retain the dispatcher wrapper; the service owner keeps it alive.
    explicit CoreDispatcherPortUWP(DispatcherUWP^ dispatcher) : m_dispatcher(dispatcher) { }

    // Report dispatcher affinity without running any task.
    bool hasThreadAccess() override { return !!m_dispatcher->HasThreadAccess(); }

    // Enqueue one native task through a weak dispatcher reference.
    bool post(std::function<void()>&& task) override
    {
        try {
            auto pending = std::make_shared<std::function<void()>>(std::move(task));
            WeakReference weak(m_dispatcher.Get());
            return SUCCEEDED(m_dispatcher->Post(ref new DispatchedHandler([weak, pending] {
                auto dispatcher = weak.Resolve<DispatcherUWP>();
                // A dead dispatcher at shutdown drops the task; teardown owns that path.
                if (!dispatcher)
                    return;
                (*pending)();
            })));
        } catch (...) {
            return false;
        }
    }

private:
    // Owning dispatcher wrapper retained for process lifetime by the caller.
    Agile<DispatcherUWP^> m_dispatcher;
};

// Native WinRT text transport behind clipboard reads and writes.
class ClipboardTransportUWP final : public IClipboardTransportUWP {
public:
    // Publish copied text synchronously on the owner thread.
    HRESULT writeText(const std::string& utf8) override
    {
        try {
            if (!validUtf8(utf8.data(), utf8.size()))
                return E_INVALIDARG;
            auto package = ref new DataPackage();
            package->SetText(toPlatformString(utf8));
            package->RequestedOperation = DataPackageOperation::Copy;
            Clipboard::SetContent(package);
            return S_OK;
        } catch (Exception^ exception) {
            return exception->HResult;
        } catch (const std::bad_alloc&) {
            return E_OUTOFMEMORY;
        } catch (...) {
            return E_FAIL;
        }
    }

    // Read through GetTextAsync; the completion runs on an unspecified thread.
    // A clipboard without text completes OK with an empty span: positive
    // HRESULTs other than S_OK are contract codes, so S_FALSE must never travel.
    void readText(std::function<void(HRESULT, std::string)> done) override
    {
        try {
            auto view = Clipboard::GetContent();
            if (!view->Contains(StandardDataFormats::Text)) {
                done(S_OK, { });
                return;
            }
            auto operation = view->GetTextAsync();
            auto pending = std::make_shared<std::function<void(HRESULT, std::string)>>(std::move(done));
            operation->Completed = ref new AsyncOperationCompletedHandler<String^>(
                [pending](IAsyncOperation<String^>^ completed, AsyncStatus status) {
                    HRESULT result = S_OK;
                    std::string text;
                    try {
                        auto value = completed->GetResults();
                        if (status == AsyncStatus::Canceled)
                            result = E_ABORT;
                        else
                            text = fromPlatformString(value);
                    } catch (Exception^ exception) {
                        result = exception->HResult;
                    } catch (...) {
                        result = E_FAIL;
                    }
                    (*pending)(result, std::move(text));
                });
        } catch (Exception^ exception) {
            done(exception->HResult, { });
        } catch (...) {
            done(E_FAIL, { });
        }
    }
};

} // namespace

// Create the WinRT text transport; the service marshals every read completion.
std::unique_ptr<IClipboardTransportUWP> CreateClipboardPlatformTransport()
{
    return std::make_unique<ClipboardTransportUWP>();
}

// Create the CoreDispatcher hop used for service completion delivery.
std::unique_ptr<IServiceDispatcherUWP> CreatePlatformServiceDispatcher(DispatcherUWP^ dispatcher)
{
    if (!dispatcher)
        throw ref new InvalidArgumentException();
    return std::make_unique<CoreDispatcherPortUWP>(dispatcher);
}

// Fill a borrowed platform-services table from a caller-owned service.
HRESULT BuildClipboardPlatformServices(ClipboardServiceUWP* service, WKUWPPlatformServices* table)
{
    if (!service)
        return E_INVALIDARG;
    return service->buildTable(table);
}

} // namespace WebKit
