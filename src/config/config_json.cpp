#include "config.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>

namespace psvr2pt {
std::string config_to_json(const Config& c) {
    const auto& b=c.passthrough_binding;
    return nlohmann::json{{"schema_version",1},{"enabled",c.enabled},{"force_passthrough_on",c.force_passthrough_on},
        {"global_alpha",c.global_alpha},{"toggle_mode",c.toggle_mode},{"pt_type",static_cast<int>(b.type)},
        {"pt_vk",b.vk_code},{"pt_xi_ctrl",b.xinput_controller},{"pt_xi_btn",b.xinput_button_mask},
        {"pt_di_guid",b.dinput_device_guid},{"pt_di_btn",b.dinput_button_index},{"pt_di_name",b.dinput_device_name}}.dump(2)+"\n";
}
bool config_from_json(const std::string& text,Config& out) {
    try {
        const auto j=nlohmann::json::parse(text);
        if (!j.is_object()) return false;
        Config c;
        c.enabled=j.value("enabled",c.enabled);
        c.force_passthrough_on=j.value("force_passthrough_on",c.force_passthrough_on);
        c.global_alpha=j.value("global_alpha",c.global_alpha);
        if (!std::isfinite(c.global_alpha)) return false;
        c.global_alpha=std::clamp(c.global_alpha,0.f,1.f);
        c.toggle_mode=j.value("toggle_mode",c.toggle_mode);
        auto& b=c.passthrough_binding;
        const int type=j.value("pt_type",0);
        if (type<0 || type>3) return false;
        b.type=static_cast<BindingType>(type);
        b.vk_code=j.value("pt_vk",0);
        b.xinput_controller=j.value("pt_xi_ctrl",0);
        b.xinput_button_mask=j.value("pt_xi_btn",0u);
        b.dinput_device_guid=j.value("pt_di_guid",std::string{});
        b.dinput_button_index=j.value("pt_di_btn",0);
        b.dinput_device_name=j.value("pt_di_name",std::string{});
        if (b.type==BindingType::Keyboard && (b.vk_code<1 || b.vk_code>255)) return false;
        if (b.type==BindingType::XInput && (b.xinput_controller<0 || b.xinput_controller>3 || !b.xinput_button_mask || b.xinput_button_mask>65535)) return false;
        if (b.type==BindingType::DirectInput && (b.dinput_device_guid.empty() || b.dinput_button_index<0 || b.dinput_button_index>127)) return false;
        out=std::move(c); // Removed legacy processing keys are deliberately ignored.
        return true;
    } catch (...) { return false; }
}
} // namespace psvr2pt
