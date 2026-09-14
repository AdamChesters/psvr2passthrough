#pragma once
#include "camera_source.h"
#include "d3d_helpers.h"
#include <d3d11_1.h>

namespace psvr2pt {
struct CompositorConfig { float global_alpha = 1.f; };
struct EyeOutput { ComPtr<ID3D11Texture2D> texture; ComPtr<ID3D11RenderTargetView> rtv; };
class Compositor {
public:
    bool initialise(ID3D11Device* device, UINT width, UINT height, DXGI_FORMAT format);
    bool render(const CameraFrame& frame, const CompositorConfig& config);
    const EyeOutput& eye(int i) const { return eyes_[i]; }
private:
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext1> context_;
    ComPtr<ID3DDeviceContextState> state_;
    ComPtr<ID3D11VertexShader> vs_;
    ComPtr<ID3D11PixelShader> ps_;
    ComPtr<ID3D11SamplerState> sampler_;
    ComPtr<ID3D11RasterizerState> raster_;
    ComPtr<ID3D11Buffer> params_;
    EyeOutput eyes_[2];
    UINT width_ = 0, height_ = 0;
};
} // namespace psvr2pt
