# M9 loader integration map

## Verified source inventory

| Responsibility | Existing source | Integration decision |
| --- | --- | --- |
| Document/client lifecycle | `Source/WebCore/loader/FrameLoader.*`, `LocalFrameLoaderClient.h`, `DocumentLoader.*` | Implement an embedding client; use the real frame load path |
| Document/subresource checks | `loader/cache/CachedResourceLoader.*`, `loader/SubresourceLoader.*`, `DocumentThreadableLoader.*` | Preserve these existing document/origin/script mechanisms |
| Loader host boundary | `Source/WebCore/loader/LoaderStrategy.h` | Replace the snapshot-only strategy for browser initialization |
| Old single-process scheduler | `Source/WebKitLegacy/WebCoreSupport/WebResourceLoadScheduler.*` | Reference for scheduling, deferral and `SubresourceLoader::create`; do not import legacy UI |
| Generic old transport | `Source/WebCore/platform/network/ResourceHandle.*` | This revision contains a Cocoa backend, but no curl ResourceHandle backend; legacy `ResourceLoader::start()` cannot simply be enabled for HTTP |
| Modern host adapter | `Source/WebKit/WebProcess/Network/WebLoaderStrategy.cpp`, `WebResourceLoader.*` | Reference for driving loader callbacks without calling old ResourceHandle start; its WebPage/IPC/NetworkProcess dependencies prevent direct standalone reuse |
| Curl request/worker scheduling | `Source/WebCore/platform/network/curl/CurlRequest.*`, `CurlRequestScheduler.*` | Reuse existing transport, asynchronous callback and cancellation mechanisms |
| Response/redirect/auth/session reference | `Source/WebKit/NetworkProcess/curl/NetworkDataTaskCurl.*` | Adapt required behavior at the standalone boundary; do not silently omit redirect/security/cookie checks |
| TLS | `platform/network/curl/CurlSSLHandle.*`, `CurlContext.*` | Configure the verified packaged CA path; keep validation enabled |
| Cookies | `NetworkStorageSessionCurl.cpp`, `CookieJarDB.*`, `CookieUtil.*` | Ephemeral sessions use in-memory DB; align engine and transport cookies; explicit sharing only |
| Current rejection path | `Source/WebCore/platform/uwp/FirstPageUWP.cpp` | Separate snapshot initialization from browser strategies before real navigation |
| Current shell shortcut | `Tools/MiniBrowser/uwp/MiniBrowserUWP.cpp` | Remove worker HTML download/polling only after native loader tests pass |
| Main-thread integration | `CurlRequest::runOnMainThread`, WTF RunLoop/main-thread dispatch | Completion must be driven by the UI dispatcher independently of CompositionTarget.Rendering |

## Required standalone adapters

1. Browser-capable platform/loader strategy driving normal SubresourceLoader lifecycle with existing CurlRequest callbacks.
2. UWP embedding/frame-loader clients for URL navigation, response policy, title/history/load outcomes and same-window links.
3. Session/networking context for cookies, credential policy, TLS configuration and application-scoped profiles.
4. Owner-thread dispatch plus safe cancellation/teardown, including removal while a response policy callback is pending.
5. Versioned opaque page API behind the native wrapper; WinRT metadata and XAML activation remain a subsequent layer.

Do not assume `ResourceHandle::create` has a working curl platform implementation merely because the generic method is present. Do not bypass CSP/CORS or return `havePerformedSecurityChecks=true` unless the actual corresponding checks run. Default `LoaderStrategy::shouldPerformSecurityChecks` is false; the selected standalone path must deliberately place/verify these checks.
