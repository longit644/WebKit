#pragma once

#include "NetworkStorageSession.h"
#include "StorageSessionProvider.h"
#include <memory>
#include <pal/SessionID.h>
#include <wtf/Forward.h>
#include <wtf/HashMap.h>
#include <wtf/NeverDestroyed.h>

// In-memory cookie session for one page. Cache-retained cookie jars may
// outlive the page, so the host explicitly closes the session at teardown.
namespace WebCore {

// Returns the page-owned cookie session for this ephemeral ID, or null.
NetworkStorageSession* storageSessionW10M(PAL::SessionID sessionID);

// Registered for one page lifetime. Closing unregisters and destroys the
// session independently of CookieJar references held by cached resources.
class StorageSessionProviderW10M final : public StorageSessionProvider {
public:
    // Creates the session and registers it for the page lifetime.
    explicit StorageSessionProviderW10M(PAL::SessionID sessionID);

    // Closes the session if its host has not already done so.
    ~StorageSessionProviderW10M() final;

    // Idempotent owner-thread teardown: unregisters the session and destroys
    // its in-memory cookie database even if a cached CookieJar retains us.
    void close();

    // Session handed to the cookie jar for this page; null after close.
    NetworkStorageSession* storageSession() const final { return m_session.get(); }

private:
    // Owned session backing DOM and transport cookies.
    std::unique_ptr<NetworkStorageSession> m_session;
};

} // namespace WebCore

