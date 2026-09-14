#include "config.h"
#include <windows.h>
#include <shlobj.h>
#include <fstream>
#include <sstream>

namespace psvr2pt {
std::filesystem::path config_file_path() {
    PWSTR path=nullptr;
    std::filesystem::path dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&path))) {
        dir=path; CoTaskMemFree(path);
    } else dir=std::filesystem::temp_directory_path();
    dir/="PSVR2PassthroughLayer";
    std::error_code ec; std::filesystem::create_directories(dir,ec);
    return dir/"config-hybrid-beta.json";
}
Config load_config() {
    Config c;
    auto path=config_file_path();
    // Import once, without overwriting the older build's settings.
    if (!std::filesystem::exists(path)) path=path.parent_path()/"config.json";
    std::ifstream f(path,std::ios::binary);
    if (f) { std::stringstream ss; ss<<f.rdbuf(); config_from_json(ss.str(),c); }
    return c;
}
bool save_config(const Config& c) {
    const auto path=config_file_path();
    const auto temp=path.wstring()+L".tmp";
    try {
        std::ofstream f(std::filesystem::path(temp),std::ios::binary|std::ios::trunc);
        if (!f) return false;
        f<<config_to_json(c); f.close();
        if (!f) return false;
        // Preserve the previous file if replacement fails.
        return MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
    } catch (...) { return false; }
}
} // namespace psvr2pt
