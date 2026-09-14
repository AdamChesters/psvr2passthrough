#pragma once

#include <array>
#include <cmath>

namespace psvr2pt {

struct CameraFov { float left{}, right{}, up{}, down{}; };
struct CameraPose {
    std::array<float, 4> orientation{0, 0, 0, 1}; // x, y, z, w
    std::array<float, 3> position{};
};

// OpenVR and OpenXR both use metres, +Y up and -Z forward. These helpers
// convert rigid camera-to-head transforms, not tracking-space origins.
inline bool camera_pose(const float m[3][4], CameraPose& out) {
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 4; ++j) if (!std::isfinite(m[i][j])) return false;
        if (std::abs(m[i][3]) > 0.5f) return false;
        for (int j = 0; j < 3; ++j) {
            float dot = 0;
            for (int k = 0; k < 3; ++k) dot += m[i][k] * m[j][k];
            if (std::abs(dot - (i == j ? 1.f : 0.f)) > 0.02f) return false;
        }
    }
    const float det = m[0][0]*(m[1][1]*m[2][2]-m[1][2]*m[2][1])
                    -m[0][1]*(m[1][0]*m[2][2]-m[1][2]*m[2][0])
                    +m[0][2]*(m[1][0]*m[2][1]-m[1][1]*m[2][0]);
    if (std::abs(det - 1.f) > 0.03f) return false;
    auto& q = out.orientation;
    const float trace = m[0][0] + m[1][1] + m[2][2];
    if (trace > 0) {
        const float s = 2.f * std::sqrt(trace + 1.f);
        q = {(m[2][1]-m[1][2])/s, (m[0][2]-m[2][0])/s, (m[1][0]-m[0][1])/s, s/4.f};
    } else {
        int i = m[1][1] > m[0][0] ? 1 : 0;
        if (m[2][2] > m[i][i]) i = 2;
        const int j = (i+1)%3, k = (i+2)%3;
        const float s = 2.f * std::sqrt(1.f + m[i][i] - m[j][j] - m[k][k]);
        q[i] = s/4.f;
        q[j] = (m[j][i]+m[i][j])/s;
        q[k] = (m[k][i]+m[i][k])/s;
        q[3] = (m[k][j]-m[j][k])/s;
    }
    float norm = 0;
    for (float v : q) norm += v*v;
    for (float& v : q) v /= std::sqrt(norm);
    out.position = {m[0][3], m[1][3], m[2][3]};
    return true;
}

inline CameraPose compose_pose(const CameraPose& head, const CameraPose& camera) {
    CameraPose out;
    const auto& a = head.orientation; const auto& b = camera.orientation;
    out.orientation = {
        a[3]*b[0]+a[0]*b[3]+a[1]*b[2]-a[2]*b[1],
        a[3]*b[1]-a[0]*b[2]+a[1]*b[3]+a[2]*b[0],
        a[3]*b[2]+a[0]*b[1]-a[1]*b[0]+a[2]*b[3],
        a[3]*b[3]-a[0]*b[0]-a[1]*b[1]-a[2]*b[2]};
    const auto& p = camera.position;
    const float t[3] = {2*(a[1]*p[2]-a[2]*p[1]), 2*(a[2]*p[0]-a[0]*p[2]), 2*(a[0]*p[1]-a[1]*p[0])};
    out.position = {head.position[0]+p[0]+a[3]*t[0]+a[1]*t[2]-a[2]*t[1],
                    head.position[1]+p[1]+a[3]*t[1]+a[2]*t[0]-a[0]*t[2],
                    head.position[2]+p[2]+a[3]*t[2]+a[0]*t[1]-a[1]*t[0]};
    return out;
}

inline bool camera_fov(const float m[4][4], CameraFov& out) {
    for (int i=0; i<4; ++i) for (int j=0; j<4; ++j)
        if (!std::isfinite(m[i][j])) return false;
    if (m[0][0] <= 0 || m[1][1] <= 0 || std::abs(m[3][2]+1.f)>0.001f || std::abs(m[3][3])>0.001f)
        return false;
    out = {std::atan((m[0][2]-1.f)/m[0][0]), std::atan((m[0][2]+1.f)/m[0][0]),
           std::atan((m[1][2]+1.f)/m[1][1]), std::atan((m[1][2]-1.f)/m[1][1])};
    return out.left < out.right && out.down < out.up;
}

} // namespace psvr2pt
