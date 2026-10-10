#pragma once

#include "FrameMetricsUWP.h"
#include "WKUWPRendering.h"
#include <d3d11.h>
#include <dxgi1_2.h>
#include <inspectable.h>
#include <windows.ui.xaml.media.dxinterop.h>
#include <wrl/client.h>

namespace WebKit {

// Exclusive surface ownership; the engine's ANGLE/Skia objects never move to the host.
enum class PresentationModeUWP {
    // No swap chain may remain attached to the host visual.
    Detached,
    // Presenter owns the D3D11 upload texture/device/swap chain.
    CPU,
    // Engine owns its compositor/swap chain; presenter retains only the host panel/table.
    GPU
};

// Owner-thread native DirectX presenter, with no WebCore or projected XAML dependencies.
class SwapChainPresenterUWP {
public:
    // Compatibility spelling for preserved native presenter consumers.
    using FrameTiming = FrameTimingUWP;
    // Use hardware in production; WARP is injectable for headless native ownership tests.
    explicit SwapChainPresenterUWP(D3D_DRIVER_TYPE driver = D3D_DRIVER_TYPE_HARDWARE) : m_driver(driver) { }
    // Ordinary owner-thread teardown; runtime-owner dispatcher-loss cleanup is separate.
    ~SwapChainPresenterUWP();
    // A presentation attachment has one owner and cannot be copied.
    SwapChainPresenterUWP(const SwapChainPresenterUWP&) = delete;
    // Prevent duplicating active panel/native cleanup ownership.
    SwapChainPresenterUWP& operator=(const SwapChainPresenterUWP&) = delete;
    // Create a host-owned window-backed swap chain at physical pixel extent.
    HRESULT initializeForCoreWindow(IUnknown*, unsigned width, unsigned height);
    // QI/retain the panel's native interface and attach a host-owned CPU swap chain.
    HRESULT initializeForSwapChainPanel(IUnknown*, unsigned width, unsigned height);
    // Borrow a panel Inspectable through a capable frozen table; engine QIs/AddRefs its needs.
    HRESULT initializeForCompositor(const WKUWPEngineAPI*, WKUWPPageHandle, IUnknown*, double scaleX, double scaleY);
    // Detach the exclusive surface before another mode can attach; Pending retains ownership.
    HRESULT detach();
    // End a Pending engine detach only after the runtime confirms actual native release.
    HRESULT acknowledgeCompositorDetach(uint64_t attachmentId);
    // Render only the attached engine-owned compositor with finite CSS overscroll.
    HRESULT renderCompositor(double overscrollYCssPixels);
    // Validate frozen CPU buffer header, alignment, exact extent, stride and capacity.
    HRESULT present(const WKUWPPaintBuffer&);
    // Upload borrowed premultiplied BGRA pixels and present; no pixel pointer is retained.
    HRESULT present(const unsigned char* bgra, unsigned stride);
    // Rebuild CPU buffers at a checked physical pixel extent, dropping the upload texture.
    HRESULT resize(unsigned width, unsigned height);
    // Query the underlying D3D device removal reason; S_OK when healthy.
    // Detached and engine-owned modes have no host device to query.
    HRESULT deviceRemovedReason();
    // Tear down device-dependent resources and rebuild them on the retained
    // panel and geometry, reattaching exactly once. Rebuilds even when healthy;
    // shells call it after DXGI_ERROR_DEVICE_REMOVED/RESET or to reassert a
    // known-good device. CoreWindow mode has no reattachable panel.
    HRESULT recoverDevice();
    // Completed device rebuilds over this presenter's lifetime; never reset.
    unsigned deviceRecoveries() const { return m_recoveries; }
    // Apply finite positive CSS-to-device scale, bounded by the V1 contract.
    HRESULT setCompositionScale(float scaleX, float scaleY);
    // Apply finite CSS-space overscroll without repainting.
    HRESULT setCompositionOffset(float x, float y);
    // Copy/present the retained CPU upload, or S_FALSE when no upload exists.
    HRESULT presentCachedFrame();
    // Copy CPU submission timing in milliseconds.
    FrameTimingUWP frameTiming() const { return m_timing; }
    // Inspect which exclusive attachment currently owns the visual.
    PresentationModeUWP mode() const { return m_mode; }
    // Capture this GPU attachment's monotonic cookie for a future release acknowledgement.
    uint64_t compositorAttachmentId() const { return m_gpuAttachment; }

private:
    // Enforce owner affinity before argument access/native operation.
    HRESULT checkThread() const;
    // Reject zero/oversized physical edges before DirectX allocation.
    static bool validExtent(unsigned width, unsigned height);
    // Validate finite V1 CSS-to-device scale before touching the panel/provider.
    static bool validScale(double scaleX, double scaleY);
    // Create the BGRA-capable D3D11 device and initialize the native clock rate.
    HRESULT initializeDevice();
    // Build the retained double-buffered flip-sequential BGRA description.
    DXGI_SWAP_CHAIN_DESC1 description(unsigned width, unsigned height) const;
    // Read monotonic seconds from the performance counter for CPU submission timing.
    double now() const;
    // Clear host resources only after successful detach or unpublished setup failure.
    void releaseResources();
    // Owner thread for all DirectX/engine/panel operations.
    DWORD m_owner { GetCurrentThreadId() };
    // Selected driver type; production defaults to hardware.
    D3D_DRIVER_TYPE m_driver;
    // Exclusive active presentation owner.
    PresentationModeUWP m_mode { PresentationModeUWP::Detached };
    // Native CPU panel interface retained after successful QI.
    Microsoft::WRL::ComPtr<ISwapChainPanelNative> m_nativePanel;
    // Host-owned CPU D3D11 device.
    Microsoft::WRL::ComPtr<ID3D11Device> m_device;
    // Host-owned CPU upload/copy immediate context.
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_context;
    // Host-owned CPU flip-sequential swap chain.
    Microsoft::WRL::ComPtr<IDXGISwapChain1> m_swapChain;
    // Host-owned CPU texture retaining uploaded contents, never a caller pixel pointer.
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_upload;
    // Host visual retained in GPU mode; engine separately owns its retained interfaces.
    Microsoft::WRL::ComPtr<IInspectable> m_gpuPanel;
    // Copied negotiated table; caller/runtime owns the process-pinned module.
    WKUWPEngineAPI m_gpuAPI { };
    // Actual page handle paired with the copied GPU table.
    WKUWPPageHandle m_gpuPage { 0 };
    // Native detach was accepted asynchronously; attachment cannot be replaced yet.
    bool m_detachPending { false };
    // Monotonic GPU attachment cookie; retained across detach to reject obsolete acknowledgements.
    uint64_t m_gpuAttachment { 0 };
    // CPU swap-chain physical width and height.
    unsigned m_width { 0 }, m_height { 0 };
    // Completed device rebuilds; a lifetime diagnostic, never reset by detach.
    unsigned m_recoveries { 0 };
    // CSS-to-device scale stored for inverse CPU composition transforms.
    float m_compositionScaleX { 1 }, m_compositionScaleY { 1 };
    // Performance-counter ticks per second.
    double m_frequency { 1 };
    // Last CPU upload/copy/Present call durations in milliseconds.
    FrameTimingUWP m_timing;
};

} // namespace WebKit
