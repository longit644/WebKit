#include "ClipboardServiceW10M.h"

namespace WebKit {

// Bind borrowed ports; either missing port makes the service unusable.
ClipboardServiceW10M::ClipboardServiceW10M(std::unique_ptr<IServiceDispatcherW10M>&& dispatcher,
    std::unique_ptr<IClipboardTransportW10M>&& transport)
    : m_dispatcher(std::move(dispatcher))
    , m_transport(std::move(transport))
{
}

// Abandon pending work; late transport completions find no record and stop.
ClipboardServiceW10M::~ClipboardServiceW10M() = default;

// Enforce owning-thread access before touching the registry or any provider.
HRESULT ClipboardServiceW10M::checkThread() const
{
    return GetCurrentThreadId() == m_owner ? S_OK : RPC_E_WRONG_THREAD;
}

// Count operations still awaiting their single completion.
unsigned ClipboardServiceW10M::pendingCount() const
{
    std::lock_guard<std::mutex> lock(m_lock);
    return static_cast<unsigned>(m_operations.size());
}

// Count transport completions lost to a refused dispatcher enqueue.
unsigned ClipboardServiceW10M::droppedEnqueues() const
{
    std::lock_guard<std::mutex> lock(m_lock);
    return m_droppedEnqueues;
}

// Read the borrowed-table retain count for lifetime verification.
unsigned ClipboardServiceW10M::contextRefCount() const
{
    std::lock_guard<std::mutex> lock(m_lock);
    return m_contextRefs;
}

// Record one more borrowed-table retain; the owner destroys this instance.
void ClipboardServiceW10M::retain()
{
    std::lock_guard<std::mutex> lock(m_lock);
    if (m_contextRefs < 0xFFFFFFFFu)
        ++m_contextRefs;
}

// Release a previous retain without self-deleting; shutdown owns teardown.
void ClipboardServiceW10M::release()
{
    std::lock_guard<std::mutex> lock(m_lock);
    if (m_contextRefs)
        --m_contextRefs;
}

// Erase the record first, then invoke its single completion outside the lock.
HRESULT ClipboardServiceW10M::completeOperation(const std::shared_ptr<Operation>& operation,
    uint64_t operationId, WKW10MResult result, const std::string& text)
{
    auto owner = checkThread();
    if (FAILED(owner))
        return owner;
    std::shared_ptr<Operation> owned;
    {
        std::lock_guard<std::mutex> lock(m_lock);
        auto found = m_operations.find(operationId);
        if (found == m_operations.end() || found->second != operation)
            return S_FALSE;
        owned = found->second;
        m_operations.erase(found);
    }
    WKW10MString span { reinterpret_cast<const uint8_t*>(text.data()), static_cast<uint32_t>(text.size()) };
    owned->completion(owned->completionContext, operationId, result, &span);
    return S_OK;
}

// Validate and accept one request; writes finish inline, reads ride the dispatcher.
HRESULT ClipboardServiceW10M::requestClipboard(WKW10MPageHandle page, uint64_t operationId, uint32_t kind,
    const WKW10MString* text, WKW10MClipboardCompletion completion, void* completionContext)
{
    auto owner = checkThread();
    if (FAILED(owner))
        return owner;
    if (!completion || !m_dispatcher || !m_transport)
        return E_INVALIDARG;
    if (kind != WKW10MClipboardRead && kind != WKW10MClipboardWrite)
        return E_INVALIDARG;
    if (!text)
        return E_INVALIDARG;
    if (text->length && !text->data)
        return E_INVALIDARG;
    auto operation = std::make_shared<Operation>();
    operation->page = page;
    operation->kind = kind;
    operation->completion = completion;
    operation->completionContext = completionContext;
    {
        std::lock_guard<std::mutex> lock(m_lock);
        if (m_operations.find(operationId) != m_operations.end())
            return E_INVALIDARG;
        m_operations.emplace(operationId, operation);
    }
    if (kind == WKW10MClipboardWrite) {
        std::string bytes;
        if (text->length)
            bytes.assign(reinterpret_cast<const char*>(text->data), text->length);
        HRESULT written = m_transport->writeText(bytes);
        if (FAILED(written)) {
            completeOperation(operation, operationId, static_cast<WKW10MResult>(written), std::string());
            return written;
        }
        std::string empty;
        HRESULT completed = completeOperation(operation, operationId, WKW10MResultOK, empty);
        return FAILED(completed) ? completed : S_OK;
    }
    std::weak_ptr<Operation> weak = operation;
    m_transport->readText([this, weak, operationId](HRESULT result, std::string bytes) {
        auto record = weak.lock();
        if (!record)
            return;
        if (GetCurrentThreadId() == m_owner) {
            // The transport reports S_OK or a negative HRESULT, both valid results.
            completeOperation(record, operationId, static_cast<WKW10MResult>(result), bytes);
            return;
        }
        bool posted = m_dispatcher->post([this, record, operationId, result, text = std::move(bytes)]() mutable {
            completeOperation(record, operationId, static_cast<WKW10MResult>(result), text);
        });
        if (!posted) {
            std::lock_guard<std::mutex> lock(m_lock);
            ++m_droppedEnqueues;
        }
    });
    return S_OK;
}

// Cancel one accepted request with a synchronous Closed completion; unknown IDs fail.
HRESULT ClipboardServiceW10M::cancelClipboard(uint64_t operationId)
{
    auto owner = checkThread();
    if (FAILED(owner))
        return owner;
    std::shared_ptr<Operation> operation;
    {
        std::lock_guard<std::mutex> lock(m_lock);
        auto found = m_operations.find(operationId);
        if (found == m_operations.end())
            return E_INVALIDARG;
        operation = found->second;
    }
    std::string empty;
    HRESULT completed = completeOperation(operation, operationId, WKW10MResultClosed, empty);
    return FAILED(completed) ? completed : S_OK;
}

// Fill a borrowed table; postWake stays null until the runtime owner (7.5) exists.
HRESULT ClipboardServiceW10M::buildTable(WKW10MPlatformServices* table)
{
    auto owner = checkThread();
    if (FAILED(owner))
        return owner;
    if (!table)
        return E_INVALIDARG;
    table->header = { sizeof(*table), WKW10M_STRUCTURE_VERSION };
    table->context = this;
    table->retainContext = &retainThunk;
    table->releaseContext = &releaseThunk;
    table->requestClipboard = &requestThunk;
    table->cancelClipboard = &cancelThunk;
    table->postWake = nullptr;
    return S_OK;
}

// Retain routing; void per contract, so a null context is ignored.
void __stdcall ClipboardServiceW10M::retainThunk(void* context)
{
    if (!context)
        return;
    static_cast<ClipboardServiceW10M*>(context)->retain();
}

// Release routing; shutdown owns teardown, so this never self-deletes.
void __stdcall ClipboardServiceW10M::releaseThunk(void* context)
{
    if (!context)
        return;
    static_cast<ClipboardServiceW10M*>(context)->release();
}

// Request routing; S_OK becomes OK and failures keep their originating value.
WKW10MResult __stdcall ClipboardServiceW10M::requestThunk(void* context, WKW10MPageHandle page,
    uint64_t operationId, uint32_t kind, const WKW10MString* text,
    WKW10MClipboardCompletion completion, void* completionContext)
{
    if (!context)
        return WKW10MResultInvalidArgument;
    return static_cast<WKW10MResult>(static_cast<ClipboardServiceW10M*>(context)->requestClipboard(
        page, operationId, kind, text, completion, completionContext));
}

// Cancel routing with originating-HRESULT preservation.
WKW10MResult __stdcall ClipboardServiceW10M::cancelThunk(void* context, uint64_t operationId)
{
    if (!context)
        return WKW10MResultInvalidArgument;
    return static_cast<WKW10MResult>(
        static_cast<ClipboardServiceW10M*>(context)->cancelClipboard(operationId));
}

} // namespace WebKit

