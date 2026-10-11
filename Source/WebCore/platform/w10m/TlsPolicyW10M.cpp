// Immutable per-session CA snapshots (M10 task 4.11); see TlsPolicyW10M.h.
#include "config.h"
#include "TlsPolicyW10M.h"

#include <pal/crypto/CryptoDigest.h>
#include <wtf/FileSystem.h>

namespace WebCore {

// See TlsPolicyW10M.h for the immutability contract.
bool TlsPolicyW10M::snapshotSessionBundle(PAL::SessionID sessionID, const String& caPath)
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

// See TlsPolicyW10M.h for the snapshot contract.
const TlsBundleSnapshot* TlsPolicyW10M::snapshotFor(PAL::SessionID sessionID)
{
    auto iterator = sessions().find(sessionID);
    if (iterator == sessions().end() || iterator->value.caPath.isEmpty())
        return nullptr;
    return &iterator->value;
}

// See TlsPolicyW10M.h for the teardown contract.
void TlsPolicyW10M::eraseSession(PAL::SessionID sessionID)
{
    sessions().remove(sessionID);
}

// See TlsPolicyW10M.h for the map contract.
HashMap<PAL::SessionID, TlsBundleSnapshot>& TlsPolicyW10M::sessions()
{
    static NeverDestroyed<HashMap<PAL::SessionID, TlsBundleSnapshot>> snapshots;
    return snapshots;
}

// See TlsPolicyW10M.h for the digest contract.
String TlsPolicyW10M::sha256Hex(std::span<const uint8_t> bytes)
{
    auto digest = PAL::Crypto::CryptoDigest::create(PAL::Crypto::CryptoDigest::Algorithm::SHA_256);
    if (!digest)
        return String();
    digest->addBytes(bytes);
    return digest->toHexString();
}

} // namespace WebCore

