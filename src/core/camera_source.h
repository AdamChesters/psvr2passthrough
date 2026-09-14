#pragma once

#include "camera_geometry.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <memory>
#include <cstdint>

namespace psvr2pt {

struct CameraFrame {
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> texture;
    uint32_t sequence = 0;
    int64_t exposure_qpc = 0;
    uint32_t width = 0, height = 0; // per-eye output, excludes texture padding
    float bounds[4]{0, 0, 1, 1}; // whole stereo image, normalized texture coordinates
    CameraPose camera_to_head[2];
    CameraFov fov[2];
};

// Toolkit is the camera provider. We consume its maximum-undistorted GPU
// texture and associated exposure metadata through SteamVR's client API.
// No Room View/dashboard activation, driver hooks, or CPU frame readback.
class CameraSource {
public:
    CameraSource();
    ~CameraSource();
    CameraSource(const CameraSource&) = delete;
    CameraSource& operator=(const CameraSource&) = delete;
    bool poll(ID3D11Device* device, CameraFrame& frame);
    void stop();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

void write_pipeline_status(const char* message);

} // namespace psvr2pt
