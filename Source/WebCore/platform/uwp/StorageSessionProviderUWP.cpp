// In-memory cookie session explicitly closed at host teardown (M10 task 4.7);
// see StorageSessionProviderUWP.h for cache-retained CookieJar ownership.
#include "config.h"
#include "StorageSessionProviderUWP.h"

namespace WebCore {

// Non-owning lookup for transport cookies, populated only by live sessions.
static HashMap<PAL::SessionID, NetworkStorageSession*>& storageSessionsUWP()
{
    static NeverDestroyed<HashMap<PAL::SessionID, NetworkStorageSession*>> sessions;
    return sessions;
}

// Returns the page-owned cookie session for this ephemeral ID, or null.
NetworkStorageSession* storageSessionUWP(PAL::SessionID sessionID)
{
    return storageSessionsUWP().get(sessionID);
}

// Creates the session and registers it for the page lifetime.
StorageSessionProviderUWP::StorageSessionProviderUWP(PAL::SessionID sessionID)
    : m_session(makeUnique<NetworkStorageSession>(sessionID))
{
    storageSessionsUWP().add(sessionID, m_session.get());
}

// Closes the session if its host has not already done so.
StorageSessionProviderUWP::~StorageSessionProviderUWP()
{
    close();
}

// See StorageSessionProviderUWP.h: cached resources may retain this provider,
// but neither transport lookup nor cookie storage survives explicit close.
void StorageSessionProviderUWP::close()
{
    if (!m_session)
        return;
    storageSessionsUWP().remove(m_session->sessionID());
    m_session.reset();
}

} // namespace WebCore
