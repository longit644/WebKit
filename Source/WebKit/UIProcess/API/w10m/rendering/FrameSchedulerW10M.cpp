#include "FrameSchedulerW10M.h"
#include "../native/EngineResultW10M.h"

#include <atomic>
#include <limits>

namespace WebKit {
namespace {

// Atomically select the sole owner before any process-shared pointer access.
std::atomic<DWORD> processServiceOwner { 0 };
// Native-only shared state; engine code is process-pinned and no XAML object is rooted here.
std::shared_ptr<OwnerThreadServicePumpW10M> processServicePump;

} // namespace

// Refuse a worker before any copied table/provider/client access.
HRESULT OwnerThreadServicePumpW10M::checkThread() const
{
    return GetCurrentThreadId() == m_owner ? S_OK : RPC_E_WRONG_THREAD;
}

// Require the complete negotiated table prefix and a usable process-service slot.
HRESULT OwnerThreadServicePumpW10M::validate(const WKW10MEngineAPI* api)
{
    if (!api) return E_INVALIDARG;
    if (api->header.structureSize < sizeof(*api) || api->header.structureVersion != WKW10M_STRUCTURE_VERSION
        || api->abiMajor != WKW10M_ABI_MAJOR_VERSION || !api->service)
        return HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH);
    return S_OK;
}

// Compare copied process operation identity rather than capabilities or table addresses.
bool OwnerThreadServicePumpW10M::matches(const WKW10MEngineAPI& api) const
{
    return m_bound && api.service == m_api.service && api.setSuspended == m_api.setSuspended;
}

// Copy the validated table once without calling initialization or creating a page.
HRESULT OwnerThreadServicePumpW10M::bind(const WKW10MEngineAPI* api)
{
    auto owner = checkThread();
    if (FAILED(owner)) return owner;
    auto result = validate(api);
    if (FAILED(result)) return result;
    if (m_bound) return matches(*api) ? S_OK : HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);
    m_api = *api;
    m_bound = true;
    return S_OK;
}

// Share exactly one process-engine binding while keeping invalid outputs untouched.
HRESULT OwnerThreadServicePumpW10M::acquire(const WKW10MEngineAPI* api, std::shared_ptr<OwnerThreadServicePumpW10M>& output)
{
    auto thread = GetCurrentThreadId();
    auto owner = processServiceOwner.load();
    if (owner && owner != thread) return RPC_E_WRONG_THREAD;
    auto result = validate(api);
    if (FAILED(result)) return result;
    if (!owner && !processServiceOwner.compare_exchange_strong(owner, thread) && owner != thread) return RPC_E_WRONG_THREAD;
    if (!processServicePump) processServicePump = std::make_shared<OwnerThreadServicePumpW10M>();
    result = processServicePump->bind(api);
    if (FAILED(result)) return result;
    output = processServicePump;
    return S_OK;
}

// A live instance contributes service demand even while its visual is unloaded.
HRESULT OwnerThreadServicePumpW10M::addClient()
{
    auto owner = checkThread();
    if (FAILED(owner)) return owner;
    if (!m_bound) return HRESULT_FROM_WIN32(ERROR_NOT_READY);
    if (m_clients == (std::numeric_limits<unsigned>::max)()) return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
    if (!m_clients) m_hasFrame = false;
    ++m_clients;
    return S_OK;
}

// Drop only the caller's lease; shared code/table ownership is process-pinned.
HRESULT OwnerThreadServicePumpW10M::removeClient()
{
    auto owner = checkThread();
    if (FAILED(owner)) return owner;
    if (!m_clients) return E_ILLEGAL_METHOD_CALL;
    --m_clients;
    return S_OK;
}

// Deduplicate the exact SDK epoch and retain the original result for all views.
HRESULT OwnerThreadServicePumpW10M::serviceForFrame(int64_t epoch)
{
    auto owner = checkThread();
    if (FAILED(owner)) return owner;
    if (epoch < 0) return E_INVALIDARG;
    if (m_servicing) { ++m_deferredServiceCount; return S_FALSE; }
    if (m_hasFrame && epoch < m_lastFrame) return S_FALSE;
    if (m_hasFrame && epoch == m_lastFrame) return m_frameResult;
    m_hasFrame = true;
    m_lastFrame = epoch;
    m_frameResult = serviceNow();
    return m_frameResult;
}

// Timer/wake work is independent of presentation and never recursively cycles the engine.
HRESULT OwnerThreadServicePumpW10M::serviceNow()
{
    auto owner = checkThread();
    if (FAILED(owner)) return owner;
    if (!m_bound) return HRESULT_FROM_WIN32(ERROR_NOT_READY);
    if (!m_clients || m_suspended) return S_FALSE;
    if (m_servicing) { ++m_deferredServiceCount; return S_FALSE; }
    m_servicing = true;
    ++m_serviceCount;
    auto result = engineResultToHRESULT(m_api.service());
    m_servicing = false;
    return result;
}

// Notify process suspension once and resume with a fresh frame epoch.
HRESULT OwnerThreadServicePumpW10M::setSuspended(bool suspended)
{
    auto owner = checkThread();
    if (FAILED(owner)) return owner;
    if (!m_bound) return HRESULT_FROM_WIN32(ERROR_NOT_READY);
    if (m_suspended == suspended) return S_OK;
    if (!m_api.setSuspended) return E_NOTIMPL;
    auto result = engineResultToHRESULT(m_api.setSuspended(suspended));
    if (FAILED(result)) return result;
    m_suspended = suspended;
    m_hasFrame = false;
    return S_OK;
}

// Ordinary explicit/native scope teardown releases the service lease once.
FrameSchedulerW10M::~FrameSchedulerW10M() { close(); }

// Reject foreign frame/visibility/close operations before touching the shared pump.
HRESULT FrameSchedulerW10M::checkThread() const
{
    return GetCurrentThreadId() == m_owner ? S_OK : RPC_E_WRONG_THREAD;
}

// Enter live visually paused state only after the shared lease was accepted.
HRESULT FrameSchedulerW10M::attach(const std::shared_ptr<OwnerThreadServicePumpW10M>& pump)
{
    auto owner = checkThread();
    if (FAILED(owner)) return owner;
    if (!m_closed) return E_ILLEGAL_METHOD_CALL;
    if (!pump) return E_INVALIDARG;
    auto result = pump->addClient();
    if (FAILED(result)) return result;
    m_pump = pump;
    m_closed = false;
    m_hasFrame = false;
    m_presenting = false;
    return S_OK;
}

// Visual unload retains the page lease and cannot pause resource clocks or peer work.
HRESULT FrameSchedulerW10M::setPresentationEnabled(bool enabled)
{
    auto owner = checkThread();
    if (FAILED(owner)) return owner;
    if (m_closed) return RO_E_CLOSED;
    if (m_presenting != enabled) m_hasFrame = false;
    m_presenting = enabled;
    return S_OK;
}

// Separate shared engine servicing from each eligible view's own frame callback.
HRESULT FrameSchedulerW10M::beginFrame(int64_t epoch)
{
    auto owner = checkThread();
    if (FAILED(owner)) return owner;
    if (m_closed) return RO_E_CLOSED;
    if (epoch < 0) return E_INVALIDARG;
    if (!m_presenting || m_pump->suspended() || (m_hasFrame && epoch <= m_lastFrame)) return S_FALSE;
    auto result = m_pump->serviceForFrame(epoch);
    if (result != S_OK) return result;
    if (m_closed || !m_presenting || m_pump->suspended()) return S_FALSE;
    m_hasFrame = true;
    m_lastFrame = epoch;
    ++m_frameCount;
    return S_OK;
}

// Close this participant while preserving the peer's process service and frame state.
HRESULT FrameSchedulerW10M::close()
{
    auto owner = checkThread();
    if (FAILED(owner)) return owner;
    if (m_closed) return S_OK;
    m_closed = true;
    m_presenting = false;
    auto result = m_pump->removeClient();
    m_pump.reset();
    return result;
}

} // namespace WebKit

