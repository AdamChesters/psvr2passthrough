#include "config_window.h"
#include "version.h"
#include <imgui.h>
#include <windows.h>
#include <shellapi.h>

namespace psvr2pt {
void ConfigWindow::draw_support_panel(){
    adamch_support::AppIdentity identity;
    identity.appName="PSVR2 Passthrough";identity.appVersion=kCurrentVersion;identity.appId="psvr2passthrough";
    identity.appLogo=[] {
        const auto origin=ImGui::GetCursorScreenPos();
        const float middle=origin.x+ImGui::GetContentRegionAvail().x*.5f;
        const auto color=ImGui::GetColorU32(ImGuiCol_Text);auto* draw=ImGui::GetWindowDrawList();
        draw->AddRect(ImVec2(middle-39,origin.y+8),ImVec2(middle+39,origin.y+43),color,12.f,ImDrawFlags(0),3.f);
        draw->AddBezierCubic(ImVec2(middle-35,origin.y+12),ImVec2(middle-28,origin.y-10),ImVec2(middle+28,origin.y-10),ImVec2(middle+35,origin.y+12),color,3);
        draw->AddCircleFilled(ImVec2(middle-22,origin.y+25),4,color);draw->AddCircleFilled(ImVec2(middle+22,origin.y+25),4,color);
        ImGui::Dummy(ImVec2(0,53));
    };
    const auto state=update_checker_.state();
    identity.updateStatus=state==UpdateChecker::State::Pending?"Checking for updates...":state==UpdateChecker::State::UpToDate?"Latest version installed.":state==UpdateChecker::State::Available?"Update available: "+update_checker_.latest_tag():"Update check failed. You can open the releases page.";
    identity.checkForUpdates=[this]{
        if(update_checker_.state()==UpdateChecker::State::Available){
            const int size=MultiByteToWideChar(CP_UTF8,0,kReleasesUrl,-1,nullptr,0);std::wstring url(size,L'\0');
            MultiByteToWideChar(CP_UTF8,0,kReleasesUrl,-1,url.data(),size);ShellExecuteW(nullptr,L"open",url.c_str(),nullptr,nullptr,SW_SHOW);
        }else if(update_checker_.state()!=UpdateChecker::State::Pending)update_checker_.start(kCurrentVersion);
    };
    support_panel_.draw(identity);
}
} // namespace psvr2pt
