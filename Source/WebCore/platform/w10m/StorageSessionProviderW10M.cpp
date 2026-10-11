// In-memory cookie session explicitly closed at host teardown (M10 task 4.7);
// see StorageSessionProviderW10M.h for cache-retained CookieJar ownership.
#include "config.h"
#include "StorageSessionProviderW10M.h"

namespace WebCore {

// Non-owning lookup for transport cookies, populated only by live sessions.
static HashMap<PAL::SessionID, NetworkStorageSession*>& storageSessionsW10M()
{
    static NeverDestroyed<HashMap<PAL::SessionID, NetworkStorageSession*>> sessions;
    return sessions;
}

// Returns the page-owned cookie session for this ephemeral ID, or null.
NetworkStorageSession* storageSessionW10M(PAL::SessionID sessionID)
{
    return storageSessionsW10M().get(sessionID);
}

// Creates the session and registers it for the page lifetime.
StorageSessionProviderW10M::StorageSessionProviderW10M(PAL::SessionID sessionID)
    : m_session(makeUnique<NetworkStorageSession>(sessionID))
{
    storageSessionsW10M().add(sessionID, m_session.get());
}

// Closes the session if its host has not already done so.
StorageSessionProviderW10M::~StorageSessionProviderW10M()
{
    close();
}

// See StorageSessionProviderW10M.h: cached resources may retain this provider,
// but neither transport lookup nor cookie storage survives explicit close.
void StorageSessionProviderW10M::close()
{
    if (!m_session)
        return;
    storageSessionsW10M().remove(m_session->sessionID());
    m_session.reset();
}

} // namespace WebCore

