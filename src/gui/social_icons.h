#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <string>
#include <unordered_map>

namespace psvr2pt {

// Resource ID of the PNG copy embedded for a support content icon path
// (content::socials[].icon), or 0 when that logo is not embedded.
int social_icon_resource(const char* icon);

// Textures for the support panel's social buttons, decoded from the PNG copies
// embedded in the executable. Each logo is loaded once; a logo that cannot be
// loaded returns nullptr and the panel shows its label instead.
class SocialIcons {
public:
    void* get(ID3D11Device* device, const char* icon);
    void  clear() { cache_.clear(); }

private:
    std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>> cache_;
};

}  // namespace psvr2pt
