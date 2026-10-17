#include "social_icons.h"

#include <windows.h>
#include <wincodec.h>

#include <cstring>
#include <vector>

namespace psvr2pt {

namespace {

using Microsoft::WRL::ComPtr;

// Keep in step with resources.rc.in.
struct EmbeddedIcon { const char* icon; int id; };
constexpr EmbeddedIcon kEmbeddedIcons[] = {
    {"assets/discord.svg",     203},
    {"assets/reddit.svg",      204},
    {"assets/github-mark.svg", 205},
    {"assets/linkedin.svg",    206},
    {"assets/website.svg",     207},
};

ComPtr<ID3D11ShaderResourceView> load_png_resource(ID3D11Device* device, int id) {
    HMODULE module = GetModuleHandleW(nullptr);
    HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(id), RT_RCDATA);
    if (!resource) return {};
    HGLOBAL memory = LoadResource(module, resource);
    auto* bytes = memory ? static_cast<BYTE*>(LockResource(memory)) : nullptr;
    const DWORD size = SizeofResource(module, resource);
    if (!bytes || size == 0) return {};

    ComPtr<IWICImagingFactory>    factory;
    ComPtr<IWICStream>            stream;
    ComPtr<IWICBitmapDecoder>     decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter>   converter;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) ||
        FAILED(factory->CreateStream(&stream)) ||
        FAILED(stream->InitializeFromMemory(bytes, size)) ||
        FAILED(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &decoder)) ||
        FAILED(decoder->GetFrame(0, &frame)))
        return {};

    UINT width = 0, height = 0;
    if (FAILED(frame->GetSize(&width, &height)) || width == 0 || height == 0 || width > 512 || height > 512)
        return {};
    if (FAILED(factory->CreateFormatConverter(&converter)) ||
        FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone,
                                     nullptr, 0.0, WICBitmapPaletteTypeCustom)))
        return {};
    std::vector<BYTE> pixels(size_t(width) * height * 4);
    if (FAILED(converter->CopyPixels(nullptr, width * 4, UINT(pixels.size()), pixels.data())))
        return {};

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width            = width;
    desc.Height           = height;
    desc.MipLevels        = 1;
    desc.ArraySize        = 1;
    desc.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage            = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags        = D3D11_BIND_SHADER_RESOURCE;
    const D3D11_SUBRESOURCE_DATA data{pixels.data(), width * 4, 0};
    ComPtr<ID3D11Texture2D>          texture;
    ComPtr<ID3D11ShaderResourceView> view;
    if (FAILED(device->CreateTexture2D(&desc, &data, &texture)) ||
        FAILED(device->CreateShaderResourceView(texture.Get(), nullptr, &view)))
        return {};
    return view;
}

}  // namespace

int social_icon_resource(const char* icon) {
    if (!icon) return 0;
    for (const auto& embedded : kEmbeddedIcons)
        if (std::strcmp(embedded.icon, icon) == 0) return embedded.id;
    return 0;
}

void* SocialIcons::get(ID3D11Device* device, const char* icon) {
    if (!device || !icon) return nullptr;
    auto found = cache_.find(icon);
    if (found == cache_.end()) {
        const int id = social_icon_resource(icon);
        found = cache_.emplace(icon, id ? load_png_resource(device, id) : nullptr).first;
    }
    return found->second.Get();
}

}  // namespace psvr2pt
