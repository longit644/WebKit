# Source/state diagnostic

This ARM32 CX/XAML host exercises the actual `WebKit.WebKitWebView`, its URI DP,
read-only properties, INPC events and runtime bindings. The diagnostic component
copy adds `SourceStateTests.cpp`, which binds a per-control deterministic command
port and sends frozen V1 structured signals. Shipping builds exclude its native
test export; no private controller methods enter public metadata.

## Build and run

Use PowerShell 7, a fresh output directory and the installed ARM32 MSVC/SDK:

```powershell
.\Tools\uwp\build\build-cx-source-state-probe.ps1 `
  -OutDir <fresh-output-directory> -Version 1.0.0.3 -SignThumbprint <thumbprint>
python Tools/uwp/device-portal.py --url <portal-url> --insecure install <output>/SourceStateProbe-arm.appx
python Tools/uwp/device-portal.py --url <portal-url> --insecure get /api/app/packagemanager/state
```

Launch the actual installed version with package-family App ID
`WebKitProbe.SourceStateHost_2932e6rj4m41p!App` and full package name
`WebKitProbe.SourceStateHost_<version>_arm__2932e6rj4m41p`. Poll in one visible
30-second step. Retrieve `LocalState/source-state-result.txt`; require matching
version, `result=PASS`, expected assertion count and `failures=0`.

Version 1.0.0.3 passed **42/42** on Lumia 950 XL / Mobile 15254.603. The acceptance
record and payload/report hashes are in
`openspec/changes/wkview-m10-uwp-cppcx-restructure/artifact/source-state-transactions.md`.
The host accepts property/state behavior under controlled inputs; production
engine page/async initialization, rendering and compiled-XAML integration retain
their separate gates.

## Shared initialization actions

Add `-InitializationTests` to the build command to include the diagnostic-only
`WKUWPTestInitialization` entry and run it after the Source/state suite. The host
shows `INITIALIZATION / STATE PASS` only if both suites pass. This flavor stages
actual fixture `cacert.pem` and required DejaVu faces; override `-FontDirectory`
when the preserved package fonts are not available at the default path.

Version 1.0.0.4 passes **33/33** initialization assertions and **42/42** Source/state
assertions on the Lumia. Fetch both `LocalState/initialization-result.txt` and
`LocalState/source-state-result.txt`, requiring exact version and zero failures.
The acceptance record is `artifact/shared-initialization.md` in the M10 change.
The returned actions are the real SDK IAsyncAction/IAsyncInfo objects; readiness
is supplied by a controlled per-control port, and production page forwarding
retains its separate integration gate.

## Retained structured callbacks and tokens

Add `-EventCallbackTests` (normally with `-InitializationTests`) to run the native
frozen-table suite after both existing suites. The host shows
`EVENTS / INITIALIZATION / STATE PASS` only when every selected suite passes.
Fetch `LocalState/event-callback-result.txt` as well as the other two reports.

Version 1.0.0.5 passes **37/37** callback/token assertions, **33/33** initialization
and **42/42** state assertions on Lumia 950 XL. It exercises the actual retained
WKUWPPageCallbacks table and copied real-control event objects, including
borrowed-buffer scrambling, late Cancel, handler HRESULT precedence, trace
ordering, cross-control token identity and malformed-packet rejection.
`artifact/structured-callbacks.md` in the M10 change records exact payload/report
hashes and scope. All diagnostic exports are excluded from production builds.

## Public commands through the engine table

Add `-CommandTests` with both previous switches to bind the real control through
`native/PageControllerUWP` to a deterministic frozen engine table. The test
provider owns its page registry, history cursor and decision ledger; none of those
fixtures become framework code. Fetch `LocalState/command-result.txt` as well as
the three regression files.

Version 1.0.0.7 passes **27/27** command, **37/37** callback, **33/33** initialization
and **42/42** state assertions. V7 includes the corrected provider-side pending
decision invalidation on history/no-document no-op commands. The acceptance record
is `artifact/public-page-commands.md` in the M10 change, with exact payload/report
hashes and the consumer-versus-real-engine enforcement scope.

## Close, weak references and threads

Add `-LifecycleTests` with the other switches to exercise ordinary explicit Close,
native Pending/release acknowledgement, weak subscriptions and actual worker-thread
public mutations. Fetch `LocalState/lifecycle-result.txt` and all four regression
reports; require matching version and zero failures in every file.

Version 1.0.0.8 passes **24/24** lifecycle, **27/27** command, **37/37** callback,
**33/33** initialization and **42/42** state assertions on the Lumia. The lifecycle
oracle checks weak-reference expiration after captured delegate release, native
attachment not rooting a control, and a bounded thread-pool worker returning
RPC_E_WRONG_THREAD for ten public mutations plus frozen policy dispatch.
`artifact/close-and-weak-subscriptions.md` in the M10 change records payload/report
hashes, callback acknowledgement ordering and the remaining task-7.5 scope.

## Exact notification transactions and FIFO revalidation

Add `-TransactionTests` with all previous switches to run `TransactionTests.cpp`
through the real control, native page controller and retained frozen callback
table. The host displays `TRANSACTIONS / REGRESSIONS PASS <version>` only when
every selected suite passes. Fetch `LocalState/transaction-result.txt` and all
five existing reports; missing, wrong-version or failing reports do not pass.

```powershell
.\Tools\uwp\build\build-cx-source-state-probe.ps1 `
  -OutDir <fresh-output-directory> -Version 1.0.0.10 `
  -InitializationTests -EventCallbackTests -CommandTests -LifecycleTests -TransactionTests `
  -SignThumbprint <thumbprint>
```

Version 1.0.0.10 passes **35/35** transaction assertions across 20 independent
cases, including 13 complete actual/reference trace comparisons. It also passes
**24/24** lifecycle, **27/27** commands, **37/37** callbacks, **33/33** initialization
and **42/42** state assertions on Lumia 950 XL. Coverage includes notification
navigation/Close, Starting cancellation/replacement/Stop/throw precedence, stale
generation, deferred native/token/retry rejection, asynchronous policy terminal
waits and latest Source/HTML readiness intent. The acceptance record is
`artifact/notification-transactions.md` in the M10 change. The provider controls
V1 responses; real WebCore/renderer/platform integration retains its own gates.
