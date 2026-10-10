// Native ownership/metrics oracle: real WARP DirectX objects and controlled V1 GPU slots.
#include "rendering/SwapChainPresenterUWP.h"
#include "WebKitWebViewPresenterUWP.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <dxgi1_3.h>
#include <limits>
#include <map>
#include <roapi.h>
#include <thread>
#include <type_traits>

using namespace WebKit;
using Microsoft::WRL::ComPtr;

namespace {

// Checked physical CPU fixture extent; aligned BGRA storage below has exactly this size.
constexpr unsigned cpuExtent = 16;
// Exact bytes per row of the premultiplied BGRA fixture.
constexpr unsigned cpuStride = cpuExtent * 4;
// Fail-closed assertion totals reported independently of the last case.
unsigned checks = 0, failures = 0;

// Preserve all failures even when later ownership cases pass.
void check(bool value, const char* name)
{
    ++checks;
    if (!value) ++failures;
    std::printf("%s: %s\n", value ? "PASS" : "FAIL", name);
}

// Compare deterministic millisecond measurements independently of formatting rounding.
bool closeTo(double left, double right) { return std::abs(left - right) < 0.00001; }

// Inspectable/native panel double observes actual QI/AddRef/release and DXGI attachments.
class Panel final : public IInspectable, public ISwapChainPanelNative {
public:
    // Intrusive reference count starts at zero for the root ComPtr constructor.
    std::atomic<ULONG> references { 0 };
    // Real CPU DXGI chain retained by SetSwapChain exactly as a native panel would retain it.
    ComPtr<IDXGISwapChain> chain;
    // CPU attachment and detachment calls that reached the native panel.
    unsigned attachments { 0 }, detachments { 0 };
    // Controlled QI/detach failures before any attachment side effect.
    bool inspectableSupported { true }, nativeSupported { true }, rejectDetach { false };
    // Controlled engine ownership cannot coexist with a CPU chain.
    bool engineAttached { false };

    // Return COM identity or the requested rendering interface with one owned reference.
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** output) override
    {
        if (!output) return E_POINTER;
        *output = nullptr;
        if (iid == __uuidof(IUnknown) || (iid == __uuidof(IInspectable) && inspectableSupported))
            *output = static_cast<IInspectable*>(this);
        else if (iid == __uuidof(ISwapChainPanelNative) && nativeSupported)
            *output = static_cast<ISwapChainPanelNative*>(this);
        if (!*output) return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }

    // Retain one observed host/provider interface ownership.
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references; }
    // Destroy only after root, host and provider references are all gone.
    ULONG STDMETHODCALLTYPE Release() override
    {
        auto value = --references;
        if (!value) delete this;
        return value;
    }

    // The double has no projected type-provider interface inventory.
    HRESULT STDMETHODCALLTYPE GetIids(ULONG* count, IID** iids) override
    {
        if (!count || !iids) return E_POINTER;
        *count = 0; *iids = nullptr; return S_OK;
    }

    // No runtime class name is needed for a native-only ownership double.
    HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HSTRING* name) override
    {
        if (!name) return E_POINTER;
        *name = nullptr; return S_OK;
    }

    // Return the ordinary trust classification without granting special behavior.
    HRESULT STDMETHODCALLTYPE GetTrustLevel(TrustLevel* value) override
    {
        if (!value) return E_POINTER;
        *value = BaseTrust; return S_OK;
    }

    // Retain exactly the supplied real CPU chain, or reject without mutation.
    HRESULT STDMETHODCALLTYPE SetSwapChain(IDXGISwapChain* value) override
    {
        if (value && (chain || engineAttached)) return E_ILLEGAL_METHOD_CALL;
        if (!value && rejectDetach) return E_ACCESSDENIED;
        if (value) ++attachments; else ++detachments;
        chain = value;
        return S_OK;
    }
};

// Per-page provider instance for context-free frozen GPU table signatures.
struct GPUProvider;
// Explicit page routing used only by this test provider.
std::map<WKUWPPageHandle, GPUProvider*> gpuPages;

// Controlled provider retains its rendering interface independently of host ownership.
struct GPUProvider {
    // Provider page identity and exact table presented to the consumer.
    WKUWPPageHandle page;
    WKUWPEngineAPI api { };
    // Engine-owned native panel interface; no host CPU resources are allocated here.
    ComPtr<ISwapChainPanelNative> retained;
    // Panel double whose ownership counters observe both participants.
    Panel* panel { nullptr };
    // Calls admitted to each native slot.
    unsigned attaches { 0 }, detaches { 0 }, renders { 0 };
    // Controlled attach/render/detach originating native result.
    WKUWPResult attachResult { WKUWPResultOK }, detachResult { WKUWPResultOK }, renderResult { WKUWPResultOK };
    // Exact CSS overscroll forwarded through the frozen renderer slot.
    double overscroll { 0 };

    // Register only this explicit page and construct a capable exact V1 table.
    explicit GPUProvider(WKUWPPageHandle id) : page(id)
    {
        gpuPages[page] = this;
        api.header = { sizeof(api), WKUWP_STRUCTURE_VERSION };
        api.abiMajor = WKUWP_ABI_MAJOR_VERSION;
        api.capabilities = WKUWPCapabilityGPUCompositor;
        api.attachCompositor = &attach;
        api.detachCompositor = &detach;
        api.renderCompositor = &render;
    }

    // Release any controlled pending engine ownership before destroying the registry entry.
    ~GPUProvider() { release(); gpuPages.erase(page); }
    // Find exactly the handle supplied by the native caller.
    static GPUProvider* find(WKUWPPageHandle page)
    {
        auto item = gpuPages.find(page);
        return item == gpuPages.end() ? nullptr : item->second;
    }

    // QI/retain the engine rendering interface only after valid frozen attachment.
    static WKUWPResult __stdcall attach(WKUWPPageHandle page, const WKUWPCompositorOptions* options)
    {
        auto self = find(page);
        if (!self) return WKUWPResultClosed;
        ++self->attaches;
        if (self->attachResult != WKUWPResultOK) return self->attachResult;
        auto inspectable = static_cast<IInspectable*>(options->panelInspectable);
        ComPtr<ISwapChainPanelNative> native;
        auto result = inspectable->QueryInterface(IID_PPV_ARGS(&native));
        if (FAILED(result)) return static_cast<WKUWPResult>(result);
        auto panel = static_cast<Panel*>(native.Get());
        if (panel->chain || panel->engineAttached) return WKUWPResultInvalidState;
        self->panel = panel;
        self->retained = std::move(native);
        panel->engineAttached = true;
        return WKUWPResultOK;
    }

    // Failed/Pending detach keeps engine references; immediate success releases them.
    static WKUWPResult __stdcall detach(WKUWPPageHandle page)
    {
        auto self = find(page);
        if (!self) return WKUWPResultClosed;
        ++self->detaches;
        if (self->detachResult != WKUWPResultOK) return self->detachResult;
        self->release();
        return WKUWPResultOK;
    }

    // Observe exact page/overscroll routing without pretending to execute ANGLE/Skia.
    static WKUWPResult __stdcall render(WKUWPPageHandle page, double overscroll)
    {
        auto self = find(page);
        if (!self) return WKUWPResultClosed;
        ++self->renders; self->overscroll = overscroll;
        return self->renderResult;
    }

    // Model the actual native-release acknowledgement before host completion is permitted.
    void release()
    {
        if (panel) panel->engineAttached = false;
        retained.Reset(); panel = nullptr;
    }
};

// Verify real native CPU resources, QI lifetime, failures and exclusive GPU transitions.
void runOwnership()
{
    static_assert(std::is_same_v<WebKitWebViewPresenterUWP, SwapChainPresenterUWP>);
    static_assert(std::is_same_v<WebKitWebViewPresenterUWP::FrameTiming, FrameTimingUWP>);
    ComPtr<Panel> panel(new Panel);
    auto unknown = static_cast<IInspectable*>(panel.Get());
    SwapChainPresenterUWP presenter { D3D_DRIVER_TYPE_WARP };
    GPUProvider gpu(101), peer(102);
    check(presenter.detach() == S_OK && presenter.mode() == PresentationModeUWP::Detached, "initial/repeated detach is idempotent");
    check(presenter.initializeForSwapChainPanel(unknown, 0, 16) == E_INVALIDARG
        && presenter.initializeForSwapChainPanel(unknown, 4097, 16) == E_INVALIDARG && panel->references == 1,
        "zero/oversized physical extent rejects before panel QI or allocation");
    panel->nativeSupported = false;
    check(presenter.initializeForSwapChainPanel(unknown, 16, 16) == E_NOINTERFACE && panel->references == 1,
        "CPU native-panel QI failure leaves attachment/refcount untouched");
    panel->nativeSupported = true;
    auto initialized = presenter.initializeForSwapChainPanel(unknown, cpuExtent, cpuExtent);
    check(initialized == S_OK && presenter.mode() == PresentationModeUWP::CPU && panel->chain && panel->references == 2,
        "CPU attachment owns real WARP swap chain and one QI-retained panel reference");
    if (FAILED(initialized)) return;
    DXGI_SWAP_CHAIN_DESC1 description { };
    ComPtr<IDXGISwapChain1> chain;
    panel->chain.As(&chain);
    chain->GetDesc1(&description);
    check(description.Width == cpuExtent && description.Height == cpuExtent && description.BufferCount == 2
        && description.Format == DXGI_FORMAT_B8G8R8A8_UNORM && description.AlphaMode == DXGI_ALPHA_MODE_PREMULTIPLIED,
        "real CPU chain preserves double-buffered premultiplied BGRA format");
    check(presenter.initializeForCompositor(&gpu.api, gpu.page, unknown, 1, 1) == E_ILLEGAL_METHOD_CALL && !gpu.attaches,
        "GPU cannot attach while CPU surface owns the visual");
    check(presenter.initializeForSwapChainPanel(unknown, 16, 16) == E_ILLEGAL_METHOD_CALL && panel->attachments == 1,
        "second CPU initialization cannot replace a live attachment");
    check(presenter.presentCachedFrame() == S_FALSE, "new CPU surface has no fabricated cached pixels");
    alignas(16) unsigned char pixels[cpuStride * cpuExtent] { };
    WKUWPPaintBuffer buffer { { sizeof(buffer), WKUWP_STRUCTURE_VERSION }, pixels, sizeof(pixels), cpuStride, cpuExtent, cpuExtent };
    auto shortBuffer = buffer; shortBuffer.capacityBytes = 1;
    auto wrongStride = buffer; wrongStride.strideBytes = cpuStride - 1;
    auto wrongExtent = buffer; wrongExtent.widthPixels = cpuExtent - 1;
    auto unaligned = buffer; unaligned.data = pixels + 1;
    check(presenter.present(shortBuffer) == E_INVALIDARG && presenter.present(wrongStride) == E_INVALIDARG
        && presenter.present(wrongExtent) == E_INVALIDARG && presenter.present(unaligned) == E_INVALIDARG,
        "V1 CPU buffer validates capacity stride exact extent and 16-byte alignment before upload");
    check(SUCCEEDED(presenter.present(buffer)) && presenter.frameTiming().uploadMS >= 0 && presenter.frameTiming().copyMS >= 0
        && presenter.frameTiming().presentMS >= 0, "real WARP BGRA upload copy and Present report nonnegative millisecond call durations");
    check(presenter.setCompositionScale(0, 1) == E_INVALIDARG && presenter.setCompositionScale(9, 1) == E_INVALIDARG
        && presenter.setCompositionOffset(std::numeric_limits<float>::infinity(), 0) == E_INVALIDARG,
        "invalid scale/offset rejects before native composition mutation");
    check(presenter.setCompositionScale(2, 2) == S_OK && presenter.setCompositionOffset(3, 4) == S_OK,
        "CPU scale and CSS offset update actual composition matrix");
    ComPtr<IDXGISwapChain2> chain2;
    chain.As(&chain2);
    DXGI_MATRIX_3X2_F matrix { };
    chain2->GetMatrixTransform(&matrix);
    check(closeTo(matrix._11, 0.5) && closeTo(matrix._22, 0.5) && closeTo(matrix._31, 3) && closeTo(matrix._32, 4),
        "native CPU transform uses inverse device scale with CSS translation");
    chain2.Reset(); chain.Reset();
    check(presenter.resize(32, 16) == S_OK && presenter.presentCachedFrame() == S_FALSE,
        "CPU resize releases cached upload before rebuilding buffers");
    panel->rejectDetach = true;
    check(presenter.detach() == E_ACCESSDENIED && presenter.mode() == PresentationModeUWP::CPU && panel->chain && panel->references == 2,
        "failed CPU detach retains ownership and exact native HRESULT");
    check(presenter.initializeForCompositor(&gpu.api, gpu.page, unknown, 1, 1) == E_ILLEGAL_METHOD_CALL && !gpu.attaches,
        "failed CPU detach cannot expose an overlapping GPU attachment");
    panel->rejectDetach = false;
    check(presenter.detach() == S_OK && !panel->chain && panel->references == 1 && presenter.mode() == PresentationModeUWP::Detached,
        "successful CPU detach clears panel before releasing host resources");
    auto incapable = gpu.api; incapable.capabilities = 0;
    check(presenter.initializeForCompositor(&incapable, gpu.page, unknown, 1, 1) == E_NOTIMPL && !gpu.attaches,
        "missing GPU capability rejects before provider/QI work");
    auto shortAPI = gpu.api; shortAPI.header.structureSize = 8;
    check(presenter.initializeForCompositor(&shortAPI, gpu.page, unknown, 1, 1) == HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH),
        "truncated frozen table rejects before slot access");
    panel->inspectableSupported = false;
    check(presenter.initializeForCompositor(&gpu.api, gpu.page, unknown, 1, 1) == E_NOINTERFACE && !gpu.attaches && panel->references == 1,
        "GPU Inspectable QI failure preserves native ownership");
    panel->inspectableSupported = true;
    gpu.attachResult = static_cast<WKUWPResult>(E_ACCESSDENIED);
    check(presenter.initializeForCompositor(&gpu.api, gpu.page, unknown, 1, 1) == E_ACCESSDENIED && panel->references == 1
        && presenter.mode() == PresentationModeUWP::Detached, "failed GPU attach releases temporary host reference and preserves HRESULT");
    gpu.attachResult = WKUWPResultOK;
    check(presenter.initializeForCompositor(&gpu.api, gpu.page, unknown, 2, 2) == S_OK && panel->references == 3
        && !panel->chain && panel->engineAttached && presenter.mode() == PresentationModeUWP::GPU,
        "GPU retains host visual plus independent engine QI reference without a CPU chain");
    check(presenter.initializeForSwapChainPanel(unknown, 16, 16) == E_ILLEGAL_METHOD_CALL && panel->attachments == 1
        && presenter.present(buffer) == E_ILLEGAL_METHOD_CALL,
        "CPU allocation/upload is blocked while engine GPU owns the visual");
    check(presenter.renderCompositor(7.5) == S_OK && gpu.renders == 1 && closeTo(gpu.overscroll, 7.5) && !peer.renders,
        "GPU render routes exact CSS overscroll only to its explicit page");
    check(presenter.renderCompositor(std::numeric_limits<double>::infinity()) == E_INVALIDARG && gpu.renders == 1,
        "nonfinite GPU overscroll rejects before provider call");
    gpu.renderResult = static_cast<WKUWPResult>(E_ACCESSDENIED);
    check(presenter.renderCompositor(0) == E_ACCESSDENIED && presenter.mode() == PresentationModeUWP::GPU,
        "GPU render failure preserves HRESULT and requires explicit detach before fallback");
    gpu.detachResult = static_cast<WKUWPResult>(E_ACCESSDENIED);
    check(presenter.detach() == E_ACCESSDENIED && panel->references == 3 && presenter.mode() == PresentationModeUWP::GPU,
        "failed GPU detach retains both owners instead of permitting fallback");
    gpu.detachResult = WKUWPResultPending;
    check(presenter.detach() == E_PENDING && presenter.renderCompositor(0) == E_PENDING && panel->references == 3,
        "Pending GPU detach keeps attachment alive and prevents further render");
    auto detachCalls = gpu.detaches;
    auto attachment = presenter.compositorAttachmentId();
    check(presenter.detach() == E_PENDING && gpu.detaches == detachCalls
        && presenter.initializeForSwapChainPanel(unknown, 16, 16) == E_ILLEGAL_METHOD_CALL,
        "repeated Pending detach does not reissue native cleanup or allow CPU overlap");
    unsigned wrongThread = 0;
    std::thread worker([&] {
        wrongThread += presenter.detach() == RPC_E_WRONG_THREAD;
        wrongThread += presenter.acknowledgeCompositorDetach(attachment) == RPC_E_WRONG_THREAD;
        wrongThread += presenter.initializeForSwapChainPanel(nullptr, 0, 0) == RPC_E_WRONG_THREAD;
        wrongThread += presenter.initializeForCompositor(nullptr, 0, nullptr, 0, 0) == RPC_E_WRONG_THREAD;
        wrongThread += presenter.present(buffer) == RPC_E_WRONG_THREAD;
        wrongThread += presenter.renderCompositor(0) == RPC_E_WRONG_THREAD;
        wrongThread += presenter.resize(0, 0) == RPC_E_WRONG_THREAD;
        wrongThread += presenter.setCompositionScale(0, 0) == RPC_E_WRONG_THREAD;
        wrongThread += presenter.setCompositionOffset(0, 0) == RPC_E_WRONG_THREAD;
    });
    worker.join();
    check(wrongThread == 9 && gpu.detaches == detachCalls && panel->references == 3,
        "nine worker mutations reject before argument/provider/COM ownership changes");
    gpu.release();
    check(panel->references == 2 && presenter.mode() == PresentationModeUWP::GPU,
        "native completion releases engine QI ownership before host acknowledgement");
    check(presenter.acknowledgeCompositorDetach(attachment) == S_OK && panel->references == 1 && presenter.mode() == PresentationModeUWP::Detached,
        "acknowledged GPU release ends exclusive host attachment exactly once");
    check(presenter.acknowledgeCompositorDetach(attachment) == E_ILLEGAL_METHOD_CALL, "repeated detach acknowledgement cannot repeat host release");
    check(presenter.initializeForSwapChainPanel(unknown, 16, 16) == S_OK && panel->attachments == 2 && !panel->engineAttached,
        "GPU-to-CPU fallback attaches only after native release acknowledgement");
    check(presenter.detach() == S_OK && panel->references == 1, "fallback CPU resources return panel references to baseline");
    gpu.detachResult = WKUWPResultOK;
    check(presenter.initializeForCompositor(&gpu.api, gpu.page, unknown, 1, 1) == S_OK, "CPU-to-GPU transition begins only after CPU release");
    gpu.api.detachCompositor = nullptr;
    check(presenter.detach() == S_OK && panel->references == 1 && !panel->engineAttached,
        "copied negotiated table survives caller table mutation and returns references to baseline");
    peer.detachResult = WKUWPResultPending;
    check(presenter.initializeForCompositor(&peer.api, peer.page, unknown, 1, 1) == S_OK && presenter.detach() == E_PENDING,
        "another explicit page can enter its own pending detach after prior release");
    auto nextAttachment = presenter.compositorAttachmentId();
    check(nextAttachment != attachment && presenter.acknowledgeCompositorDetach(attachment) == E_INVALIDARG
        && panel->references == 3 && presenter.mode() == PresentationModeUWP::GPU,
        "late prior-attachment acknowledgement cannot release a newer pending visual");
    peer.release();
    check(presenter.acknowledgeCompositorDetach(nextAttachment) == S_OK && panel->references == 1,
        "matching new attachment cookie completes only its own host release");
}

// Verify independently calculated metric distributions, reset and report units.
void runMetrics()
{
    FrameMetricsUWP metrics;
    FramePacingSummaryUWP summary;
    bool early = false, complete = false;
    for (unsigned i = 0; i < 72; ++i) {
        double start = 1 + i * 0.02;
        auto ready = metrics.recordPacing(start, start + 0.002, start + 0.005, start + 0.01, true, summary);
        if (i < 71) early = early || ready; else complete = ready;
    }
    check(!early && complete && summary.workCount == 64 && summary.intervalCount == 64,
        "eight cold submissions precede exactly 64 warmed pacing observations");
    check(closeTo(summary.work.averageMS, 10) && closeTo(summary.work.p95MS, 10) && closeTo(summary.interval.averageMS, 20)
        && closeTo(summary.scrollMS, 2) && closeTo(summary.engineMS, 3) && closeTo(summary.renderMS, 5) && !summary.overBudget,
        "pacing separates millisecond work intervals and stage costs from display scan-out");
    char output[frameProfileMessageCapacity];
    check(FrameMetricsUWP::formatPacing(summary, output, sizeof(output)) && std::strstr(output, "work-us=10000/10000/10000")
        && std::strstr(output, "callback-mHz=50000") && std::strstr(output, "scroll-us=2000 engine-us=3000 render-us=5000"),
        "diagnostic format converts milliseconds to microseconds and callback milli-Hertz");
    char shortOutput[4];
    check(!FrameMetricsUWP::formatPacing(summary, shortOutput, sizeof(shortOutput)) && shortOutput[3] == '\0',
        "short diagnostic output fails with a bounded terminated buffer");
    metrics.resetPacing();
    FramePacingSummaryUWP sentry; sentry.workCount = 999;
    check(!metrics.recordPacing(10, 9, 10, 11, true, sentry) && sentry.workCount == 999,
        "invalid timestamp order cannot publish or mutate a completed summary");
    for (unsigned i = 0; i < 8; ++i) metrics.recordPacing(10 + i, 10 + i, 10 + i, 10 + i, true, sentry);
    for (unsigned i = 0; i < 64; ++i) {
        double start = 20 + i;
        double duration = (i + 1) / 1000.0;
        metrics.recordPacing(start, start, start, start + duration, true, sentry);
    }
    check(sentry.workCount == 64 && closeTo(sentry.work.averageMS, 32.5) && closeTo(sentry.work.p95MS, 61)
        && closeTo(sentry.work.maximumMS, 64) && sentry.overBudget == 48 && !sentry.intervalCount,
        "nearest-rank p95 and work budget are independently calculated while idle gaps are excluded");
    FrameMetricsUWP paint;
    FramePaintSummaryUWP painted;
    bool paintEarly = false;
    for (unsigned i = 0; i < 7; ++i) paintEarly = paintEarly || paint.recordPaint(i * 0.2, 2, { 1, 3, 4 }, painted);
    check(!paintEarly && paint.recordPaint(1.4, 2, { 1, 3, 4 }, painted) && painted.count == 8
        && closeTo(painted.paintMS, 2) && closeTo(painted.uploadMS, 1) && closeTo(painted.copyMS, 3) && closeTo(painted.presentMS, 4)
        && closeTo(painted.maximumMS, 10), "CPU paint reporting requires eight samples plus one second and preserves stage units");
    check(!paint.recordPaint(2, -1, { }, painted) && !paint.recordPaint(2, 1, { std::numeric_limits<double>::infinity(), 0, 0 }, painted),
        "invalid CPU durations cannot contaminate subsequent metric windows");
}

// Verify device-loss detection and single-owner rebuilds on real WARP objects.
void runRecovery()
{
    ComPtr<Panel> panel(new Panel);
    auto unknown = static_cast<IInspectable*>(panel.Get());
    SwapChainPresenterUWP presenter { D3D_DRIVER_TYPE_WARP };
    check(presenter.deviceRemovedReason() == E_ILLEGAL_METHOD_CALL, "detached presenter has no device removal reason");
    check(presenter.recoverDevice() == E_ILLEGAL_METHOD_CALL && presenter.deviceRecoveries() == 0,
        "detached presenter cannot rebuild a device");
    HRESULT threadReason = S_OK, threadRecover = S_OK;
    unsigned recoveriesBefore = presenter.deviceRecoveries();
    std::thread stranger([&] {
        threadReason = presenter.deviceRemovedReason();
        threadRecover = presenter.recoverDevice();
    });
    stranger.join();
    check(threadReason == RPC_E_WRONG_THREAD && threadRecover == RPC_E_WRONG_THREAD
        && presenter.deviceRecoveries() == recoveriesBefore, "worker device queries reject before touching DirectX");
    check(presenter.initializeForSwapChainPanel(unknown, cpuExtent, cpuExtent) == S_OK
        && presenter.mode() == PresentationModeUWP::CPU, "recovery fixture attaches a real WARP chain");
    check(presenter.deviceRemovedReason() == S_OK, "healthy WARP device reports S_OK removal reason");
    alignas(16) unsigned char pixels[cpuStride * cpuExtent] { };
    WKUWPPaintBuffer buffer { { sizeof(buffer), WKUWP_STRUCTURE_VERSION }, pixels, sizeof(pixels), cpuStride, cpuExtent, cpuExtent };
    check(SUCCEEDED(presenter.present(buffer)), "fixture frame presents before rebuild");
    check(presenter.setCompositionScale(2, 2) == S_OK, "fixture scale stages matrix retention");
    unsigned attachments = panel->attachments, detachments = panel->detachments;
    check(presenter.recoverDevice() == S_OK && presenter.deviceRecoveries() == 1,
        "healthy device rebuilds on demand and counts the recovery");
    check(presenter.mode() == PresentationModeUWP::CPU && panel->chain
        && panel->attachments == attachments + 1 && panel->detachments == detachments + 1
        && panel->references == 2, "rebuild detaches the dead chain before attaching exactly one replacement");
    ComPtr<IDXGISwapChain1> chain;
    panel->chain.As(&chain);
    ComPtr<IDXGISwapChain2> chain2;
    chain.As(&chain2);
    DXGI_MATRIX_3X2_F matrix { };
    chain2->GetMatrixTransform(&matrix);
    check(closeTo(matrix._11, 0.5) && closeTo(matrix._22, 0.5), "rebuild retains the staged composition scale");
    chain2.Reset(); chain.Reset();
    check(SUCCEEDED(presenter.present(buffer)) && presenter.presentCachedFrame() == S_OK,
        "presentation resumes on the rebuilt device with a lazily recreated upload");
    check(presenter.recoverDevice() == S_OK && presenter.deviceRecoveries() == 2
        && panel->attachments == attachments + 2 && panel->detachments == detachments + 2,
        "second rebuild counts again with exactly one live attachment");
    check(presenter.resize(32, 16) == S_OK && presenter.presentCachedFrame() == S_FALSE,
        "resize after recovery rebuilds buffers at the new extent");
    check(presenter.detach() == S_OK && presenter.mode() == PresentationModeUWP::Detached && !panel->chain,
        "detach after recovery returns the panel to baseline");
    GPUProvider gpu(201);
    check(presenter.initializeForCompositor(&gpu.api, gpu.page, unknown, 2, 2) == S_OK
        && presenter.mode() == PresentationModeUWP::GPU, "engine surface attaches for mode-gated recovery checks");
    check(presenter.deviceRemovedReason() == E_ILLEGAL_METHOD_CALL, "engine-owned mode has no host device reason");
    check(presenter.recoverDevice() == E_ILLEGAL_METHOD_CALL && presenter.deviceRecoveries() == 2,
        "engine-owned mode cannot rebuild and counts nothing");
    check(presenter.detach() == S_OK && presenter.mode() == PresentationModeUWP::Detached,
        "engine detach after recovery checks returns to baseline");
}

} // namespace

// Run native host ownership checks without pretending to construct a XAML control.
int main()
{
    auto apartment = RoInitialize(RO_INIT_MULTITHREADED);
    check(SUCCEEDED(apartment), "native COM test apartment initialized");
    if (SUCCEEDED(apartment)) { runOwnership(); runRecovery(); runMetrics(); RoUninitialize(); }
    std::printf("M10 rendering contract: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
