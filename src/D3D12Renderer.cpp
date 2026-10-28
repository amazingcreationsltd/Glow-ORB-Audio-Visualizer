#include "D3D12Renderer.h"
#include "Util.h"

namespace {

D3D12_HEAP_PROPERTIES HeapProps(D3D12_HEAP_TYPE type) {
    D3D12_HEAP_PROPERTIES p = {};
    p.Type = type;
    p.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    p.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    p.CreationNodeMask = 1;
    p.VisibleNodeMask = 1;
    return p;
}

D3D12_RESOURCE_DESC BufferDesc(uint64_t size) {
    D3D12_RESOURCE_DESC d = {};
    d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    d.Alignment = 0;
    d.Width = size;
    d.Height = 1;
    d.DepthOrArraySize = 1;
    d.MipLevels = 1;
    d.Format = DXGI_FORMAT_UNKNOWN;
    d.SampleDesc.Count = 1;
    d.SampleDesc.Quality = 0;
    d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    d.Flags = D3D12_RESOURCE_FLAG_NONE;
    return d;
}

uint64_t AlignUp(uint64_t v, uint64_t a) { return (v + a - 1) / a * a; }

void Transition(ID3D12GraphicsCommandList* cmd, ID3D12Resource* res,
                D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER b = {};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    b.Transition.pResource = res;
    b.Transition.StateBefore = before;
    b.Transition.StateAfter = after;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmd->ResourceBarrier(1, &b);
}

} // namespace

uint32_t D3D12Renderer::CurrentBackBufferIndex() {
    if (swapChain3_) return swapChain3_->GetCurrentBackBufferIndex();
    return frameIndex_ % FRAME_COUNT;
}

bool D3D12Renderer::Initialize(HWND hwnd, uint32_t width, uint32_t height,
                               bool transparent) {
    hwnd_ = hwnd;
    width_ = std::max(1u, width);
    height_ = std::max(1u, height);
    transparent_ = transparent;
    opaqueFallback_ = false;

    if (!CreateDeviceAndQueue()) return false;
    if (!CreateSwapChain()) return false;
    if (!CreateRenderTargets()) return false;
    if (!CreateBuffersAndTexture()) return false;
    if (!CompileShaders()) return false;
    if (!CreateRootSignature()) return false;
    if (!CreatePipelineStates()) return false;

    initialized_ = true;
    Glow::Log(L"[GlowOrb] D3D12 renderer ready (%ux%u, %s)", width_, height_,
              opaqueFallback_ ? L"opaque HWND swapchain" : L"premultiplied DComp overlay");
    return true;
}

bool D3D12Renderer::CreateDeviceAndQueue() {
    UINT flags = 0;
    HR_RETURN_FALSE(CreateDXGIFactory2(flags, GLOW_PPV(IDXGIFactory4, factory_)));

    bool found = false;
    for (UINT i = 0; factory_->EnumAdapters1(i, adapter_.GetAddressOf()) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 desc = {};
        adapter_->GetDesc1(&desc);
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
        if (SUCCEEDED(D3D12CreateDevice(adapter_.Get(), D3D_FEATURE_LEVEL_11_0,
                                        __uuidof(ID3D12Device), nullptr))) {
            found = true;
            break;
        }
    }
    if (!found) {
        Glow::Log(L"[GlowOrb] No hardware D3D12 adapter found; using default device");
        adapter_.Reset();
    }

    HR_RETURN_FALSE(D3D12CreateDevice(adapter_.Get(), D3D_FEATURE_LEVEL_11_0,
                                      GLOW_PPV(ID3D12Device, device_)));

    D3D12_COMMAND_QUEUE_DESC q = {};
    q.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    q.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    q.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    q.NodeMask = 0;
    HR_RETURN_FALSE(device_->CreateCommandQueue(&q, GLOW_PPV(ID3D12CommandQueue, queue_)));

    for (uint32_t i = 0; i < FRAME_COUNT; ++i) {
        HR_RETURN_FALSE(device_->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT, GLOW_PPV(ID3D12CommandAllocator, allocators_[i])));
    }
    HR_RETURN_FALSE(device_->CreateCommandList(
        0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocators_[0].Get(), nullptr,
        GLOW_PPV(ID3D12GraphicsCommandList, commandList_)));
    commandList_->Close();

    HR_RETURN_FALSE(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                         GLOW_PPV(ID3D12Fence, fence_)));
    fenceEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!fenceEvent_) return false;
    return true;
}

bool D3D12Renderer::CreateSwapChain() {
    DXGI_SWAP_CHAIN_DESC1 desc = {};
    desc.Width = width_;
    desc.Height = height_;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.Stereo = FALSE;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = FRAME_COUNT;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.AlphaMode = transparent_ ? DXGI_ALPHA_MODE_PREMULTIPLIED : DXGI_ALPHA_MODE_IGNORE;
    desc.Flags = 0;

    if (transparent_) {
        // Alpha-blended swapchains can ONLY be created for composition.
        // (CreateSwapChainForHwnd returns DXGI_ERROR_INVALID_CALL otherwise.)
        HRESULT hr = factory_->CreateSwapChainForComposition(
            queue_.Get(), &desc, nullptr, swapChain_.GetAddressOf());
        if (FAILED(hr)) {
            Glow::Log(L"[GlowOrb] FLIP_DISCARD composition swapchain failed (%s); trying FLIP_SEQUENTIAL",
                      Glow::HrToString(hr).c_str());
            desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
            hr = factory_->CreateSwapChainForComposition(
                queue_.Get(), &desc, nullptr, swapChain_.GetAddressOf());
        }

        if (SUCCEEDED(hr)) {
            dcompDll_ = LoadLibraryW(L"dcomp.dll");
            using CreateDeviceFn = HRESULT(WINAPI*)(IDXGIDevice*, REFIID, void**);
            auto createDevice = dcompDll_
                ? reinterpret_cast<CreateDeviceFn>(GetProcAddress(dcompDll_, "DCompositionCreateDevice"))
                : nullptr;
            if (createDevice) {
                // A null rendering device is fine - we only need visuals and
                // SetContent(swapchain), not DComp surfaces.
                hr = createDevice(nullptr, __uuidof(IDCompositionDevice),
                                  reinterpret_cast<void**>(dcompDevice_.GetAddressOf()));
            } else {
                hr = E_FAIL;
            }
            if (SUCCEEDED(hr)) {
                hr = dcompDevice_->CreateTargetForHwnd(hwnd_, TRUE,
                                                       dcompTarget_.GetAddressOf());
            }
            if (SUCCEEDED(hr)) {
                hr = dcompDevice_->CreateVisual(dcompVisual_.GetAddressOf());
            }
            if (SUCCEEDED(hr)) {
                hr = dcompVisual_->SetContent(swapChain_.Get());
            }
            if (SUCCEEDED(hr)) {
                hr = dcompTarget_->SetRoot(dcompVisual_.Get());
            }
            if (SUCCEEDED(hr)) {
                hr = dcompDevice_->Commit();
            }
        }

        if (FAILED(hr)) {
            Glow::Log(L"[GlowOrb] DirectComposition unavailable (%s). "
                      L"Falling back to an opaque window - use --opaque to silence this.",
                      Glow::HrToString(hr).c_str());
            swapChain_.Reset();
            dcompVisual_.Reset();
            dcompTarget_.Reset();
            dcompDevice_.Reset();
            opaqueFallback_ = true;
        } else {
            return true;
        }
    } else {
        opaqueFallback_ = true;
    }

    // Opaque fallback: ordinary HWND swapchain, alpha ignored.
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    HRESULT hr = factory_->CreateSwapChainForHwnd(
        queue_.Get(), hwnd_, &desc, nullptr, nullptr, swapChain_.GetAddressOf());
    if (FAILED(hr)) {
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        HR_RETURN_FALSE(factory_->CreateSwapChainForHwnd(
            queue_.Get(), hwnd_, &desc, nullptr, nullptr, swapChain_.GetAddressOf()));
    }
    factory_->MakeWindowAssociation(hwnd_, DXGI_MWA_NO_ALT_ENTER);
    return true;
}

bool D3D12Renderer::CreateRenderTargets() {
    D3D12_DESCRIPTOR_HEAP_DESC heap = {};
    heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    heap.NumDescriptors = FRAME_COUNT;
    heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    heap.NodeMask = 0;
    HR_RETURN_FALSE(device_->CreateDescriptorHeap(&heap, GLOW_PPV(ID3D12DescriptorHeap, rtvHeap_)));
    rtvStride_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    D3D12_CPU_DESCRIPTOR_HANDLE handle = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    for (uint32_t i = 0; i < FRAME_COUNT; ++i) {
        HR_RETURN_FALSE(swapChain_->GetBuffer(i, GLOW_PPV(ID3D12Resource, renderTargets_[i])));
        D3D12_RENDER_TARGET_VIEW_DESC rtv = {};
        rtv.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        rtv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        rtv.Texture2D.MipSlice = 0;
        rtv.Texture2D.PlaneSlice = 0;
        device_->CreateRenderTargetView(renderTargets_[i].Get(), &rtv, handle);
        handle.ptr += rtvStride_;
    }
    swapChain_.As(swapChain3_);
    frameIndex_ = CurrentBackBufferIndex();
    return true;
}

bool D3D12Renderer::CreateBuffersAndTexture() {
    // --- Waveform texture (256x1 R8_UNORM) + SRV heap -----------------------
    waveDesc_ = {};
    waveDesc_.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    waveDesc_.Alignment = 0;
    waveDesc_.Width = WAVE_SIZE;
    waveDesc_.Height = 1;
    waveDesc_.DepthOrArraySize = 1;
    waveDesc_.MipLevels = 1;
    waveDesc_.Format = DXGI_FORMAT_R8_UNORM;
    waveDesc_.SampleDesc.Count = 1;
    waveDesc_.SampleDesc.Quality = 0;
    waveDesc_.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    waveDesc_.Flags = D3D12_RESOURCE_FLAG_NONE;

    const D3D12_HEAP_PROPERTIES defaultHeap = HeapProps(D3D12_HEAP_TYPE_DEFAULT);
    const D3D12_HEAP_PROPERTIES uploadHeap = HeapProps(D3D12_HEAP_TYPE_UPLOAD);

    HR_RETURN_FALSE(device_->CreateCommittedResource(
        &defaultHeap, D3D12_HEAP_FLAG_NONE, &waveDesc_,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr,
        GLOW_PPV(ID3D12Resource, waveTexture_)));

    D3D12_DESCRIPTOR_HEAP_DESC srvHeap = {};
    srvHeap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeap.NumDescriptors = 1;
    srvHeap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    srvHeap.NodeMask = 0;
    HR_RETURN_FALSE(device_->CreateDescriptorHeap(&srvHeap, GLOW_PPV(ID3D12DescriptorHeap, srvHeap_)));

    D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
    srv.Format = DXGI_FORMAT_R8_UNORM;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels = 1;
    srv.Texture2D.MostDetailedMip = 0;
    srv.Texture2D.PlaneSlice = 0;
    srv.Texture2D.ResourceMinLODClamp = 0.0f;
    device_->CreateShaderResourceView(waveTexture_.Get(), &srv,
                                      srvHeap_->GetCPUDescriptorHandleForHeapStart());

    // 512 bytes per frame slot (texture placed-footprints want 512-byte alignment)
    D3D12_RESOURCE_DESC waveUploadDesc = BufferDesc(512 * FRAME_COUNT);
    HR_RETURN_FALSE(device_->CreateCommittedResource(
        &uploadHeap, D3D12_HEAP_FLAG_NONE, &waveUploadDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        GLOW_PPV(ID3D12Resource, waveUpload_)));
    HR_RETURN_FALSE(waveUpload_->Map(0, nullptr, reinterpret_cast<void**>(&waveMapped_)));
    std::memset(waveMapped_, 128, static_cast<size_t>(512 * FRAME_COUNT));

    // --- Constant buffer (512 bytes x FRAME_COUNT) ---------------------------
    D3D12_RESOURCE_DESC cbDesc = BufferDesc(sizeof(OrbConstants) * FRAME_COUNT);
    HR_RETURN_FALSE(device_->CreateCommittedResource(
        &uploadHeap, D3D12_HEAP_FLAG_NONE, &cbDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        GLOW_PPV(ID3D12Resource, constantBuffer_)));
    HR_RETURN_FALSE(constantBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&cbMapped_)));
    cbGpu_ = constantBuffer_->GetGPUVirtualAddress();

    // --- Particle structured buffer -----------------------------------------
    particleSlotSize_ = AlignUp(static_cast<uint64_t>(MAX_PARTICLES) * sizeof(ParticleGPU), 256);
    D3D12_RESOURCE_DESC partDesc = BufferDesc(particleSlotSize_ * FRAME_COUNT);
    HR_RETURN_FALSE(device_->CreateCommittedResource(
        &uploadHeap, D3D12_HEAP_FLAG_NONE, &partDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        GLOW_PPV(ID3D12Resource, particleBuffer_)));
    HR_RETURN_FALSE(particleBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&particleMapped_)));
    particleGpu_ = particleBuffer_->GetGPUVirtualAddress();
    std::memset(particleMapped_, 0, static_cast<size_t>(particleSlotSize_ * FRAME_COUNT));

    return true;
}

ComPtr<ID3DBlob> D3D12Renderer::CompileEntry(const std::wstring& path,
                                              const char* entry, const char* target) {
    ComPtr<ID3DBlob> blob;
    if (!compilerDll_) return blob;
    using CompileFn = HRESULT(WINAPI*)(LPCWSTR, const D3D_SHADER_MACRO*, ID3DInclude*,
                                       LPCSTR, LPCSTR, UINT, UINT,
                                       ID3DBlob**, ID3DBlob**);
    auto fn = reinterpret_cast<CompileFn>(GetProcAddress(compilerDll_, "D3DCompileFromFile"));
    if (!fn) {
        Glow::Log(L"[GlowOrb] D3DCompileFromFile not found in d3dcompiler_47.dll");
        return blob;
    }

    ComPtr<ID3DBlob> errors;
    const UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
    HRESULT hr = fn(path.c_str(), nullptr, nullptr, entry, target, flags, 0,
                    blob.GetAddressOf(), errors.GetAddressOf());
    if (FAILED(hr) || !blob) {
        if (errors && errors->GetBufferPointer()) {
            std::string msg(static_cast<const char*>(errors->GetBufferPointer()),
                            errors->GetBufferSize());
            Glow::Log(L"[GlowOrb] HLSL compile failed (%s / %s):\n%s",
                      Glow::Widen(entry).c_str(), Glow::Widen(target).c_str(),
                      Glow::Widen(msg).c_str());
        } else {
            Glow::Log(L"[GlowOrb] HLSL compile failed (%s / %s): %s",
                      Glow::Widen(entry).c_str(), Glow::Widen(target).c_str(),
                      Glow::HrToString(hr).c_str());
        }
        blob.Reset();
    }
    return blob;
}

bool D3D12Renderer::CompileShaders() {
    const std::wstring path = Glow::FindShaderFile();
    if (path.empty()) {
        Glow::Fatal(L"GlowOrb - missing shader",
                    L"Could not find shaders\\Orb.hlsl next to GlowOrb.exe.\n"
                    L"Copy the 'shaders' folder into the same directory as the executable.");
        return false;
    }
    Glow::Log(L"[GlowOrb] Compiling %s", path.c_str());

    compilerDll_ = LoadLibraryW(L"d3dcompiler_47.dll");
    if (!compilerDll_) compilerDll_ = LoadLibraryW(L"d3dcompiler_46.dll");
    if (!compilerDll_) compilerDll_ = LoadLibraryW(L"D3DCompiler.dll");
    if (!compilerDll_) {
        Glow::Fatal(L"GlowOrb - missing compiler",
                    L"d3dcompiler_47.dll was not found. Install the Windows 10/11 SDK "
                    L"or the DirectX End-User Runtime.");
        return false;
    }

    orbVS_ = CompileEntry(path, "VSFullscreen", "vs_5_0");
    orbPS_ = CompileEntry(path, "PSOrb", "ps_5_0");
    particleVS_ = CompileEntry(path, "VSParticles", "vs_5_0");
    particlePS_ = CompileEntry(path, "PSParticles", "ps_5_0");
    return orbVS_ && orbPS_ && particleVS_ && particlePS_;
}

bool D3D12Renderer::CreateRootSignature() {
    D3D12_ROOT_PARAMETER params[3] = {};

    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].Descriptor.ShaderRegister = 0;
    params[0].Descriptor.RegisterSpace = 0;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_DESCRIPTOR_RANGE range = {};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 1;
    range.BaseShaderRegister = 0;
    range.RegisterSpace = 0;
    range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges = &range;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    params[2].Descriptor.ShaderRegister = 1;
    params[2].Descriptor.RegisterSpace = 0;
    params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MipLODBias = 0.0f;
    sampler.MaxAnisotropy = 1;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    sampler.MinLOD = 0.0f;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;
    sampler.RegisterSpace = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC desc = {};
    desc.NumParameters = 3;
    desc.pParameters = params;
    desc.NumStaticSamplers = 1;
    desc.pStaticSamplers = &sampler;
    desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> error;
    HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1,
                                             signature.GetAddressOf(), error.GetAddressOf());
    if (FAILED(hr)) {
        if (error && error->GetBufferPointer()) {
            Glow::Log(L"[GlowOrb] Root signature: %s",
                      Glow::Widen(std::string(static_cast<const char*>(error->GetBufferPointer()))).c_str());
        }
        return false;
    }
    HR_RETURN_FALSE(device_->CreateRootSignature(
        0, signature->GetBufferPointer(), signature->GetBufferSize(),
        GLOW_PPV(ID3D12RootSignature, rootSignature_)));
    return true;
}

bool D3D12Renderer::CreatePipelineStates() {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso = {};
    pso.pRootSignature = rootSignature_.Get();
    pso.VS = { orbVS_->GetBufferPointer(), orbVS_->GetBufferSize() };
    pso.PS = { orbPS_->GetBufferPointer(), orbPS_->GetBufferSize() };
    pso.BlendState.AlphaToCoverageEnable = FALSE;
    pso.BlendState.IndependentBlendEnable = FALSE;
    for (auto& rt : pso.BlendState.RenderTarget) {
        rt.BlendEnable = FALSE;
        rt.LogicOpEnable = FALSE;
        rt.SrcBlend = D3D12_BLEND_ONE;
        rt.DestBlend = D3D12_BLEND_ZERO;
        rt.BlendOp = D3D12_BLEND_OP_ADD;
        rt.SrcBlendAlpha = D3D12_BLEND_ONE;
        rt.DestBlendAlpha = D3D12_BLEND_ZERO;
        rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        rt.LogicOp = D3D12_LOGIC_OP_NOOP;
        rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    }
    pso.SampleMask = UINT_MAX;
    pso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pso.RasterizerState.FrontCounterClockwise = FALSE;
    pso.RasterizerState.DepthBias = 0;
    pso.RasterizerState.DepthBiasClamp = 0.0f;
    pso.RasterizerState.SlopeScaledDepthBias = 0.0f;
    pso.RasterizerState.DepthClipEnable = TRUE;
    pso.RasterizerState.MultisampleEnable = FALSE;
    pso.RasterizerState.AntialiasedLineEnable = FALSE;
    pso.RasterizerState.ForcedSampleCount = 0;
    pso.RasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
    pso.DepthStencilState.DepthEnable = FALSE;
    pso.DepthStencilState.StencilEnable = FALSE;
    pso.InputLayout = { nullptr, 0 };
    pso.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0] = DXGI_FORMAT_B8G8R8A8_UNORM;
    pso.DSVFormat = DXGI_FORMAT_UNKNOWN;
    pso.SampleDesc.Count = 1;
    pso.SampleDesc.Quality = 0;
    pso.NodeMask = 0;
    pso.CachedPSO = { nullptr, 0 };
    pso.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

    HR_RETURN_FALSE(device_->CreateGraphicsPipelineState(
        &pso, GLOW_PPV(ID3D12PipelineState, orbPSO_)));

    // Particle pass: additive ONE/ONE on both color and alpha.
    auto& rt = pso.BlendState.RenderTarget[0];
    rt.BlendEnable = TRUE;
    rt.SrcBlend = D3D12_BLEND_ONE;
    rt.DestBlend = D3D12_BLEND_ONE;
    rt.BlendOp = D3D12_BLEND_OP_ADD;
    rt.SrcBlendAlpha = D3D12_BLEND_ONE;
    rt.DestBlendAlpha = D3D12_BLEND_ONE;
    rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    pso.VS = { particleVS_->GetBufferPointer(), particleVS_->GetBufferSize() };
    pso.PS = { particlePS_->GetBufferPointer(), particlePS_->GetBufferSize() };
    HR_RETURN_FALSE(device_->CreateGraphicsPipelineState(
        &pso, GLOW_PPV(ID3D12PipelineState, particlePSO_)));
    return true;
}

void D3D12Renderer::WaitForFrame(uint32_t index) {
    if (fenceValues_[index] == 0) return;
    if (fence_->GetCompletedValue() < fenceValues_[index]) {
        HR_RETURN(fence_->SetEventOnCompletion(fenceValues_[index], fenceEvent_));
        WaitForSingleObject(fenceEvent_, INFINITE);
    }
}

void D3D12Renderer::WaitForGpu() {
    if (!queue_ || !fence_) return;
    const uint64_t value = ++fenceValue_;
    if (SUCCEEDED(queue_->Signal(fence_.Get(), value))) {
        if (fence_->GetCompletedValue() < value) {
            if (SUCCEEDED(fence_->SetEventOnCompletion(value, fenceEvent_))) {
                WaitForSingleObject(fenceEvent_, INFINITE);
            }
        }
    }
}

bool D3D12Renderer::Resize(uint32_t width, uint32_t height) {
    if (!initialized_) return false;
    width = std::max(1u, width);
    height = std::max(1u, height);
    if (width_ == width && height_ == height) return true;
    WaitForGpu();
    width_ = width;
    height_ = height;

    for (auto& target : renderTargets_) target.Reset();
    HRESULT hr = swapChain_->ResizeBuffers(FRAME_COUNT, width_, height_,
                                           DXGI_FORMAT_B8G8R8A8_UNORM, 0);
    if (FAILED(hr)) {
        Glow::Log(L"[GlowOrb] ResizeBuffers failed: %s", Glow::HrToString(hr).c_str());
        return false;
    }
    return CreateRenderTargets();
}

void D3D12Renderer::Render(const OrbConstants& cb, const uint8_t wave[WAVE_SIZE],
                           const ParticleGPU* particles, uint32_t particleCount,
                           bool vsync) {
    if (!initialized_) return;
    frameIndex_ = CurrentBackBufferIndex();
    WaitForFrame(frameIndex_);

    // --- Upload per-frame data ----------------------------------------------
    std::memcpy(cbMapped_ + static_cast<size_t>(frameIndex_) * sizeof(OrbConstants),
                &cb, sizeof(cb));

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout = {};
    UINT rowCount = 0;
    UINT64 rowSize = 0, totalSize = 0;
    device_->GetCopyableFootprints(&waveDesc_, 0, 1,
                                   static_cast<UINT64>(frameIndex_) * 512,
                                   &layout, &rowCount, &rowSize, &totalSize);
    std::memcpy(waveMapped_ + layout.Offset, wave, WAVE_SIZE);

    const uint32_t count = std::min(particleCount, MAX_PARTICLES);
    if (count && particles) {
        std::memcpy(particleMapped_ + frameIndex_ * particleSlotSize_,
                    particles, static_cast<size_t>(count) * sizeof(ParticleGPU));
    }

    // --- Record --------------------------------------------------------------
    HR_RETURN(allocators_[frameIndex_]->Reset());
    HR_RETURN(commandList_->Reset(allocators_[frameIndex_].Get(), orbPSO_.Get()));

    Transition(commandList_.Get(), waveTexture_.Get(),
               D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
               D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12_TEXTURE_COPY_LOCATION src = {};
    src.pResource = waveUpload_.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint = layout;
    D3D12_TEXTURE_COPY_LOCATION dst = {};
    dst.pResource = waveTexture_.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = 0;
    commandList_->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    Transition(commandList_.Get(), waveTexture_.Get(),
               D3D12_RESOURCE_STATE_COPY_DEST,
               D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    Transition(commandList_.Get(), renderTargets_[frameIndex_].Get(),
               D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += static_cast<SIZE_T>(frameIndex_) * rtvStride_;
    const float clear[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    commandList_->ClearRenderTargetView(rtv, clear, 0, nullptr);
    commandList_->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

    D3D12_VIEWPORT viewport = { 0.0f, 0.0f, static_cast<float>(width_),
                                static_cast<float>(height_), 0.0f, 1.0f };
    D3D12_RECT scissor = { 0, 0, static_cast<LONG>(width_), static_cast<LONG>(height_) };
    commandList_->RSSetViewports(1, &viewport);
    commandList_->RSSetScissorRects(1, &scissor);

    ID3D12DescriptorHeap* heaps[] = { srvHeap_.Get() };
    commandList_->SetDescriptorHeaps(1, heaps);
    commandList_->SetGraphicsRootSignature(rootSignature_.Get());
    commandList_->SetGraphicsRootConstantBufferView(
        0, cbGpu_ + static_cast<uint64_t>(frameIndex_) * sizeof(OrbConstants));
    commandList_->SetGraphicsRootDescriptorTable(
        1, srvHeap_->GetGPUDescriptorHandleForHeapStart());
    commandList_->SetGraphicsRootShaderResourceView(
        2, particleGpu_ + static_cast<uint64_t>(frameIndex_) * particleSlotSize_);

    commandList_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList_->SetPipelineState(orbPSO_.Get());
    commandList_->DrawInstanced(3, 1, 0, 0);

    if (count) {
        commandList_->SetPipelineState(particlePSO_.Get());
        commandList_->DrawInstanced(6, count, 0, 0);
    }

    Transition(commandList_.Get(), renderTargets_[frameIndex_].Get(),
               D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);

    HR_RETURN(commandList_->Close());
    ID3D12CommandList* lists[] = { commandList_.Get() };
    queue_->ExecuteCommandLists(1, lists);

    swapChain_->Present(vsync ? 1 : 0, 0);
    fenceValues_[frameIndex_] = ++fenceValue_;
    queue_->Signal(fence_.Get(), fenceValue_);
}

void D3D12Renderer::Shutdown() {
    if (queue_ && fence_) WaitForGpu();
    auto unmap = [](ComPtr<ID3D12Resource>& res, uint8_t*& ptr) {
        if (res && ptr) { res->Unmap(0, nullptr); ptr = nullptr; }
    };
    unmap(constantBuffer_, cbMapped_);
    unmap(waveUpload_, waveMapped_);
    unmap(particleBuffer_, particleMapped_);

    for (auto& target : renderTargets_) target.Reset();
    waveTexture_.Reset();
    waveUpload_.Reset();
    constantBuffer_.Reset();
    particleBuffer_.Reset();
    orbPSO_.Reset();
    particlePSO_.Reset();
    rootSignature_.Reset();
    orbVS_.Reset(); orbPS_.Reset(); particleVS_.Reset(); particlePS_.Reset();
    srvHeap_.Reset();
    rtvHeap_.Reset();
    commandList_.Reset();
    for (auto& a : allocators_) a.Reset();
    fence_.Reset();
    swapChain_.Reset();
    dcompVisual_.Reset();
    dcompTarget_.Reset();
    dcompDevice_.Reset();
    queue_.Reset();
    device_.Reset();
    adapter_.Reset();
    factory_.Reset();
    if (fenceEvent_) { CloseHandle(fenceEvent_); fenceEvent_ = nullptr; }
    if (dcompDll_) { FreeLibrary(dcompDll_); dcompDll_ = nullptr; }
    if (compilerDll_) { FreeLibrary(compilerDll_); compilerDll_ = nullptr; }
    initialized_ = false;
}
