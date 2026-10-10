# M10 5.5: retained structured callbacks and copied token/event projection

## Callback attachment and copied state

`Source/WebKit/UIProcess/API/uwp/platform/PageCallbacksUWP.h/.cpp` supplies the actual frozen `WKUWPPageCallbacks` table. Its native context has setup ownership, independent engine retain/release ownership and temporary active-dispatch references. It holds a weak real-control target, checks the owner thread before resolving XAML, contains every failure at the C boundary, and deactivates late delivery without prematurely freeing retained engine context. Ordinary Close finishes owed outcomes before setup deactivation; native/weak destruction invokes no application handlers or XAML teardown.

`WebKitWebView.BuildPageCallbacks` builds that attachment for the controller. Synchronous policy enters `HandleStarting`; notifications enter the copied `HandleNotification`/transaction path. No public event is inferred from a log or diagnostic string. URI/title spans are decoded/copied before public dispatch; signals deferred across a transaction own their bytes before the provider returns. Native state now retains active content kind and proposed URI, validates terminal kind/generation, and supplies the terminal target when the provider's URI is empty.

The public payload maps ContentKind, Outcome, ErrorDomain and ChangeKind explicitly. ErrorCode preserves its own domain's numeric code/HRESULT bits; HasHttpStatus is not inferred from a status value. Absent status normalizes to zero. HostHtml cannot acquire HTTP status or certificate eligibility. Source, history/title and loading snapshots are installed before property notifications and corresponding typed events. Terminal obligations survive handler errors, reentrant mutation and Close.

Starting handler exceptions return the original negative HRESULT; the controlled provider maps that to Failed/Platform. Explicit Cancel or Close takes precedence and yields Cancelled. Nonpolicy subscriber failures remain diagnostic and never change/retry the recorded terminal. Dispatch freezes Cancel on every path; late owner writes return RO_E_CLOSED. Mutable Cancel/dispatch flags are atomic so retained arguments do not expose racy reads after owner dispatch.

## Opaque certificate objects

Each control has a nonzero process-unique hidden identity independent of page or navigation IDs. An accepted Failed/Transport Remote completion with a nonzero engine issuer/decision and HTTPS target constructs a nonactivatable immutable `WebKitCertificateFailureToken`. The current object is bound to control identity, issuer and increasing failure revision. Two controls failing at local ID 1 cannot submit each other's object as current. HTTP/HostHtml/nonfailed or partial-pair eligibility packets are rejected before publishing a terminal or current token.

Completion arguments retain the copied token; CanProceedWithCertificateFailure is derived from token presence at creation. Later Source-null/remote proposal or Close invalidates the control's current eligibility without rewriting old event objects/diagnostics. Invalid argument rejection has no token side effect. The engine's offered pair is authoritative; a numeric CURLcode alone never issues a token. Atomic public retry and associated warning-HTML commands remain task 5.6.

## Acceptance scope

The diagnostic uses the real XAML/CX control and **retained frozen callback table**, including context retain/release, policy return values and borrowed-buffer scrambling. Its controlled per-page provider supplies V1 signals/readiness; it does not parse browser logs or claim network TLS behavior from synthetic packets. Production negotiated engine forwarding remains Unsupported until subsequent integration. These results accept the copied callback/token projection and do not complete G3 or replace the engine security/renderer/public-runner gates.

## Verification commands

Succeeded on 2026-10-08, PowerShell 7.6.6, MSVC 14.44.35207, SDK 10.0.19041.0:

```powershell
.\Tools\uwp\build\build-cx-source-state-probe.ps1 `
  -OutDir "$env:TEMP\opencode\m10-callbacks-phone-v5" -Version 1.0.0.5 `
  -InitializationTests -EventCallbackTests `
  -SignThumbprint 18FD65CF5E103CF96F04C2A451DC49C94788A40D
.\Tools\uwp\build\test-cx-component-contract.ps1 `
  -OutDir "$env:TEMP\opencode\m10-callbacks-55-contract-v1"
.\Tools\uwp\build\test-navigation-state.ps1 `
  -ComponentDirectory "$env:TEMP\opencode\m10-callbacks-55-arm-v1" `
  -OutDir "$env:TEMP\opencode\m10-callbacks-55-native-v1"
.\Tools\uwp\build\build-arm32-cx-component.ps1 `
  -OutDir "$env:TEMP\opencode\m10-callbacks-55-production-v5"
```

- ARM32 `/W4 /WX` compile, full DLL/WinMD link, package/sign: pass.
- Production ARMNT/AppContainer, all diagnostic-export exclusions and exact public metadata/private-member checks: pass.
- Factory/WinMD-only consumer regression: **20/20** plus four expected negative construction/setter compilations.
- Native identity/generation/state regression: **30/30**.
- Targeted documentation audit: **zero candidates**; methods/functions, constants/enums/types and owned fields are documented.

## Lumia evidence

Lumia 950 XL, `MobileCoreProd`, OS `15254.603.armfre.feature2_rs3svc.200106-1633`, portal `https://192.168.1.52`. Package `WebKitProbe.SourceStateHost_1.0.0.5_arm__2932e6rj4m41p`; App ID `WebKitProbe.SourceStateHost_2932e6rj4m41p!App`. Install completed with Code=0/Success=true; launch HTTP 200. One 30-second poll fetched all three version-matched files with HTTP 200:

```text
event-callback-result.txt: version=1.0.0.5 result=PASS checks=37 failures=0
initialization-result.txt: version=1.0.0.5 result=PASS checks=33 failures=0
source-state-result.txt:   version=1.0.0.5 result=PASS checks=42 failures=0
```

The 37 new assertions cover frozen ownership entries, copied proposal/UTF8 commit data, late Cancel, generation-derived traversal attribution, trace-based property-before-event ordering, explicit HTTP/error-domain fields, all four error families with unchanged numeric/HRESULT codes, HostHtml policy, equal-ID cross-control tokens, invalid/current-token transitions, Starting throw/cancel/ Close precedence, post-close retained-context safety, diagnostic-only subscriber errors, malformed eligibility/spans, foreign-page rejection, duplicates, and owned reentrant deferred native payloads after source-buffer scrambling.

Hashes (SHA256):

- Signed APPX: `20DB31667B8C5B1E6EB6E99C3FB8C1ADDCA2A45367101A9E3181B2D918A8C169`
- Diagnostic `WebKit.dll`: `DEE6401E25BDB9BA40C823A60974697FA1F39541DC886F4F731018CB2FCE7D40`
- Diagnostic `WebKit.winmd`: `07730064CD5206333953C2005526FECB59511EFEA1A006485211C91275684E6D`
- Callback report (3049 bytes): `A7F9397C298AA022AE6869F28518DA817A1676DA30EA258A864D85FE1FB30474`
- Initialization report (2362 bytes): `B54219B58956D36DBFE966ADC375527F9A2B32F6261DDE9A9BEC58A76AE34197`
- Source/state report (2597 bytes): `D12350E40ED6A86A2FE721B6AA7A1DD4D6038DE307209417A9B96705C389B70C`
- Production `WebKit.dll`: `D38F18E64917DFB8A862D6EA5FF5ECD03513AA3BCFF2E53F81B8DCC27BEA5F6A`
- Production `WebKit.winmd`: `F2CF0D893C7B9C1794D354067B6F8AFE9353CF59E27C45FB612993960EC6AE8B`

Full fetched reports remain under `$env:TEMP/opencode/` as `m10-callbacks-v5-result.txt`, `m10-callbacks-v5-initialization.txt` and `m10-callbacks-v5-state.txt`; builds retain matching metadata/identity manifests.
