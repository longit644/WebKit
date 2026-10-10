# M10 4.7 - Session/cookie isolation and explicit teardown

Status: PASS on Lumia 950 XL with WebKitProbe.EngineBrowser 1.0.0.18. All isolation and retained-jar teardown assertions passed; task 4.7 is checked.

## Existing device evidence

`WebKitProbe.EngineBrowser 1.0.0.16` on Lumia 950 XL, log `C:\Users\Longi\AppData\Local\Temp\opencode\m10-engine-47-run-v16.log` (146998 bytes), showed:

- Different ephemeral session IDs and independent DOM cookies in the two engine pages were already working in the isolation phases.
- `teardown-registered`, `scope-alive`, `scope-gone`, `teardown-unique`, `teardown-live`, and `teardown-intact` were all 1.
- `teardown-gone` and the combined `teardown` were 0: the retired peer's session remained registered after host destruction.
- `suite-completed: 1` was not sufficient evidence of a pass: the old runner continued after failed Boolean assertions.

Device Portal interruptions during these sessions were caused by device availability while the owner was away; they are separate from this teardown failure.

## Source-level diagnosis

The ownership chain extends beyond the Page:

```text
MemoryCache / other retained CachedResources
    -> CachedResource::m_cookieJar (RefPtr<const CookieJar>)
    -> CookieJar::m_storageSessionProvider (Ref<StorageSessionProvider>)
    -> StorageSessionProviderUWP::m_session (unique_ptr<NetworkStorageSession>)
```

- `Source/WebCore/loader/cache/CachedResource.h` retains the CookieJar.
- `Source/WebCore/loader/cache/MemoryCache.h` stores resources by session and retains dead resources; eviction does not necessarily destroy resources that other objects still reference.
- `Source/WebCore/loader/CookieJar.h` retains its storage provider.
- `Source/WebCore/page/Page.cpp` releases its own jar on destruction, but does not guarantee it was the last jar reference.

Consequently, `Page::hasOneRef()` cannot establish unique CookieJar/provider ownership. Waiting for the provider destructor ties session closure to cache or document lifetime rather than the control lifetime. This is a concrete source-level retention path. The v18 jar-sharing marker also reports 1 before the fixture deliberately retains additional references, confirming shared jar ownership on device despite unique Page ownership.

## Fix

- `StorageSessionProviderUWP::close()` is idempotent. It unregisters the transport lookup and destroys the NetworkStorageSession, including its in-memory SQLite cookie database. A cache-retained provider returns null from `storageSession()` after close. Its destructor calls close as a fallback.
- Both `WKCreatePageUWP` and the isolation peer retain the provider in their PageHostUWP, while CookieJar keeps its normal shared provider reference.
- `PageHostUWP` teardown expires deferred probes, stops frame loaders, detaches presentation, releases the Page, evicts only that ephemeral session's resource cache, and explicitly closes the provider. It keeps the provider alive through Page destruction and eviction.
- The teardown fixture deliberately retains a CookieJar and provider while deleting the peer host. It checks the Page through a non-retaining WeakPtr, registry removal, actual storage closure, idempotent close, and the live control's remaining cookie. A failed combined teardown now ends the suite with `suite-completed: 0`.

## Supported ephemeral storage

Each persistent engine page gets a fresh ephemeral PAL SessionID. The curl NetworkStorageSession selects SQLite `:memory:` for that ID. DOM cookies and HTTP response/request cookies share this page's database; distinct controls use distinct databases. Closing the host destroys that database regardless of cached jar references. Resources are cached in memory by session and the retired session is evicted at host teardown. This evidence covers cookies and resource-cache lifetime; it does not establish support for other web storage APIs or session-specific TLS configuration (task 4.11).

## Host verification

### Build dependency correction

The first fixed package, v1.0.0.17, stopped before the suite with an access violation at address 0. Its log is `m10-engine-47-run-v17-progress1.log`. Inspection found that Ninja's recorded dependencies for ChromeClientUWP and TransientPageUWP omitted PageHostUWP.h; the incremental build had left those objects using the old host layout.

`PlatformWin.cmake` now sets explicit OBJECT_DEPENDS on PageHostUWP and its embedded-state headers for all four host-layout readers. The next build recompiled ChromeClientUWP and TransientPageUWP and relinked. `ninja -t query` confirms the header dependencies on the generated object edges. Package v1.0.0.18 contains this corrected build; v1.0.0.17 is not acceptance evidence.

```text
cmake --build build-arm-uwp-fresh --target WebCore --parallel 8
exit code: 0
```

- Final build log: `C:\Users\Longi\AppData\Local\Temp\opencode\m10-fresh-47-close-layout.log`.
- `check-engine-uwp-extraction.py`: startup/smoke preservation and actual ARMNT/AppContainer DLL inspection passed, with 24 legacy exports plus `WKUWPGetEngineAPI`.
- `check-wkuwp-legacy-surface.ps1`: signature checks passed with both clang-cl and MSVC ARM32; preserved export and consumer-surface checks passed.
- clang-format dry-run and documentation audit passed for all five affected source files; `git diff --check` passed.
- OpenSpec strict validation through the current npx CLI returned two warnings for existing >500-character requirements ("Isolated host HTML" and "Bounded and distinguishable loading policies"). Those requirement files were not changed in this teardown fix; this run is not recorded as a strict validation pass.

```text
WebCore.dll SHA-256:
558ADF824497F22D2185FF89509F4B107A23A87D5BB9A69FD235646A783C4842
```

## Device acceptance

`WebKitProbe.EngineBrowser 1.0.0.18` was packaged, signed, installed (deployment state reported Success), and launched (HTTP 200). After the phone was available again, the existing log was fetched successfully without relaunching:

```text
Device: Lumia 950 XL
OS: 15254.603.armfre.feature2_rs3svc.200106-1633
Package: WebKitProbe.EngineBrowser_1.0.0.18_arm__2932e6rj4m41p
Log: C:\Users\Longi\AppData\Local\Temp\opencode\m10-engine-47-run-v18.log
Bytes: 144372
Log SHA-256:
552476E18CA230DF678C20FA1A9053947D46187671A1C74B7EBDDBB05C8A0D8C
```

Every required cookie marker below reported `0x00000001`:

| Marker (prefix `fixture-cookie-`) | Result established |
|---|---|
| `session-distinct` | Separate ephemeral session IDs |
| `own` | Owner reads its own DOM cookie |
| `isolated` | Peer cannot read the owner's cookie |
| `cross` | Each page retains its own cookie without seeing the other's |
| `jar-shared` | CookieJar already has another reference before deliberate retention |
| `teardown-gone` | Retired session's transport lookup is absent |
| `teardown-live` | Surviving session remains registered |
| `teardown-intact` | Surviving page retains its cookie |
| `teardown-page-gone` | WeakPtr confirms the peer Page was destroyed |
| `teardown-storage-closed` | Retained provider no longer owns a cookie session |
| `teardown-close-idempotent` | Repeated close leaves storage and lookup absent |
| `teardown` | Combined teardown acceptance passed |

The scoped-provider checks also passed. The suite reported `fixture-suite-completed: 0x00000001`, with no timeout, late-success marker, or `0xc0000005` access violation. The stale-layout startup failure observed in v17 did not recur in v18.

The existing navigation and transient acceptance markers passed in the same run: 23 fresh-ID allocation checks, redirect/traversal/reload/fragment/push/ replace attribution, replacement winner/loser, veto, loop/download rejection, and warning/generic/history/cursor behavior. Seven duplicate terminal signals were absorbed. `fixture-transient-reload-body: 0` remains the previously recorded empty-blank reload observation, not a failed isolation assertion. The existing startup `0xe06d7363` exception records remain present.
