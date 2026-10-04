#include "WebKitWebViewPresenterUWP.h"
#include "PageUWP.h" // shared UWP validation limits (buffer counts, BGRA stride)
#include <cmath>
#include <dxgi1_3.h>
#include <windows.ui.xaml.media.dxinterop.h>

namespace WebKit {
using Microsoft::WRL::ComPtr;

// Double-buffered presentation; the second buffer hides upload latency.
constexpr unsigned kSwapChainBufferCount = 2;
// Present with vsync; flags stay zero (no test, no restart, no stereo).
constexpr unsigned kPresentSyncInterval = 1;
constexpr unsigned kNoDXGIFlags = 0;
constexpr unsigned kFirstSwapChainBuffer = 0;
constexpr unsigned kFirstSubresource = 0;
constexpr unsigned kNoSourceDepthPitch = 0;
constexpr unsigned kSingleMipLevel = 1;
constexpr unsigned kSingleTextureArraySlice = 1;
constexpr unsigned kSingleSampleCount = 1;
constexpr double kMillisecondsPerSecond = 1000.0;
constexpr float kZeroCompositionOffset = 0.0f;

// Creates the hardware D3D11 device and records the performance-counter rate.
HRESULT WebKitWebViewPresenterUWP::initializeDevice()
{
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    m_frequency = static_cast<double>(frequency.QuadPart);
    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0, D3D_FEATURE_LEVEL_9_3 };
    return D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        levels, ARRAYSIZE(levels), D3D11_SDK_VERSION, &m_device, nullptr, &m_context);
}

// Builds the shared flip-sequential BGRA swap-chain description.
DXGI_SWAP_CHAIN_DESC1 WebKitWebViewPresenterUWP::description(unsigned width, unsigned height) const
{
    DXGI_SWAP_CHAIN_DESC1 desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = kSingleSampleCount;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = kSwapChainBufferCount;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    return desc;
}

// Attaches a double-buffered swap chain directly to a CoreWindow.
HRESULT WebKitWebViewPresenterUWP::initializeForCoreWindow(IUnknown* window, unsigned width, unsigned height)
{
    HRESULT result = initializeDevice();
    if (FAILED(result))
        return result;
    ComPtr<IDXGIDevice> device;
    ComPtr<IDXGIAdapter> adapter;
    ComPtr<IDXGIFactory2> factory;
    if (FAILED(result = m_device.As(&device)) || FAILED(result = device->GetAdapter(&adapter))
        || FAILED(result = adapter->GetParent(IID_PPV_ARGS(&factory))))
        return result;
    auto desc = description(width, height);
    result = factory->CreateSwapChainForCoreWindow(m_device.Get(), window, &desc, nullptr, &m_swapChain);
    if (SUCCEEDED(result)) {
        m_width = width;
        m_height = height;
    }
    return result;
}

// Creates a composition swap chain and attaches it to the SwapChainPanel.
HRESULT WebKitWebViewPresenterUWP::initializeForSwapChainPanel(IUnknown* panel, unsigned width, unsigned height)
{
    HRESULT result = initializeDevice();
    if (FAILED(result))
        return result;
    ComPtr<IDXGIDevice> device;
    ComPtr<IDXGIAdapter> adapter;
    ComPtr<IDXGIFactory2> factory;
    ComPtr<ISwapChainPanelNative> nativePanel;
    if (FAILED(result = m_device.As(&device)) || FAILED(result = device->GetAdapter(&adapter))
        || FAILED(result = adapter->GetParent(IID_PPV_ARGS(&factory)))
        || FAILED(result = panel->QueryInterface(IID_PPV_ARGS(&nativePanel))))
        return result;
    auto desc = description(width, height);
    desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    result = factory->CreateSwapChainForComposition(m_device.Get(), &desc, nullptr, &m_swapChain);
    if (FAILED(result))
        return result;
    result = nativePanel->SetSwapChain(m_swapChain.Get());
    if (SUCCEEDED(result)) {
        m_width = width;
        m_height = height;
    }
    return result;
}

// Uploads caller BGRA pixels and presents the new frame with vsync.
HRESULT WebKitWebViewPresenterUWP::present(const unsigned char* bgra, unsigned stride)
{
    if (!m_swapChain || !bgra || stride < m_width * UWPBytesPerBGRA)
        return E_INVALIDARG;
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = m_width;
    desc.Height = m_height;
    desc.MipLevels = kSingleMipLevel;
    desc.ArraySize = kSingleTextureArraySlice;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = kSingleSampleCount;
    desc.Usage = D3D11_USAGE_DEFAULT;
    HRESULT result = S_OK;
    m_timing = {};
    double start = now();
    if (!m_upload) {
        result = m_device->CreateTexture2D(&desc, nullptr, &m_upload);
        if (FAILED(result))
            return result;
    }
    m_context->UpdateSubresource(m_upload.Get(), kFirstSubresource, nullptr, bgra, stride, kNoSourceDepthPitch);
    m_timing.uploadMS = (now() - start) * kMillisecondsPerSecond;
    return presentCachedFrame();
}

// Copies the last upload to the current backbuffer and swaps it to the screen.
HRESULT WebKitWebViewPresenterUWP::presentCachedFrame()
{
    if (!m_swapChain || !m_upload)
        return S_FALSE;
    ComPtr<ID3D11Texture2D> backBuffer;
    HRESULT result = m_swapChain->GetBuffer(kFirstSwapChainBuffer, IID_PPV_ARGS(&backBuffer));
    if (FAILED(result))
        return result;
    double start = now();
    m_context->CopyResource(backBuffer.Get(), m_upload.Get());
    m_timing.copyMS = (now() - start) * kMillisecondsPerSecond;
    start = now();
    result = m_swapChain->Present(kPresentSyncInterval, kNoDXGIFlags);
    m_timing.presentMS = (now() - start) * kMillisecondsPerSecond;
    return result;
}

// Seconds since boot from the performance counter for stage timing.
double WebKitWebViewPresenterUWP::now() const
{
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return static_cast<double>(counter.QuadPart) / m_frequency;
}

// Resizes swap-chain buffers and drops the old staging texture.
HRESULT WebKitWebViewPresenterUWP::resize(unsigned width, unsigned height)
{
    if (!m_swapChain || !width || !height)
        return E_INVALIDARG;
    m_upload.Reset();
    HRESULT result = m_swapChain->ResizeBuffers(kSwapChainBufferCount, width, height, DXGI_FORMAT_B8G8R8A8_UNORM, kNoDXGIFlags);
    if (SUCCEEDED(result)) {
        m_width = width;
        m_height = height;
    }
    return result;
}

// Applies inverse DPI scale so logical XAML size maps to physical swap-chain pixels.
HRESULT WebKitWebViewPresenterUWP::setCompositionScale(float scaleX, float scaleY)
{
    if (!m_swapChain || !std::isfinite(scaleX) || !std::isfinite(scaleY) || scaleX <= 0 || scaleY <= 0)
        return E_INVALIDARG;
    ComPtr<IDXGISwapChain2> swapChain;
    HRESULT result = m_swapChain.As(&swapChain);
    if (FAILED(result))
        return result;
    DXGI_MATRIX_3X2_F matrix = { 1 / scaleX, kZeroCompositionOffset, kZeroCompositionOffset, 1 / scaleY, kZeroCompositionOffset, kZeroCompositionOffset };
    result = swapChain->SetMatrixTransform(&matrix);
    if (SUCCEEDED(result)) {
        m_compositionScaleX = scaleX;
        m_compositionScaleY = scaleY;
    }
    return result;
}

// Applies an offset without repainting; used for edge overscroll spring motion.
HRESULT WebKitWebViewPresenterUWP::setCompositionOffset(float x, float y)
{
    if (!m_swapChain || !std::isfinite(x) || !std::isfinite(y))
        return E_INVALIDARG;
    ComPtr<IDXGISwapChain2> swapChain;
    HRESULT result = m_swapChain.As(&swapChain);
    if (FAILED(result))
        return result;
    DXGI_MATRIX_3X2_F matrix = { 1 / m_compositionScaleX, kZeroCompositionOffset, kZeroCompositionOffset, 1 / m_compositionScaleY, x, y };
    return swapChain->SetMatrixTransform(&matrix);
}
}
