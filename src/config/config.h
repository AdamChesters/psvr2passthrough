#pragma once
#include "input_binding.h"
#include <filesystem>
#include <string>
namespace psvr2pt {
struct Config {
    bool enabled = true;
    bool force_passthrough_on = false;
    float global_alpha = 1.f;
    bool toggle_mode = false;
    PassthroughBinding passthrough_binding{};
};
std::string config_to_json(const Config& config);
bool config_from_json(const std::string& json, Config& out);
// Separate beta settings; first load imports supported keys from config.json.
std::filesystem::path config_file_path();
Config load_config();
bool save_config(const Config& config);
} // namespace psvr2pt
