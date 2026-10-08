// Immutable per-session CA snapshots (M10 task 4.11); see TlsPolicyUWP.h.
#include "config.h"
#include "TlsPolicyUWP.h"

#include <pal/crypto/CryptoDigest.h>
#include <wtf/FileSystem.h>

namespace WebCore {

// See TlsPolicyUWP.h for the immutability contract.
bool TlsPolicyUWP::snapshotSessionBundle(PAL::SessionID sessionID, const String& caPath)
{
    if (caPath.isEmpty())
        return false;
    auto iterator = sessions().find(sessionID);
    if (iterator != sessions().end())
        return true;
    auto bytes = FileSystem::readEntireFile(caPath);
    if (!bytes || bytes->isEmpty())
        return false;
    std::span<const uint8_t> span { bytes->begin(), bytes->size() };
    sessions().set(sessionID, TlsBundleSnapshot { caPath, sha256Hex(span) });
    return true;
}

// See TlsPolicyUWP.h for the snapshot contract.
const TlsBundleSnapshot* TlsPolicyUWP::snapshotFor(PAL::SessionID sessionID)
{
    auto iterator = sessions().find(sessionID);
    if (iterator == sessions().end() || iterator->value.caPath.isEmpty())
        return nullptr;
    return &iterator->value;
}

// See TlsPolicyUWP.h for the teardown contract.
void TlsPolicyUWP::eraseSession(PAL::SessionID sessionID)
{
    sessions().remove(sessionID);
}

// See TlsPolicyUWP.h for the map contract.
HashMap<PAL::SessionID, TlsBundleSnapshot>& TlsPolicyUWP::sessions()
{
    static NeverDestroyed<HashMap<PAL::SessionID, TlsBundleSnapshot>> snapshots;
    return snapshots;
}

// See TlsPolicyUWP.h for the digest contract.
String TlsPolicyUWP::sha256Hex(std::span<const uint8_t> bytes)
{
    auto digest = PAL::Crypto::CryptoDigest::create(PAL::Crypto::CryptoDigest::Algorithm::SHA_256);
    if (!digest)
        return String();
    digest->addBytes(bytes);
    return digest->toHexString();
}

} // namespace WebCore
