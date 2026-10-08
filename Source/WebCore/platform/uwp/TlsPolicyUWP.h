#pragma once

#include <pal/SessionID.h>
#include <wtf/Forward.h>
#include <wtf/HashMap.h>
#include <wtf/NeverDestroyed.h>
#include <wtf/text/WTFString.h>

// Immutable per-session CA snapshots for the frozen V1 WKUWP contract (M10
// task 4.11): CA-file-only bundles frozen at first use, enforced per
// transfer without touching the mutable process trust singleton and without
// consulting the native OS store. All entry points run on the owner thread.
namespace WebCore {

// Immutable CA-file snapshot for one page session: the bundle path handed to
// curl plus the file bytes hashed at snapshot time for identity proof.
struct TlsBundleSnapshot {
    // CA bundle path as handed to curl; empty means no snapshot.
    String caPath;

    // Lowercase hex SHA-256 over the file bytes read at snapshot time.
    String sha256Hex;
};

// Immutable per-session CA snapshots with explicit teardown. Owns bundle
// identity and lifetime; enforcement reads snapshots without mutating them.
class TlsPolicyUWP {
public:
    // Snapshots the CA file for a session on first use; later paths are
    // ignored so concurrent controls cannot rebind each other's trust.
    // Returns false for an empty path or an unreadable file.
    static bool snapshotSessionBundle(PAL::SessionID, const String& caPath);

    // Snapshot for a session, or null when it never snapshotted one.
    static const TlsBundleSnapshot* snapshotFor(PAL::SessionID);

    // Drops a session's snapshot at teardown.
    static void eraseSession(PAL::SessionID);

    // Lowercase hex SHA-256 over bytes; shared digest helper for bundle and
    // font fingerprints. Empty string on backend failure.
    static String sha256Hex(std::span<const uint8_t>);

private:
    // Live snapshots by session; entries vanish at teardown, never by timeout.
    static HashMap<PAL::SessionID, TlsBundleSnapshot>& sessions();
};

} // namespace WebCore
