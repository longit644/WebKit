#pragma once
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

namespace WebKit {

// GPU presentation of Cairo's premultiplied BGRA backing store. UI apartment
// only. Composition entry point is reusable by the future XAML control.
class WebKitWebViewPresenterUWP {
public:
    // Per-stage GPU cost of the last presented frame in milliseconds.
    struct FrameTiming {
        // CPU-to-GPU texture upload.
        double uploadMS { 0 };

        // GPU copy from staging texture to the swap-chain buffer.
        double copyMS { 0 };

        // Swap-chain present including vsync wait.
        double presentMS { 0 };
    };

    // Creates a window-backed swap chain on the given CoreWindow.
    HRESULT initializeForCoreWindow(IUnknown*, unsigned width, unsigned height);

    // Creates a composition swap chain attached to the given SwapChainPanel.
    HRESULT initializeForSwapChainPanel(IUnknown*, unsigned width, unsigned height);

    // Uploads one BGRA frame and presents it with vsync.
    HRESULT present(const unsigned char* bgra, unsigned stride);

    // Rebuilds the buffers at the new size, dropping the staging texture.
    HRESULT resize(unsigned width, unsigned height);

    // Maps CSS pixels to backing pixels for high-density displays.
    HRESULT setCompositionScale(float scaleX, float scaleY);

    // Shifts the presented image for edge overscroll without repainting.
    HRESULT setCompositionOffset(float x, float y);

    // Re-presents the last uploaded frame, e.g. after an offset-only change.
    HRESULT presentCachedFrame();

    // Cost breakdown of the last presented frame.
    FrameTiming frameTiming() const { return m_timing; }

private:
    // Seconds since boot from the performance counter for stage timing.
    double now() const;

    // Creates the BGRA-capable D3D11 device, falling back through feature levels.
    HRESULT initializeDevice();

    // Double-buffered flip-sequential description for the given size.
    DXGI_SWAP_CHAIN_DESC1 description(unsigned width, unsigned height) const;

    // D3D11 device backing the swap chain.
    Microsoft::WRL::ComPtr<ID3D11Device> m_device;

    // Immediate context for uploads and copies.
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_context;

    // Flip-sequential swap chain presenting with vsync.
    Microsoft::WRL::ComPtr<IDXGISwapChain1> m_swapChain;

    // Staging texture holding the last uploaded frame.
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_upload;

    // Swap-chain size in physical pixels.
    unsigned m_width { 0 };

    // Swap-chain size in physical pixels.
    unsigned m_height { 0 };

    // CSS-to-backing scale applied by the swap-chain matrix.
    float m_compositionScaleX { 1 }, m_compositionScaleY { 1 };

    // Performance-counter frequency for now().
    double m_frequency { 1 };

    // Cost breakdown of the last presented frame.
    FrameTiming m_timing;
};

}
