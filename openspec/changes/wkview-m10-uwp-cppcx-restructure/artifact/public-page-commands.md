# M10 5.6: frozen-table public page commands

## Native controller boundary

`Source/WebKit/UIProcess/API/uwp/native/PageControllerUWP.h/.cpp` consumes an
already-negotiated, runtime-pinned V1 page table. Binding validates structure/
ABI identity, required structured/HTML/certificate capabilities, required command
slots and callback attachment before adoption. Requests cross only as frozen
headers, spans, opaque handles and decision pairs. No WebCore, CX or STL object
crosses the engine boundary. Owner-thread checks precede forwarding; positive
operation codes are mapped explicitly to HRESULTs and negative originating
HRESULTs are preserved.

`WebKitWebView.BindPageController` attaches the real retained V1 callback table.
Remote Source, Stop and Close use this controller when bound. Public Back,
Forward, Reload, host-HTML and retry are copied deferred operations with current
lifetime/state/token revalidation. Reentrant work cannot begin between loading
state publication and its owed terminal event.

Unavailable history/no-document Reload remains a behavioral no-op: no main ID,
terminal, history mutation or synthetic document is invented. A bound provider
still receives the no-op command so its pending native decision is invalidated;
an unbound document-free control can return without page work. Review of v6
found that UI-only invalidation skipped this provider side effect. V7 corrects
that path and asserts both UI/provider eligibility are gone without a new ID.

## HTML and capability commands

Both NavigateToString overloads copy host UTF8 by explicit length, including
embedded NULs. Null text and invalid warning objects reject before enqueue or
native mutation. `loadHtml` receives ContentKind=HostHtml, an **empty URI/base**,
owned text borrowed only during the call, and either zero decision or the exact
associated warning pair. Source/policy/terminal lifecycle comes from the retained
structured callbacks, not a downloader or document-writer shortcut.

Host text is retained only when its attributed HostHtml proposal actually commits;
veto leaves the previous document/text/history untouched. A remote commit/Close
clears transient text. HostHtml Reload submits retained text again with a new
provider main ID, empty URI and zero warning pair, maintaining history-neutral
content without preserving consent. Back/Forward invoke the provider cursor;
there is no application-owned history list or invented prior-document entry in
the control. Generic HTML invalidates current decisions on execution even when
later vetoed. Associated warning HTML is revalidated and preserves exact current
eligibility across commit and repeated warning proposals.

ProceedWithCertificateFailure validates the exact current object/control binding
and its HTTPS target, copies the hidden pair, then consumes local object
eligibility **before enqueue**. A mutation epoch is checked before deferred
provider execution, so invalidated reservations cannot silently grant consent.
The V1 provider `proceedWithCertificateFailure` atomically consumes its own
decision and retries the exact stored target with a new Remote ID. A second
submission fails E_INVALIDARG; consent stays consumed if the retry is vetoed.
Readiness continuations for copied host text use weak control lifetime and the
same shared initialization/callback drain guards.

## Acceptance scope

The diagnostic binds the actual public control to a deterministic **frozen V1
engine function table**, with a per-page provider registry and retained callbacks.
Only the test provider owns its history cursor/token ledger; no such vector or
test URL/sequence enters framework sources. It verifies request shape, command
dispatch, callback identity/lifecycle, provider-side no-op effects, history-neutral
submission and consent sequencing.

These tests do not prove real DOM/network origin enforcement from a simulated
provider. Actual WebCore substitute-data/TLS integration and production negotiated
slot implementation retain their engine/integration gates. The existing engine
slots remain Unsupported until their forwarders are delivered; controller binding
cannot manufacture a successful readiness/capability claim. Full G3 and public
runner/renderer acceptance remain pending.

## Verification

On 2026-10-08, PowerShell 7.6.6, MSVC 14.44.35207, SDK 10.0.19041.0:

```powershell
.\Tools\uwp\build\build-cx-source-state-probe.ps1 `
  -OutDir "$env:TEMP\opencode\m10-commands-phone-v7" -Version 1.0.0.7 `
  -InitializationTests -EventCallbackTests -CommandTests `
  -SignThumbprint 18FD65CF5E103CF96F04C2A451DC49C94788A40D
.\Tools\uwp\build\test-cx-component-contract.ps1 `
  -OutDir "$env:TEMP\opencode\m10-commands-56-contract-v1"
.\Tools\uwp\build\build-arm32-cx-component.ps1 `
  -OutDir "$env:TEMP\opencode\m10-commands-56-production-v7"
```

- ARM32 native/CX `/W4 /WX`, full DLL/WinMD link and signed package: pass.
- Production ARMNT/AppContainer, exact public metadata and all four diagnostic
  export exclusions: pass.
- Host factory/WinMD-only consumer regression: **20/20** plus four expected
  forbidden construction/setter compilations.
- Documentation audit: **zero candidates**; all new methods/functions, constants,
  enums/types and owned fields are documented.

Lumia 950 XL, `MobileCoreProd`, OS
`15254.603.armfre.feature2_rs3svc.200106-1633`, portal `https://192.168.1.52`.
Package `WebKitProbe.SourceStateHost_1.0.0.7_arm__2932e6rj4m41p`;
App ID `WebKitProbe.SourceStateHost_2932e6rj4m41p!App`.
Install state Code=0/Success=true, launch HTTP 200. One 30-second poll fetched
all four version-matched reports with HTTP 200:

```text
command-result.txt:        version=1.0.0.7 result=PASS checks=27 failures=0
event-callback-result.txt: version=1.0.0.7 result=PASS checks=37 failures=0
initialization-result.txt: version=1.0.0.7 result=PASS checks=33 failures=0
source-state-result.txt:   version=1.0.0.7 result=PASS checks=42 failures=0
```

The 27 command assertions cover bound no-ops, Remote request shape, provider
history traversal/branching, fresh reload IDs, empty-base HostHtml lifecycle,
unchanged entries/cursor, retained transient reload, existing-neighbor Back,
terminal Stop, veto retention, NUL text/null rejection, equal-ID cross-control
warning/retry rejection, repeated associated warnings, atomic exact-target retry,
single use, generic/warning-reload invalidation, provider-side no-op invalidation,
vetoed and reentrant retry, and explicit native-result/HRESULT mapping.

SHA256 identities:

- Signed APPX: `C022926C663E81ACAA667E75F6ADB42DA6EDD4DC7B48A97FAEC733E647045FF5`
- Diagnostic `WebKit.dll`: `220A1F750B952DEB48A5E6EECAEB1D4BC2F0C39EE5819709DAEA091B53F36F2C`
- Diagnostic `WebKit.winmd`: `E7B63F6CE26FB88497D45AD586078B5BA26D54B211FF49A253C8433DD7A3E283`
- Command report (2130 bytes): `C1D567EABF4F79F9FEE8352E7931674740BD5E81878984E8ADEB866D7D572C2A`
- Callback report: `9A605230EC13D513E7A8E9A92F4A2BD4AD65F1EB44335633262212E348608E45`
- Initialization report: `A4778A453FA870A823F081625EF10175EA4D2441FE0047CE59167D3B9B494ECB`
- Source/state report: `EAD7940025F302C373AB5CE3F73AE3134B2BB991C9ED336D8768E79FD77A1D0F`
- Production `WebKit.dll`: `78CE11478A9CEA2A14E8FCE53A52C99E61A6D8FE0BAD2E925D477E821270E60B`
- Production `WebKit.winmd`: `A86288EAF5E8367A7E77936C166DF6F64FB7D457BE1FF1A0BC129859548C55B7`

Reports are retained under `$env:TEMP/opencode/` as `m10-commands-v7-result.txt`,
`m10-commands-v7-events.txt`, `m10-commands-v7-initialization.txt` and
`m10-commands-v7-state.txt`; matching build metadata/manifests remain in output roots.
