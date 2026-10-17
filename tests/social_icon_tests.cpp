#include "social_icons.h"
#include "support_content.hpp"

#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <iostream>
#include <iterator>
#include <stdexcept>

static void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

int main() {
    using Microsoft::WRL::ComPtr;
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    try {
        check(SUCCEEDED(com), "COM initialises");
        ComPtr<ID3D11Device> device;
        check(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
                                          D3D11_SDK_VERSION, &device, nullptr, nullptr)),
              "WARP device is available");
        psvr2pt::SocialIcons icons;
        for (const auto& social : adamch_support::content::socials) {
            check(psvr2pt::social_icon_resource(social.icon) != 0, "every support social logo is embedded");
            auto* view = static_cast<ID3D11ShaderResourceView*>(icons.get(device.Get(), social.icon));
            check(view != nullptr, "every embedded social logo loads as a texture");
            check(icons.get(device.Get(), social.icon) == view, "each logo is loaded once");
            ComPtr<ID3D11Resource> resource;
            view->GetResource(&resource);
            ComPtr<ID3D11Texture2D> texture;
            check(SUCCEEDED(resource.As(&texture)), "logo view is a 2D texture");
            D3D11_TEXTURE2D_DESC desc{};
            texture->GetDesc(&desc);
            check(desc.Width == 64 && desc.Height == 64 && desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM,
                  "logo texture is 64x64 RGBA");
        }
        check(icons.get(device.Get(), "assets/unknown.svg") == nullptr, "an unknown logo falls back to its label");
        check(icons.get(nullptr, adamch_support::content::socials[0].icon) == nullptr, "no device means no texture");
        icons.clear();
        std::cout << "PASS: " << std::size(adamch_support::content::socials) << " embedded social logos load as textures\n";
        if (SUCCEEDED(com)) CoUninitialize();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        if (SUCCEEDED(com)) CoUninitialize();
        return 1;
    }
}
