#include "support_panel.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <iostream>
#include <stdexcept>

static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(){
    ImGui::CreateContext();
    try{
        auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize=ImVec2(760,860);io.DeltaTime=1.f/60;
        io.Fonts->AddFontDefault();unsigned char* pixels;int width,height;io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
        adamch_support::SupportPanel panel;int logoCount=0,updateCount=0;
        adamch_support::AppIdentity identity;
        identity.appName="Native support test";identity.appVersion="1.0.0";identity.appId="test-app";
        identity.appLogo=[&]{++logoCount;ImGui::Dummy(ImVec2(0,40));};identity.checkForUpdates=[&]{++updateCount;};
        auto frame=[&](int open=0){
            ImGui::NewFrame();ImGui::Begin("Support fixture");
            if(open==1)panel.open();
            if(open==2)panel.openFeedback();
            panel.draw(identity);ImGui::End();ImGui::Render();
        };
        frame(1);frame();
        const auto supportTitle=std::string(adamch_support::content::title)+"##support";
        auto* support=ImGui::FindWindowByName(supportTitle.c_str());
        check(support&&support->Active&&logoCount>0,"support opens with the app logo callback");
        check(!support->DC.ChildWindows.empty(),"support content scrolls in its child region");
        auto* content=support->DC.ChildWindows[0];
        ImGui::ActivateItemByID(content->GetID(adamch_support::content::updateLabel));frame();frame();
        check(updateCount==1&&!support->Active,"update action closes support and invokes the app callback");
        frame(2);frame();
        const auto feedbackTitle=std::string(adamch_support::content::feedbackTitle)+"##feedback";
        auto* feedback=ImGui::FindWindowByName(feedbackTitle.c_str());
        check(feedback&&feedback->Active,"feedback opens");
        ImGui::ActivateItemByID(feedback->GetID(adamch_support::content::feedbackSubmit));frame();frame();
        check(panel.feedbackError()=="Enter a name, up to "+std::to_string(adamch_support::content::nameMax)+" characters.","empty feedback is rejected without network access");
        ImGui::ActivateItemByID(feedback->GetID("Close"));frame();frame();
        check(!feedback->Active,"feedback closes");
        ImGui::DestroyContext();std::cout<<"PASS: bundled support, logo callback, updater callback, feedback validation and close\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';ImGui::DestroyContext();return 1;}
}
