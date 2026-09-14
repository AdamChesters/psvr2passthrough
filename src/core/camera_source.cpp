#include "camera_source.h"
#include "logging.h"
#include <openvr.h>
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <mutex>
#include <stdexcept>
#include <limits>

namespace psvr2pt {
namespace {
using Clock = std::chrono::steady_clock;
constexpr auto frame_type = vr::VRTrackedCameraFrameType_MaximumUndistorted;
int dll_location_anchor;

struct Connection {
    HMODULE module = nullptr;
    using Init = uint32_t (__cdecl*)(vr::EVRInitError*, vr::EVRApplicationType, const char*);
    using Shutdown = void (__cdecl*)();
    using Generic = void* (__cdecl*)(const char*, vr::EVRInitError*);
    using Token = uint32_t (__cdecl*)();
    Shutdown shutdown = nullptr;
    Token token = nullptr;
    vr::IVRSystem* system = nullptr;
    vr::IVRTrackedCamera* camera = nullptr;
    uint32_t generation = 0;
    bool owns_init = false;
    ~Connection() {
        if (owns_init && shutdown && token && token() == generation) shutdown();
        if (module) FreeLibrary(module);
    }
    bool valid() const { return token && token() == generation; }
};

std::shared_ptr<Connection> connect() {
    static std::mutex mutex;
    static std::weak_ptr<Connection> shared;
    std::lock_guard lock(mutex);
    if (auto existing = shared.lock(); existing && existing->valid()) return existing;
    auto c = std::make_shared<Connection>();
    // Borrow an existing OpenVR module/connection when the host has one. Keep
    // a module reference, and never shut down a connection we did not create.
    if (!GetModuleHandleExW(0, L"openvr_api.dll", &c->module)) {
        HMODULE self = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              reinterpret_cast<LPCWSTR>(&dll_location_anchor), &self)) return {};
        wchar_t path[32768];
        const DWORD n = GetModuleFileNameW(self, path, 32768);
        if (!n || n == 32768) return {};
        const auto dll = std::filesystem::path(path).parent_path() / L"openvr_api.dll";
        c->module = LoadLibraryExW(dll.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    }
    if (!c->module) throw std::runtime_error("Missing openvr_api.dll beside the layer DLL.");
    const auto init = reinterpret_cast<Connection::Init>(GetProcAddress(c->module, "VR_InitInternal2"));
    const auto generic = reinterpret_cast<Connection::Generic>(GetProcAddress(c->module, "VR_GetGenericInterface"));
    c->shutdown = reinterpret_cast<Connection::Shutdown>(GetProcAddress(c->module, "VR_ShutdownInternal"));
    c->token = reinterpret_cast<Connection::Token>(GetProcAddress(c->module, "VR_GetInitToken"));
    if (!init || !generic || !c->shutdown || !c->token) throw std::runtime_error("Incompatible OpenVR client library.");
    vr::EVRInitError error = vr::VRInitError_None;
    c->system = static_cast<vr::IVRSystem*>(generic(vr::IVRSystem_Version, &error));
    if (!c->system) {
        // Only initialize on NotInitialized; never replace an existing
        // connection just because it exposes a different interface version.
        if (error != vr::VRInitError_Init_NotInitialized)
            throw std::runtime_error("OpenVR system interface unavailable; update SteamVR.");
        init(&error, vr::VRApplication_Background, nullptr);
        if (error != vr::VRInitError_None) throw std::runtime_error("SteamVR connection failed; start SteamVR with the headset connected.");
        c->generation = c->token();
        c->owns_init = true;
        c->system = static_cast<vr::IVRSystem*>(generic(vr::IVRSystem_Version, &error));
    }
    c->generation = c->token();
    c->camera = static_cast<vr::IVRTrackedCamera*>(generic(vr::IVRTrackedCamera_Version, &error));
    if (!c->system || !c->camera || error != vr::VRInitError_None)
        throw std::runtime_error("SteamVR tracked-camera interface unavailable.");
    shared = c;
    return c;
}
}

void write_pipeline_status(const char* message) {
    // Diagnostic only. Failure to write status must never stop a game.
    try {
        static std::mutex mutex;
        std::lock_guard lock(mutex);
        std::ofstream out(get_layer_data_dir() / "pipeline_status.txt", std::ios::trunc);
        out << message << '\n';
    } catch (...) {}
}

struct CameraSource::Impl {
    std::shared_ptr<Connection> connection;
    vr::TrackedCameraHandle_t stream = INVALID_TRACKED_CAMERA_HANDLE;
    Clock::time_point retry_at{};
    CameraFrame calibration;
    bool announced = false;
    void close() {
        if (stream && connection && connection->valid()) connection->camera->ReleaseVideoStreamingService(stream);
        stream = INVALID_TRACKED_CAMERA_HANDLE;
        connection.reset();
        announced = false;
    }
    void start() {
        connection = connect();
        if (!connection) throw std::runtime_error("Could not locate the OpenVR client.");
        auto* system = connection->system;
        auto* camera = connection->camera;
        bool has_camera = false;
        if (camera->HasCamera(vr::k_unTrackedDeviceIndex_Hmd, &has_camera) != vr::VRTrackedCameraError_None || !has_camera)
            throw std::runtime_error("Camera unavailable. Install PSVR2Toolkit experimental 2 or later and enable camera access in SteamVR.");
        vr::ETrackedPropertyError error{};
        const auto layout = system->GetInt32TrackedDeviceProperty(vr::k_unTrackedDeviceIndex_Hmd, vr::Prop_CameraFrameLayout_Int32, &error);
        if (error != vr::TrackedProp_Success || layout != (vr::EVRTrackedCameraFrameLayout_Stereo | vr::EVRTrackedCameraFrameLayout_HorizontalLayout))
            throw std::runtime_error("Unsupported camera layout: this beta requires a horizontal stereo stream.");
        vr::HmdMatrix34_t transforms[2]{};
        const auto bytes = system->GetArrayTrackedDeviceProperty(vr::k_unTrackedDeviceIndex_Hmd,
            vr::Prop_CameraToHeadTransforms_Matrix34_Array, vr::k_unHmdMatrix34PropertyTag,
            transforms, sizeof(transforms), &error);
        if (error != vr::TrackedProp_Success || bytes != sizeof(transforms))
            throw std::runtime_error("Toolkit camera-to-head calibration is unavailable.");
        for (unsigned eye=0; eye<2; ++eye) {
            vr::HmdMatrix44_t projection{};
            if (!camera_pose(transforms[eye].m, calibration.camera_to_head[eye]) ||
                camera->GetCameraProjection(vr::k_unTrackedDeviceIndex_Hmd, eye, frame_type, 0.05f, 100.f, &projection) != vr::VRTrackedCameraError_None ||
                !camera_fov(projection.m, calibration.fov[eye]))
                throw std::runtime_error("Invalid camera transform or projection; passthrough remains hidden.");
            const auto& p = calibration.camera_to_head[eye];
            const auto& f = calibration.fov[eye];
            PT_LOG_INFO("Toolkit eye {} position=({},{},{}) FOV=({},{},{},{})", eye,
                p.position[0], p.position[1], p.position[2], f.left, f.right, f.up, f.down);
        }
        const auto result = camera->AcquireVideoStreamingService(vr::k_unTrackedDeviceIndex_Hmd, &stream);
        if (result != vr::VRTrackedCameraError_None || !stream)
            throw std::runtime_error("SteamVR camera access denied or unavailable; check SteamVR camera settings.");
        write_pipeline_status("Camera connected. Waiting for a fresh timestamped frame.");
    }
};

CameraSource::CameraSource() : impl_(std::make_unique<Impl>()) {}
CameraSource::~CameraSource() { stop(); }
void CameraSource::stop() { impl_->close(); }

bool CameraSource::poll(ID3D11Device* device, CameraFrame& frame) {
    if (!device || Clock::now() < impl_->retry_at) return false;
    try {
        if (impl_->connection && !impl_->connection->valid()) impl_->close();
        if (!impl_->stream) impl_->start();
        auto* camera = impl_->connection->camera;
        vr::CameraVideoStreamFrameHeader_t header{};
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> texture;
        const auto result = camera->GetVideoStreamTextureD3D11(impl_->stream, frame_type, device,
            reinterpret_cast<void**>(texture.GetAddressOf()), &header, sizeof(header));
        if (result == vr::VRTrackedCameraError_NoFrameAvailable) return false;
        if (result != vr::VRTrackedCameraError_None || !texture)
            throw std::runtime_error("SteamVR camera texture unavailable for this graphics device.");
        if (header.eFrameType != frame_type || header.ulFrameExposureTime == 0 ||
            header.ulFrameExposureTime > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
            throw std::runtime_error("Camera returned invalid exposure metadata.");
        LARGE_INTEGER now{}, frequency{};
        QueryPerformanceCounter(&now); QueryPerformanceFrequency(&frequency);
        const auto exposure = static_cast<int64_t>(header.ulFrameExposureTime);
        const double age = static_cast<double>(now.QuadPart - exposure) / frequency.QuadPart;
        if (age < -0.005 || age > 0.25) return false; // Never freeze stale video over the game.
        vr::VRTextureBounds_t bounds{};
        uint32_t width=0, height=0;
        if (camera->GetVideoStreamTextureSize(vr::k_unTrackedDeviceIndex_Hmd, frame_type, &bounds, &width, &height) != vr::VRTrackedCameraError_None)
            throw std::runtime_error("Could not query camera texture bounds.");
        if (!width || !height || width > 16384 || height > 16384 ||
            !std::isfinite(bounds.uMin) || !std::isfinite(bounds.uMax) || !std::isfinite(bounds.vMin) || !std::isfinite(bounds.vMax) ||
            bounds.uMin < 0 || bounds.uMax > 1 || bounds.vMin < 0 || bounds.vMax > 1 ||
            bounds.uMin >= bounds.uMax || bounds.vMin >= bounds.vMax)
            throw std::runtime_error("Invalid camera texture dimensions or bounds.");
        frame = impl_->calibration;
        frame.texture = std::move(texture);
        frame.width = static_cast<uint32_t>(std::lround(width * (bounds.uMax - bounds.uMin) / 2.f));
        frame.height = static_cast<uint32_t>(std::lround(height * (bounds.vMax - bounds.vMin)));
        if (!frame.width || !frame.height) return false;
        frame.bounds[0]=bounds.uMin; frame.bounds[1]=bounds.vMin;
        frame.bounds[2]=bounds.uMax; frame.bounds[3]=bounds.vMax;
        frame.sequence=header.nFrameSequence; frame.exposure_qpc=exposure;
        if (!impl_->announced) {
            PT_LOG_INFO("Toolkit maximum-undistorted stream: {}x{} per eye, exposure age {:.1f} ms", frame.width, frame.height, age*1000);
            impl_->announced=true;
        }
        return true;
    } catch (const std::exception& e) {
        PT_LOG_WARN("Toolkit camera: {}", e.what());
        write_pipeline_status(e.what());
        impl_->close();
        impl_->retry_at = Clock::now() + std::chrono::seconds(2);
        return false;
    }
}
} // namespace psvr2pt
