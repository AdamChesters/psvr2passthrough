#include "layer_session.h"
#include "layer_dispatch.h"
#include "logging.h"

#include <cmath>


namespace psvr2pt {

// Common construction shared by both graphics modes. device_/ctx_ and the
// graphics-mode members must already be set by the delegating constructor.
void LayerSession::common_init_() {
    device_->GetImmediateContext(&ctx_);

    camera_ = std::make_unique<CameraSource>();
    write_pipeline_status("Hybrid beta ready. Use your binding to start the camera.");

    XrReferenceSpaceCreateInfo rsci{ XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
    rsci.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    rsci.poseInReferenceSpace = { { 0, 0, 0, 1 }, { 0, 0, 0 } };
    if (dispatch_->xrCreateReferenceSpace) {
        XrResult r = dispatch_->xrCreateReferenceSpace(session_, &rsci, &passthrough_space_);
        if (XR_FAILED(r)) {
            PT_LOG_ERROR("xrCreateReferenceSpace(VIEW) failed: {}", static_cast<int>(r));
            passthrough_space_ = XR_NULL_HANDLE;
        }
    }

    compositor_ = std::make_unique<Compositor>();

    ready_ = (passthrough_space_ != XR_NULL_HANDLE && dispatch_->convert_time);
    if (!dispatch_->convert_time) {
        PT_LOG_WARN("XR_KHR_win32_convert_performance_counter_time unavailable; camera timing cannot be established.");
        write_pipeline_status("SteamVR exposure-time conversion unavailable. Passthrough disabled for this session.");
    }
    PT_LOG_INFO("LayerSession constructed (ready={} mode={})", ready_,
                mode_ == GraphicsMode::D3D11On12 ? "D3D11On12" : "D3D11Native");
}

LayerSession::LayerSession(XrSession xr_session,
                           InstanceDispatch* dispatch,
                           ID3D11Device* device)
    : session_(xr_session)
    , dispatch_(dispatch)
    , mode_(GraphicsMode::D3D11Native)
    , device_(device)
{
    common_init_();
}

LayerSession::LayerSession(XrSession xr_session,
                           InstanceDispatch* dispatch,
                           ID3D11Device* interop_device,
                           ID3D11On12Device* on12,
                           ID3D12Device* game_device,
                           ID3D12CommandQueue* game_queue)
    : session_(xr_session)
    , dispatch_(dispatch)
    , mode_(GraphicsMode::D3D11On12)
    , device_(interop_device)
    , on12_(on12)
    , game_d3d12_device_(game_device)
    , game_d3d12_queue_(game_queue)
{
    common_init_();
}

LayerSession::~LayerSession() {
    if (ctx_) ctx_->Flush();
    compositor_.reset();
    cached_frame_.texture.Reset();
    if (camera_) camera_->stop();
    // Teardown order matters in D3D11On12 mode: the wrapped D3D11 textures
    // reference both the runtime's Camera swapchain images and the 11on12 device,
    // so they must be released BEFORE xrDestroySwapchain (which frees the D3D12
    // images) and before the 11on12 device. This dtor body runs before member
    // ComPtr destruction, so we must clear the wrapped vectors explicitly here.
    if (mode_ == GraphicsMode::D3D11On12 && ctx_)
        ctx_->Flush();  // drain any in-flight interop work before releasing

    for (auto& sc : swapchains_)
        sc.wrapped.clear();

    if (dispatch_) {
        for (auto& sc : swapchains_) {
            if (sc.handle != XR_NULL_HANDLE && dispatch_->xrDestroySwapchain)
                dispatch_->xrDestroySwapchain(sc.handle);
        }
        if (passthrough_space_ != XR_NULL_HANDLE && dispatch_->xrDestroySpace)
            dispatch_->xrDestroySpace(passthrough_space_);
    }
    // Remaining ComPtr members release in reverse-declaration order: compositor_
    // (its D3D11 objects on the interop device) is a unique_ptr destroyed after
    // this body; on12_/device_/ctx_ release after that. The app-owned
    // game_d3d12_* ComPtrs only decrement — we never explicitly Release them.
}

bool LayerSession::negotiate_swapchain_format_() {
    // Pick an R8G8B8A8-family format the runtime offers, so the compositor's
    // R8G8B8A8_UNORM output stays CopyResource-compatible (CopyResource requires
    // the same typeless family; a BGRA/other-family target would fail the copy).
    // Prefer the sRGB variant (matches the historical D3D11 path), then UNORM.
    if (!dispatch_->xrEnumerateSwapchainFormats) {
        PT_LOG_ERROR("xrEnumerateSwapchainFormats unavailable; cannot negotiate camera format");
        return false;
    }
    uint32_t count = 0;
    if (XR_FAILED(dispatch_->xrEnumerateSwapchainFormats(session_, 0, &count, nullptr)) ||
        count == 0) {
        PT_LOG_ERROR("xrEnumerateSwapchainFormats returned no formats");
        return false;
    }
    std::vector<int64_t> formats(count);
    if (XR_FAILED(dispatch_->xrEnumerateSwapchainFormats(
            session_, count, &count, formats.data()))) {
        PT_LOG_ERROR("xrEnumerateSwapchainFormats(fill) failed");
        return false;
    }

    auto offered = [&](int64_t f) {
        for (int64_t v : formats) if (v == f) return true;
        return false;
    };
    if (offered(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)) {
        swapchain_format_ = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    } else if (offered(DXGI_FORMAT_R8G8B8A8_UNORM)) {
        swapchain_format_ = DXGI_FORMAT_R8G8B8A8_UNORM;
    } else {
        PT_LOG_ERROR("No R8G8B8A8-family swapchain format offered by runtime; "
                     "Passthrough inert (CopyResource needs matching family)");
        return false;
    }
    PT_LOG_INFO("Camera swapchain format negotiated: {} ({} formats offered)",
                static_cast<int>(swapchain_format_), count);
    return true;
}

bool LayerSession::ensure_swapchain_(uint32_t width, uint32_t height) {
    if (targets_ready_ && swapchains_[0].handle != XR_NULL_HANDLE &&
        swapchains_[0].width == width && swapchains_[0].height == height)
        return true;
    if (!dispatch_->xrCreateSwapchain || !dispatch_->xrEnumerateSwapchainImages) return false;

    targets_ready_ = false;
    const bool on12 = (mode_ == GraphicsMode::D3D11On12);

    if (!negotiate_swapchain_format_()) return false;

    for (int eye = 0; eye < 2; ++eye) {
        // Release D3D11 wrappers before the runtime destroys their D3D12 resources.
        if (on12 && ctx_) ctx_->Flush();
        swapchains_[eye].wrapped.clear();
        if (swapchains_[eye].handle != XR_NULL_HANDLE && dispatch_->xrDestroySwapchain) {
            dispatch_->xrDestroySwapchain(swapchains_[eye].handle);
            swapchains_[eye].handle = XR_NULL_HANDLE;
        }
        swapchains_[eye].acquired = swapchains_[eye].waited = false;

        XrSwapchainCreateInfo sci{ XR_TYPE_SWAPCHAIN_CREATE_INFO };
        sci.usageFlags  = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT
                        | XR_SWAPCHAIN_USAGE_SAMPLED_BIT
                        | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
        sci.format      = static_cast<int64_t>(swapchain_format_);
        sci.sampleCount = 1;
        sci.width       = width;
        sci.height      = height;
        sci.faceCount   = 1;
        sci.arraySize   = 1;
        sci.mipCount    = 1;
        XrResult r = dispatch_->xrCreateSwapchain(session_, &sci, &swapchains_[eye].handle);
        if (XR_FAILED(r)) {
            PT_LOG_ERROR("xrCreateSwapchain failed for eye {}: {}", eye, static_cast<int>(r));
            return false;
        }

        uint32_t count = 0;
        if (XR_FAILED(dispatch_->xrEnumerateSwapchainImages(swapchains_[eye].handle, 0, &count, nullptr)) || !count) return false;

        if (on12) {
            swapchains_[eye].images12.assign(count, { XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR });
            if (XR_FAILED(dispatch_->xrEnumerateSwapchainImages(
                swapchains_[eye].handle, count, &count,
                reinterpret_cast<XrSwapchainImageBaseHeader*>(swapchains_[eye].images12.data())))) return false;

            // Wrap each Camera swapchain image as a D3D11 texture once and cache it.
            // OpenXR delivers and accepts color images in RENDER_TARGET state.
            // D3D11On12 manages the transition for CopyResource between acquire
            // and release, then restores the state required by the runtime.
            swapchains_[eye].wrapped.assign(count, nullptr);
            D3D11_RESOURCE_FLAGS rf{};
            rf.BindFlags = D3D11_BIND_RENDER_TARGET;
            for (uint32_t i = 0; i < count; ++i) {
                HRESULT hr = on12_->CreateWrappedResource(
                    swapchains_[eye].images12[i].texture,
                    &rf,
                    D3D12_RESOURCE_STATE_RENDER_TARGET,
                    D3D12_RESOURCE_STATE_RENDER_TARGET,
                    IID_PPV_ARGS(&swapchains_[eye].wrapped[i]));
                if (FAILED(hr)) {
                    PT_LOG_ERROR("CreateWrappedResource failed eye={} img={} hr=0x{:08X}",
                                 eye, i, static_cast<uint32_t>(hr));
                    return false;
                }
            }
        } else {
            swapchains_[eye].images.assign(count, { XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR });
            if (XR_FAILED(dispatch_->xrEnumerateSwapchainImages(
                swapchains_[eye].handle, count, &count,
                reinterpret_cast<XrSwapchainImageBaseHeader*>(swapchains_[eye].images.data())))) return false;
        }
        swapchains_[eye].width  = width;
        swapchains_[eye].height = height;
    }

    if (!compositor_->initialise(device_.Get(), width, height, swapchain_format_)) return false;
    rendered_ = false;
    targets_ready_ = true;

    return true;
}


const XrCompositionLayerBaseHeader*
LayerSession::compose_layer(const XrFrameEndInfo* original) {
    if (!ready_ || !original || !original->layerCount || !original->layers) return nullptr;
    const bool pressed = poller_.poll();
    if (force_on_) passthrough_visible_ = true;
    else if (toggle_mode_) {
        if (pressed && !prev_button_state_) passthrough_visible_ = !passthrough_visible_;
    } else passthrough_visible_ = pressed;
    prev_button_state_ = pressed;
    if (!passthrough_visible_ || config_.global_alpha <= 0) return nullptr;

    const XrCompositionLayerProjection* game = nullptr;
    for (uint32_t i=0; i<original->layerCount; ++i) {
        if (original->layers[i] && original->layers[i]->type == XR_TYPE_COMPOSITION_LAYER_PROJECTION) {
            game = reinterpret_cast<const XrCompositionLayerProjection*>(original->layers[i]); break;
        }
    }
    if (!game || (game->viewCount != 2 && game->viewCount != 4)) return nullptr;
    CameraFrame frame;
    if (!camera_->poll(device_.Get(),frame)) return nullptr;

    LARGE_INTEGER exposure{}; exposure.QuadPart=frame.exposure_qpc;
    XrTime capture_time=0;
    const auto converted = dispatch_->convert_time(dispatch_->instance,&exposure,&capture_time);
    XrSpaceLocation head{XR_TYPE_SPACE_LOCATION};
    constexpr XrSpaceLocationFlags valid = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT;
    // Locate the VIEW space at exposure in the actual game's layer space.
    // This avoids equating SteamVR's origin with OpenXR LOCAL/STAGE and handles
    // game reference-space changes/recentering without a guessed clock offset.
    if (XR_FAILED(converted) || capture_time > original->displayTime ||
        XR_FAILED(dispatch_->xrLocateSpace(passthrough_space_,game->space,capture_time,&head)) ||
        (head.locationFlags & valid) != valid) {
        if (!timing_warning_) {
            PT_LOG_WARN("No valid OpenXR head pose for the camera exposure; hiding passthrough instead of using a guessed pose.");
            write_pipeline_status("Camera received, but its exposure pose is unavailable. Passthrough hidden.");
            timing_warning_=true; timing_announced_=false;
        }
        return nullptr;
    }
    timing_warning_=false;
    if (!timing_announced_) {
        write_pipeline_status("Toolkit camera active. Automatic calibration and exposure-time alignment; no added sharpening.");
        PT_LOG_INFO("Camera exposure converted to OpenXR time; age to display {:.1f} ms",
            static_cast<double>(original->displayTime-capture_time)/1e6);
        timing_announced_=true;
    }
    if (!ensure_swapchain_(frame.width,frame.height)) return nullptr;
    if (!rendered_ || rendered_sequence_ != frame.sequence || rendered_exposure_ != frame.exposure_qpc) {
        if (!compositor_->render(frame,config_)) return nullptr;
        rendered_sequence_=frame.sequence; rendered_exposure_=frame.exposure_qpc; rendered_=true;
    }
    cached_frame_=std::move(frame);

    // Non-blocking swapchain waits. An acquired image that times out remains
    // acquired and is waited again on a later frame; releasing it before a
    // successful wait would violate the OpenXR call order.
    for (auto& sc : swapchains_) {
        if (!sc.acquired) {
            XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
            if (XR_FAILED(dispatch_->xrAcquireSwapchainImage(sc.handle,&ai,&sc.index))) return nullptr;
            sc.acquired=true;
        }
        if (!sc.waited) {
            XrSwapchainImageWaitInfo wi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO}; wi.timeout=0;
            if (dispatch_->xrWaitSwapchainImage(sc.handle,&wi) != XR_SUCCESS) return nullptr;
            sc.waited=true;
        }
    }
    std::array<ID3D11Resource*,2> destinations{};
    for (unsigned eye=0; eye<2; ++eye) {
        auto& sc=swapchains_[eye];
        destinations[eye] = mode_ == GraphicsMode::D3D11On12 ?
            static_cast<ID3D11Resource*>(sc.wrapped[sc.index].Get()) : sc.images[sc.index].texture;
    }
    if (on12_) on12_->AcquireWrappedResources(destinations.data(),2);
    for (unsigned eye=0; eye<2; ++eye) ctx_->CopyResource(destinations[eye],compositor_->eye(eye).texture.Get());
    if (on12_) { on12_->ReleaseWrappedResources(destinations.data(),2); ctx_->Flush(); }
    bool released=true;
    for (auto& sc : swapchains_) {
        XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        if (XR_FAILED(dispatch_->xrReleaseSwapchainImage(sc.handle,&ri))) released=false;
        else sc.acquired=sc.waited=false;
    }
    if (!released) return nullptr;

    const CameraPose head_pose{{head.pose.orientation.x,head.pose.orientation.y,head.pose.orientation.z,head.pose.orientation.w},
                               {head.pose.position.x,head.pose.position.y,head.pose.position.z}};
    for (unsigned eye=0; eye<2; ++eye) {
        const auto pose=compose_pose(head_pose,cached_frame_.camera_to_head[eye]);
        const auto& f=cached_frame_.fov[eye];
        auto& view=projection_views_[eye];
        view={XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
        view.pose={{pose.orientation[0],pose.orientation[1],pose.orientation[2],pose.orientation[3]},
                   {pose.position[0],pose.position[1],pose.position[2]}};
        view.fov={f.left,f.right,f.up,f.down};
        view.subImage.swapchain=swapchains_[eye].handle;
        view.subImage.imageRect.extent={static_cast<int32_t>(cached_frame_.width),static_cast<int32_t>(cached_frame_.height)};
    }
    composition_layer_={XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    composition_layer_.layerFlags=XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
    composition_layer_.space=game->space;
    composition_layer_.viewCount=2; composition_layer_.views=projection_views_.data();
    return reinterpret_cast<const XrCompositionLayerBaseHeader*>(&composition_layer_);
}
} // namespace psvr2pt
