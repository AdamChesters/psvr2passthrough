#include "config_window.h"
#include "version.h"

#include <imgui.h>
#include <windows.h>
#include <shellapi.h>
#include <filesystem>
#include <fstream>
#include <cstring>
#include <cmath>
#include <sstream>

namespace psvr2pt {

namespace {

static void open_url(const char* url) {
    int wlen = MultiByteToWideChar(CP_UTF8, 0, url, -1, nullptr, 0);
    std::wstring wurl(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, url, -1, wurl.data(), wlen);
    ShellExecuteW(nullptr, L"open", wurl.c_str(), nullptr, nullptr, SW_SHOW);
}

// Hint text rendered in the disabled colour and wrapped to the available column width.
static void TextHint(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

bool configs_equal(const Config& a, const Config& b) {
    return config_to_json(a) == config_to_json(b);
}

}  // namespace

ConfigWindow::ConfigWindow()  = default;
ConfigWindow::~ConfigWindow() = default;

void ConfigWindow::initialise(ID3D11Device* device, ID3D11DeviceContext* ctx) {
    device_   = device;
    ctx_      = ctx;
    on_disk_  = load_config();
    working_  = on_disk_;
    dirty_    = false;
    capturer_.open_devices();

}

void ConfigWindow::shutdown() {
    capturer_.close_devices();

}

void ConfigWindow::draw() {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration
                           | ImGuiWindowFlags_NoMove
                           | ImGuiWindowFlags_NoResize
                           | ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::Begin("##root", nullptr, flags);

    ImGui::TextUnformatted("PSVR2 Passthrough Layer - Configuration");
    ImGui::Separator();
    draw_update_banner();
    TextHint("Changes apply when your sim next starts. Save then restart your sim.");
    ImGui::Spacing();

    if (ImGui::BeginTable("layout", 2, ImGuiTableFlags_BordersInnerV
                                     | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("controls", ImGuiTableColumnFlags_WidthStretch, 0.60f);
        ImGui::TableSetupColumn("status",   ImGuiTableColumnFlags_WidthStretch, 0.40f);
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        if (ImGui::BeginChild("##left_scroll", ImVec2(0, 0), false))
            draw_main_panel();
        ImGui::EndChild();

        ImGui::TableSetColumnIndex(1);
        if (ImGui::BeginChild("##right_scroll", ImVec2(0, 0), false))
            draw_about_panel();
        ImGui::EndChild();

        ImGui::EndTable();
    }

    ImGui::End();

    dirty_ = !configs_equal(working_, on_disk_);
    if (saved_recently_ && (ImGui::GetTime() - saved_at_) > 2.0)
        saved_recently_ = false;

    // Run capturer scan every frame while capturing.
    if (capturing_) {
        if (capturer_.scan()) {
            working_.passthrough_binding = capturer_.captured();
            capturing_ = false;
        }
    }
}

void ConfigWindow::draw_update_banner() {
    TextHint("Hybrid beta: requires PSVR2Toolkit experimental 2 or later and SteamVR camera access.");
}

void ConfigWindow::draw_main_panel() {

    // -----------------------------------------------------------------------
    ImGui::SeparatorText("Save / restore");

    const ImVec4 dirty_col(1.0f, 0.85f, 0.4f, 1.0f);
    const ImVec4 saved_col(0.4f, 1.0f, 0.5f, 1.0f);

    if (ImGui::Button("Save")) {
        if (save_config(working_)) {
            on_disk_        = working_;
            dirty_          = false;
            saved_recently_ = true;
            saved_at_       = ImGui::GetTime();
            save_failed_ = false;
        } else { save_failed_ = true; }
    }
    ImGui::SameLine();
    if (ImGui::Button("Reload from disk")) {
        on_disk_ = load_config();
        working_ = on_disk_;
        dirty_   = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset defaults")) {
        working_ = Config{};
    }
    ImGui::SameLine();
    if (save_failed_) ImGui::TextWrapped("Could not save settings. Check that the configuration folder is writable.");
    if (dirty_)               ImGui::TextColored(dirty_col, "Unsaved changes");
    else if (saved_recently_) ImGui::TextColored(saved_col, "Saved.");
    else                      TextHint("No changes.");

    TextHint(("Config: " + config_file_path().string()).c_str());
    ImGui::Spacing();

    // -----------------------------------------------------------------------
    ImGui::SeparatorText("Master switch");
    ImGui::Checkbox("Layer enabled", &working_.enabled);
    TextHint("If disabled, the layer loads but stays inert - no camera,");
    TextHint("no compositing. Useful for A/B comparisons.");
    ImGui::Spacing();
    ImGui::Checkbox("Always visible (ignore binding)", &working_.force_passthrough_on);
    TextHint("Ignores button binding. Use to verify passthrough is working.");
    TextHint("Disable before normal use - passthrough will overlay all games.");
    ImGui::Spacing();

    // -----------------------------------------------------------------------
    ImGui::SeparatorText("Passthrough transparency");
    ImGui::SliderFloat("Opacity", &working_.global_alpha, 0.0f, 1.0f, "%.2f");
    TextHint("1.0 = fully opaque passthrough.  0.5 = semi-transparent.");
    TextHint("0.0 = hidden.");
    ImGui::Spacing();

    // -----------------------------------------------------------------------
    ImGui::SeparatorText("Passthrough button binding");

    // Current binding display.
    const std::string bname = working_.passthrough_binding.display_name();
    ImGui::Text("Current: %s", bname.c_str());
    ImGui::Spacing();

    draw_binding_capture_button();

    ImGui::Spacing();
    if (!working_.passthrough_binding.is_none()) {
        if (ImGui::Button("Clear binding")) {
            working_.passthrough_binding = PassthroughBinding{};
            if (capturing_) { capturer_.stop(); capturing_ = false; }
        }
        TextHint("Without a binding, passthrough stays hidden unless Always visible is enabled.");
        ImGui::Spacing();

        ImGui::SeparatorText("Activation mode");
        int mode = working_.toggle_mode ? 1 : 0;
        ImGui::RadioButton("Hold to show", &mode, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Toggle on/off", &mode, 1);
        working_.toggle_mode = (mode == 1);
        if (working_.toggle_mode)
            TextHint("Press once to show passthrough, press again to hide.");
        else
            TextHint("Passthrough visible only while button is held.");
    } else {
        TextHint("No binding set. Passthrough stays hidden unless Always visible is enabled.");
        TextHint("Set a binding to control it with a button.");
    }
    ImGui::Spacing();

    ImGui::SeparatorText("Automatic camera processing");
    TextHint("Camera alignment, lens correction and image processing come from the Toolkit camera provider.");
    TextHint("This beta adds no brightness, contrast or sharpening adjustments.");
    TextHint("Changes take effect when you restart your game.");
}

void ConfigWindow::draw_binding_capture_button() {
    if (capturing_) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.2f, 0.2f, 1.0f));
        if (ImGui::Button("Cancel")) {
            capturer_.stop();
            capturing_ = false;
        }
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.4f, 1.f, 0.4f, 1.f), "Press your button now.");
    } else {
        if (ImGui::Button("Set binding...")) {
            capturer_.start();
            capturing_ = true;
        }
        TextHint("Keyboard keys, XInput gamepad buttons,");
        TextHint("and DirectInput HOTAS / joystick buttons are all supported.");
    }
}

void ConfigWindow::draw_about_panel() {
    ImGui::SeparatorText("About");
    ImGui::Text("Version: %s", kCurrentVersion);
    TextHint("Experimental hybrid beta - feedback welcome:");
    ImGui::TextLinkOpenURL("GitHub Discussions", kDiscussionsUrl);
    ImGui::SameLine();
    ImGui::TextLinkOpenURL("/r/psvr2passthrough", kSubredditUrl);
    ImGui::Spacing();
    ImGui::TextWrapped(
        "This configurator writes settings to a JSON file the layer reads "
        "once at start-up. Changes take effect when you next launch your sim.");
    ImGui::Spacing();
    ImGui::TextWrapped(
        "Passthrough button binding supports keyboard keys, Xbox/gamepad "
        "buttons (XInput), and any HOTAS or joystick (DirectInput) - no "
        "intermediate mapping required.");
    ImGui::Spacing();

    ImGui::SeparatorText("Tips");
    ImGui::TextWrapped("Ctrl+Click any slider to type an exact value.");
    ImGui::Spacing();

    ImGui::SeparatorText("Last runtime report");
    if (ImGui::GetTime() - status_read_at_ > 1.0 || intrinsics_text_.empty()) {
        status_read_at_ = ImGui::GetTime();
        std::ifstream file(config_file_path().parent_path() / "pipeline_status.txt");
        std::ostringstream report;
        if (file) report << file.rdbuf();
        intrinsics_text_ = report.str();
        if (intrinsics_text_.empty()) intrinsics_text_ = "Launch a game and activate passthrough to check the camera.";
    }
    ImGui::TextWrapped("%s", intrinsics_text_.c_str());
    TextHint("Report from the layer; it may refer to a previous game session.");

}


}  // namespace psvr2pt
