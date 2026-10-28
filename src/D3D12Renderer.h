#pragma once
#include "Common.h"
#include "ShaderTypes.h"

// DirectX 12 renderer:
//   * DirectComposition + premultiplied-alpha composition swapchain for the
//     transparent, click-through desktop overlay.
//   * Automatic fallback to a plain opaque HWND swapchain if DComp is missing.
//   * One fullscreen triangle for the orb, one additive instanced pass for
//     particles, a 256x1 R8 waveform texture and a 512-byte constant buffer.
class D3D12Renderer {
public:
    D3D12Renderer() = default;
    ~D3D12Renderer() { Shutdown(); }

    bool Initialize(HWND hwnd, uint32_t width, uint32_t height, bool transparent);
    bool Resize(uint32_t width, uint32_t height);
    void Render(const OrbConstants& cb, const uint8_t wave[WAVE_SIZE],
                const ParticleGPU* particles, uint32_t particleCount, bool vsync);
    void Shutdown();

    bool OpaqueFallback() const { return opaqueFallback_; }

private:
    bool CreateDeviceAndQueue();
    bool CreateSwapChain();
    bool CreateRenderTargets();
    bool CreateBuffersAndTexture();
    bool CreateRootSignature();
    bool CreatePipelineStates();
    bool CompileShaders();
    ComPtr<ID3DBlob> CompileEntry(const std::wstring& path, const char* entry,
                                  const char* target);

    uint32_t CurrentBackBufferIndex();
    void WaitForFrame(uint32_t index);
    void WaitForGpu();

    HWND hwnd_ = nullptr;
    uint32_t width_ = 0, height_ = 0;
    bool transparent_ = true;
    bool opaqueFallback_ = false;
    bool initialized_ = false;

    ComPtr<IDXGIFactory4> factory_;
    ComPtr<IDXGIAdapter1> adapter_;
    ComPtr<ID3D12Device> device_;
    ComPtr<ID3D12CommandQueue> queue_;
    ComPtr<IDXGISwapChain1> swapChain_;
    ComPtr<IDXGISwapChain3> swapChain3_;

    HMODULE dcompDll_ = nullptr;
    HMODULE compilerDll_ = nullptr;
    ComPtr<IDCompositionDevice> dcompDevice_;
    ComPtr<IDCompositionTarget> dcompTarget_;
    ComPtr<IDCompositionVisual> dcompVisual_;

    ComPtr<ID3D12CommandAllocator> allocators_[FRAME_COUNT];
    ComPtr<ID3D12GraphicsCommandList> commandList_;
    ComPtr<ID3D12DescriptorHeap> rtvHeap_;
    ComPtr<ID3D12DescriptorHeap> srvHeap_;
    ComPtr<ID3D12Resource> renderTargets_[FRAME_COUNT];
    uint32_t rtvStride_ = 0;

    ComPtr<ID3D12RootSignature> rootSignature_;
    ComPtr<ID3D12PipelineState> orbPSO_;
    ComPtr<ID3D12PipelineState> particlePSO_;
    ComPtr<ID3DBlob> orbVS_, orbPS_, particleVS_, particlePS_;

    D3D12_RESOURCE_DESC waveDesc_ = {};
    ComPtr<ID3D12Resource> waveTexture_;
    ComPtr<ID3D12Resource> waveUpload_;
    uint8_t* waveMapped_ = nullptr;

    ComPtr<ID3D12Resource> constantBuffer_;
    uint8_t* cbMapped_ = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS cbGpu_ = 0;

    ComPtr<ID3D12Resource> particleBuffer_;
    uint8_t* particleMapped_ = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS particleGpu_ = 0;
    uint64_t particleSlotSize_ = 0;

    ComPtr<ID3D12Fence> fence_;
    HANDLE fenceEvent_ = nullptr;
    uint64_t fenceValue_ = 0;
    uint64_t fenceValues_[FRAME_COUNT] = {};
    uint32_t frameIndex_ = 0;
};
