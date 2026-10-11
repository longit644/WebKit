# Native W10M presentation and metrics

`SwapChainPresenterW10M` owns CPU-mode D3D11 device/context/upload texture and the
double-buffered premultiplied BGRA8 swap chain. Its panel argument is borrowed;
successful native QI is retained until detach. Caller pixels are borrowed only
for upload, with no stored pointer. The V1 buffer overload checks header,
16-byte alignment, exact physical extent, stride and capacity before use.
Physical edges are bounded by 4096, row stride by 16384 and finite scale by
`0 < scale <= 8`, independently of WebCore-private headers.

GPU mode retains the host visual and a copied capable negotiated table/page
handle. The engine performs its own QI/AddRef and owns ANGLE/Skia and its swap
chain. The presenter creates no CPU resources in that mode. CPU/GPU attachment
cannot replace an active mode: detach and release first. Failed detach retains
ownership; Pending detach blocks rendering and attachment, and cannot be reissued.
The runtime captures `compositorAttachmentId()` and calls
`acknowledgeCompositorDetach(id)` only after actual engine release. A stale cookie
cannot release a newer pending attachment. The runtime owns the process-pinned
module and the live page through this protocol.

All mutating operations enforce the constructing owner thread before accessing
arguments, the panel or provider. Ordinary explicit detach is idempotent. Callers
must keep a Pending attachment alive through acknowledgement; dispatcher-loss/
last-release cleanup belongs to RuntimeOwnerW10M task 7.5.

Lost-device recovery rebuilds CPU device-dependent resources on the retained
panel, geometry and composition scale: the dead chain leaves the panel before
its replacement is created, so one visual never owns two chains, and any
rebuild failure lands Detached with nothing attached. `deviceRemovedReason()`
reports the D3D removal reason (S_OK healthy); `recoverDevice()` rebuilds even
when healthy and counts each rebuild in `deviceRecoveries()`. Detached,
engine-owned and CoreWindow modes have no reattachable host device. Shells
call recovery after `DXGI_ERROR_DEVICE_REMOVED`/`DEVICE_RESET` from present,
cached-present or resize, then retry once; real removal events are not
triggerable on WARP or the phone, so the rebuild path itself is what is proven.

`FrameMetricsW10M` owns bounded, allocation-free pacing and CPU paint windows.
Input timestamps are monotonic seconds. `FrameTimingW10M`, distribution and stage
summary fields use **milliseconds**. Text reports retain **microseconds** and
callback **milli-Hertz**. These measure CPU work/submission and callback cadence,
not GPU execution timestamps or physical display scan-out. The retained 60-Hz
diagnostic budget is not a scheduling loop or refresh-rate discovery API.

Pacing excludes eight submitted warmup callbacks, then summarizes 64 work
samples with nearest-rank p95. Only consecutive submitted intervals below
250 ms enter cadence; intervals over 25 ms are marked long. Navigation reset
clears work samples and their stage totals together. CPU-stage summaries require
at least eight paints across one second. Reporting is supplied by the consumer;
neither metrics nor diagnostic strings synthesize public browser events.

The compatibility header `../WebKitWebViewPresenterW10M.h` aliases the former
native presenter name (including nested `FrameTiming`) for existing consumers.
Native smoke/prototype application builds now compile the extracted sources; the
authored CX driver compiles them without `/ZW`. MSVC CX and native ARM clang
remain separate domains.

Run `Tools/w10m/build/test-rendering-contract.ps1 -ComponentDirectory <built-component>
-OutDir <fresh-output>` for real WARP CPU resources, observed COM lifetime,
controlled V1 GPU mode transitions, device-loss detection/rebuilds and
independently calculated metrics. M10 `artifact/swap-chain-presenter.md`
records the ownership baseline; `artifact/resize-recovery.md` records 67
checks, both native ARM compilers, phone rotation/scale/pinned-layer behavior,
single attachment and the v49 real-driver rebuild, completing G4. The
landscape system-bar overlay stays shell chrome for task 8.1.

## Shared owner scheduling

`FrameSchedulerW10M` owns one live view lease and its independent presentation
eligibility. `OwnerThreadServicePumpW10M` shares a copied negotiated service table
and deduplicates the SDK RenderingTime epoch across participants. Each view can
present once, while the engine is serviced once for that epoch. Reentrant service
cannot recursively cycle the run loop; a failed epoch result is preserved for
every participant. Ordinary Close releases only that view's lease.

`platform/FrameSchedulerDriverW10M` is the private C++/CX OS bridge. It owns one
weak-target CompositionTarget subscription and one **50-ms owner service timer**.
Timer/wake service is independent of presentation visibility/dirty state and
does not synthesize navigation completion. Worker wake requests coalesce to an
owner dispatcher handler. Removing the last driver client revokes its SDK hooks.
The service cadence is not a display-refresh loop or a frame-completion oracle.

Bound service-capable public page tables automatically acquire a lease; Pending
Close retains it until cleanup acknowledgement. SDK Loaded/Unloaded and app
event subscription wiring remains 6.5/7.5; native presentation eligibility is
already independent of explicit process suspension. Suspension reaches the real
loader's queue/deadline clock and is applied once across clients.

The real engine's `service` and `setSuspended` slots now forward to EngineW10M.
Per-page legacy ticks update rendering only. Capability mask zero and unsupported
page-creation/initialization forwards still prevent false public readiness.
`artifact/shared-frame-scheduling.md` records 34 native checks and the real
phone timer/resource DOM oracle, including zero presentation while loading.
