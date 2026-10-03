#pragma once
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

namespace WebKit {

// GPU presentation of Cairo's premultiplied BGRA backing store. UI apartment
// only. Composition entry point is reusable by the future XAML control.
class WebKitWebViewPresenterUWP {
public:
    struct FrameTiming { double uploadMS { 0 }, copyMS { 0 }, presentMS { 0 }; };
    HRESULT initializeForCoreWindow(IUnknown*, unsigned width, unsigned height);
    HRESULT initializeForSwapChainPanel(IUnknown*, unsigned width, unsigned height);
    HRESULT present(const unsigned char* bgra, unsigned stride);
    HRESULT resize(unsigned width, unsigned height);
    HRESULT setCompositionScale(float scaleX, float scaleY);
    HRESULT setCompositionOffset(float x, float y);
    HRESULT presentCachedFrame();
    FrameTiming frameTiming() const { return m_timing; }
private:
    double now() const;
    HRESULT initializeDevice();
    DXGI_SWAP_CHAIN_DESC1 description(unsigned width, unsigned height) const;
    Microsoft::WRL::ComPtr<ID3D11Device> m_device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_context;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> m_swapChain;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_upload;
    unsigned m_width { 0 };
    unsigned m_height { 0 };
    float m_compositionScaleX { 1 }, m_compositionScaleY { 1 };
    double m_frequency { 1 };
    FrameTiming m_timing;
};

}
