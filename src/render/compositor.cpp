#include "compositor.h"
#include "logging.h"
#include <d3dcompiler.h>
#include <cstring>

namespace psvr2pt {
namespace {
constexpr const char* shader = R"HLSL(
cbuffer Params : register(b0) {
    float4 bounds;
    float4 controls; // opacity, decode source sRGB, unused, unused
};
struct Vertex { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
Vertex VS(uint id : SV_VertexID) {
    Vertex v;
    float2 uv = float2((id << 1) & 2, id & 2);
    v.position = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
    v.uv = lerp(bounds.xy, bounds.zw, uv);
    return v;
}
Texture2D camera : register(t0);
SamplerState cameraSampler : register(s0);
float3 linearize(float3 c) {
    return float3(c.r <= 0.04045 ? c.r/12.92 : pow((c.r+0.055)/1.055, 2.4),
                  c.g <= 0.04045 ? c.g/12.92 : pow((c.g+0.055)/1.055, 2.4),
                  c.b <= 0.04045 ? c.b/12.92 : pow((c.b+0.055)/1.055, 2.4));
}
float4 PS(Vertex v) : SV_TARGET {
    // The provider already applies tone mapping and undistortion. Only
    // transfer-function conversion and premultiplied opacity happen here.
    float4 c = camera.Sample(cameraSampler, v.uv);
    float a = c.a * controls.x;
    float3 rgb = controls.y > 0.5 ? linearize(c.rgb) : c.rgb;
    return float4(rgb * a, a);
}
)HLSL";
ComPtr<ID3DBlob> compile(const char* entry, const char* profile) {
    ComPtr<ID3DBlob> blob, error;
    const HRESULT hr = D3DCompile(shader, std::strlen(shader), nullptr, nullptr, nullptr, entry, profile,
        D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &blob, &error);
    if (FAILED(hr)) throw std::runtime_error(error ? static_cast<const char*>(error->GetBufferPointer()) : "Shader compilation failed");
    return blob;
}
struct Params { float bounds[4]; float controls[4]; };
// Swap the complete pipeline state, including stages not used by this pass.
// This keeps our injection from changing the application's next frame.
struct ContextGuard {
    ID3D11DeviceContext1* context;
    ComPtr<ID3DDeviceContextState> previous;
    ContextGuard(ID3D11DeviceContext1* c, ID3DDeviceContextState* state) : context(c) {
        context->SwapDeviceContextState(state, &previous);
    }
    ~ContextGuard() { context->SwapDeviceContextState(previous.Get(), nullptr); }
};
}

bool Compositor::initialise(ID3D11Device* device, UINT width, UINT height, DXGI_FORMAT format) {
    try {
        device_ = device;
        width_=width; height_=height;
        if (!params_) {
            // A failed shader/state setup must be retryable from a clean state.
            context_.Reset(); state_.Reset(); vs_.Reset(); ps_.Reset();
            sampler_.Reset(); raster_.Reset();
            ComPtr<ID3D11Device1> d1;
            check_hr(device_->QueryInterface(IID_PPV_ARGS(&d1)), "Query Device1");
            d1->GetImmediateContext1(&context_);
            D3D_FEATURE_LEVEL level = device_->GetFeatureLevel(), chosen{};
            check_hr(d1->CreateDeviceContextState(0, &level, 1, D3D11_SDK_VERSION,
                __uuidof(ID3D11Device), &chosen, &state_), "CreateDeviceContextState");
            const auto vs=compile("VS", "vs_5_0"), ps=compile("PS", "ps_5_0");
            check_hr(device_->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &vs_), "CreateVertexShader");
            check_hr(device_->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &ps_), "CreatePixelShader");
            D3D11_SAMPLER_DESC sampler{};
            sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
            sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
            sampler.MaxLOD=D3D11_FLOAT32_MAX;
            check_hr(device_->CreateSamplerState(&sampler, &sampler_), "CreateSamplerState");
            D3D11_RASTERIZER_DESC raster{};
            raster.FillMode=D3D11_FILL_SOLID; raster.CullMode=D3D11_CULL_NONE; raster.DepthClipEnable=TRUE;
            check_hr(device_->CreateRasterizerState(&raster, &raster_), "CreateRasterizerState");
            D3D11_BUFFER_DESC buffer{};
            buffer.ByteWidth=sizeof(Params); buffer.Usage=D3D11_USAGE_DYNAMIC;
            buffer.BindFlags=D3D11_BIND_CONSTANT_BUFFER; buffer.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
            check_hr(device_->CreateBuffer(&buffer,nullptr,&params_), "CreateBuffer");
        }
        for (auto& eye : eyes_) {
            eye.rtv.Reset(); eye.texture.Reset();
            D3D11_TEXTURE2D_DESC td{};
            td.Width=width; td.Height=height; td.MipLevels=1; td.ArraySize=1;
            td.Format=format; td.SampleDesc.Count=1; td.Usage=D3D11_USAGE_DEFAULT;
            td.BindFlags=D3D11_BIND_RENDER_TARGET;
            check_hr(device_->CreateTexture2D(&td,nullptr,&eye.texture), "CreateTexture2D");
            check_hr(device_->CreateRenderTargetView(eye.texture.Get(),nullptr,&eye.rtv), "CreateRenderTargetView");
        }
        return true;
    } catch(const std::exception& e) { PT_LOG_ERROR("Camera compositor: {}",e.what()); return false; }
}

bool Compositor::render(const CameraFrame& frame, const CompositorConfig& config) {
    if (!context_ || !frame.texture) return false;
    try {
        D3D11_SHADER_RESOURCE_VIEW_DESC desc{}; frame.texture->GetDesc(&desc);
        if (desc.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D) return false;
        const bool srgb = desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        if (!srgb && desc.Format != DXGI_FORMAT_R8G8B8A8_UNORM && desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM)
            throw std::runtime_error("Unsupported camera texture colour format");
        ContextGuard guard(context_.Get(),state_.Get());
        context_->IASetInputLayout(nullptr);
        context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context_->VSSetShader(vs_.Get(),nullptr,0); context_->PSSetShader(ps_.Get(),nullptr,0);
        context_->GSSetShader(nullptr,nullptr,0); context_->HSSetShader(nullptr,nullptr,0); context_->DSSetShader(nullptr,nullptr,0);
        context_->RSSetState(raster_.Get());
        D3D11_VIEWPORT viewport{0,0,static_cast<float>(width_),static_cast<float>(height_),0,1};
        context_->RSSetViewports(1,&viewport);
        context_->OMSetBlendState(nullptr,nullptr,0xffffffff);
        context_->PSSetSamplers(0,1,sampler_.GetAddressOf());
        context_->PSSetShaderResources(0,1,frame.texture.GetAddressOf());
        context_->VSSetConstantBuffers(0,1,params_.GetAddressOf()); context_->PSSetConstantBuffers(0,1,params_.GetAddressOf());
        const float mid=(frame.bounds[0]+frame.bounds[2])*0.5f;
        for (int eye=0; eye<2; ++eye) {
            Params p{{eye==0?frame.bounds[0]:mid,frame.bounds[1],eye==0?mid:frame.bounds[2],frame.bounds[3]},
                     {config.global_alpha,srgb?0.f:1.f,0,0}};
            D3D11_MAPPED_SUBRESOURCE mapped{};
            check_hr(context_->Map(params_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped),"Map constants");
            std::memcpy(mapped.pData,&p,sizeof(p)); context_->Unmap(params_.Get(),0);
            context_->OMSetRenderTargets(1,eyes_[eye].rtv.GetAddressOf(),nullptr);
            context_->Draw(3,0);
        }
        ID3D11ShaderResourceView* null_srv=nullptr;
        context_->PSSetShaderResources(0,1,&null_srv);
        context_->OMSetRenderTargets(0,nullptr,nullptr);
        return true;
    } catch(const std::exception& e) { PT_LOG_ERROR("Camera rendering failed: {}",e.what()); return false; }
}
} // namespace psvr2pt
