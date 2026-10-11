#include "DispatcherW10M.h"

#include <new>

using namespace Platform;
using namespace Windows::UI::Core;

namespace WebKit {

// Retain the dispatcher; without one every later Post fails closed.
DispatcherW10M::DispatcherW10M(CoreDispatcher^ dispatcher)
{
    if (!dispatcher)
        throw ref new InvalidArgumentException();
    m_dispatcher = dispatcher;
}

// Read dispatcher affinity without touching scheduler or page state.
bool DispatcherW10M::HasThreadAccess()
{
    return !!m_dispatcher->HasThreadAccess;
}

// Enqueue one callback; a null handler or a lost dispatcher fails explicitly.
HRESULT DispatcherW10M::Post(DispatchedHandler^ handler)
{
    try {
        if (!handler)
            return E_INVALIDARG;
        m_dispatcher->RunAsync(CoreDispatcherPriority::Normal, handler);
        return S_OK;
    } catch (Exception^ exception) {
        return exception->HResult;
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    } catch (...) {
        return E_FAIL;
    }
}

} // namespace WebKit

