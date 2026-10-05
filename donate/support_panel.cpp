#include "support_panel.h"
#include "feedback_payload.h"

#include <imgui.h>
#include <windows.h>
#include <shellapi.h>
#include <algorithm>
#include <cstring>

namespace adamch_support {
namespace {
void visit(const char* url) {
    const int size = MultiByteToWideChar(CP_UTF8, 0, url, -1, nullptr, 0);
    if (size <= 1) return;
    std::wstring wide(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, url, -1, wide.data(), size);
    ShellExecuteW(nullptr, L"open", wide.c_str(), nullptr, nullptr, SW_SHOW);
}
void hint(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}
std::string describe(std::string text,const std::string& appName){
    const std::string marker="{appName}";
    for(auto position=text.find(marker);position!=std::string::npos;position=text.find(marker,position+appName.size()))text.replace(position,marker.size(),appName);
    return text;
}
ImVec2 size(float width,float height,float scale){return ImVec2(width>0?width*scale:width,height*scale);}
std::string support_title(){return std::string(content::title)+"##support";}
std::string feedback_title(){return std::string(content::feedbackTitle)+"##feedback";}
void popup_size(float width, float height) {
    auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f,
                                  viewport->WorkPos.y + viewport->WorkSize.y * 0.5f),
                           ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(std::min(width, viewport->WorkSize.x - 24),
                                  std::min(height, viewport->WorkSize.y - 24)), ImGuiCond_Appearing);
}
size_t character_count(const std::string& text) {
    size_t count = 0;
    for (unsigned char c : text) if ((c & 0xc0) != 0x80) ++count;
    return count;
}
}

void SupportPanel::open(){ImGui::OpenPopup(support_title().c_str());}
void SupportPanel::openFeedback(){open_feedback_=true;}
void SupportPanel::draw(const AppIdentity& identity){
    drawSupport(identity);
    if(open_feedback_){ImGui::OpenPopup(feedback_title().c_str());open_feedback_=false;}
    drawFeedback(identity);
    if(update_requested_){update_requested_=false;if(identity.checkForUpdates)identity.checkForUpdates();}
}

void SupportPanel::drawSupport(const AppIdentity& identity) {
    popup_size(690*identity.scale,620*identity.scale);
    bool open = true;
    if (!ImGui::BeginPopupModal(support_title().c_str(), &open, ImGuiWindowFlags_NoSavedSettings)) return;
    ImGui::BeginChild("support-content", size(0,-46,identity.scale));
    if(identity.appLogo)identity.appLogo();
    const auto titleWidth=ImGui::CalcTextSize(identity.appName.c_str()).x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX()+std::max(0.f,(ImGui::GetContentRegionAvail().x-titleWidth)*.5f));
    ImGui::TextUnformatted(identity.appName.c_str());
    ImGui::Text("Version %s",identity.appVersion.c_str());
    if(!identity.updateStatus.empty())hint(identity.updateStatus.c_str());
    ImGui::Spacing();
    if (ImGui::Button(content::discordLabel, size(0,36,identity.scale))) visit(content::discord);
    ImGui::SameLine();
    if(ImGui::Button(content::updateLabel,size(0,36,identity.scale))){update_requested_=true;ImGui::CloseCurrentPopup();}
    ImGui::SameLine();
    if (ImGui::Button(content::feedbackLabel, size(0,36,identity.scale))) {
        open_feedback_ = true;
        ImGui::CloseCurrentPopup();
    }
    ImGui::Spacing();
    ImGui::TextWrapped("%s",describe(content::intro,identity.appName).c_str());
    ImGui::Spacing();
    ImGui::TextWrapped("%s",content::projects);
    if (ImGui::Button("GitHub")) visit(content::github);
    ImGui::SameLine();
    if (ImGui::Button("AdamCh.com")) visit(content::website);
    ImGui::Spacing();
    ImGui::SeparatorText(content::donationTitle);
    ImGui::TextWrapped("%s",content::donationText);
    ImGui::Spacing();
    if(ImGui::BeginTable("donation-options",int(std::size(content::platforms)),ImGuiTableFlags_SizingStretchSame)){
        for(const auto& platform:content::platforms){
            ImGui::TableNextColumn();
            if(ImGui::Button(platform.label,size(-1,44,identity.scale)))visit(platform.url);
            hint(platform.notes);
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
    if (ImGui::Button("Close", size(0,32,identity.scale)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void SupportPanel::drawFeedback(const AppIdentity& identity) {
    popup_size(640*identity.scale,540*identity.scale);
    bool open = true;
    if (!ImGui::BeginPopupModal(feedback_title().c_str(), &open, ImGuiWindowFlags_NoSavedSettings)) return;
    const auto state = sender_.state();
    const bool sending = state == FeedbackSender::State::Sending;
    if (state == FeedbackSender::State::Sent && !feedback_sent_seen_) {
        message_[0] = '\0';
        feedback_sent_seen_ = true;
    }
    ImGui::BeginChild("feedback-content", size(0,-46,identity.scale));
    hint(describe(content::feedbackIntro,identity.appName).c_str());
    ImGui::BeginDisabled(sending);
    ImGui::TextUnformatted(content::nameLabel);
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##feedback-name", name_.data(),name_.size());
    ImGui::TextUnformatted(content::emailLabel);
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##feedback-email", email_.data(),email_.size());
    hint("Your name and email are kept here while the app is open.");
    ImGui::TextUnformatted(content::messageLabel);
    if (ImGui::InputTextMultiline("##feedback-message", message_.data(),message_.size(), size(-1,135,identity.scale))) {
        feedback_sent_seen_ = true;
        feedback_error_.clear();
    }
    ImGui::EndDisabled();
    hint(content::feedbackPrivacy);
    if (!feedback_error_.empty()) ImGui::TextWrapped("%s", feedback_error_.c_str());
    if (state == FeedbackSender::State::Failed) ImGui::TextWrapped("%s",content::feedbackFailure);
    else if (sending) ImGui::TextUnformatted("Sending...");
    else if (state == FeedbackSender::State::Sent && message_[0] == '\0') ImGui::TextUnformatted(content::feedbackSuccess);
    if (content::feedbackEndpoint[0] == '\0') hint("Feedback delivery is not configured yet.");
    ImGui::EndChild();
    if (ImGui::Button("Close", size(0,32,identity.scale)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    ImGui::SameLine();
    ImGui::BeginDisabled(sending || content::feedbackEndpoint[0] == '\0');
    if (ImGui::Button(sending ? "Sending..." : content::feedbackSubmit, size(0,32,identity.scale))) {
        const auto name = trim_feedback(name_.data());
        const auto email = trim_feedback(email_.data());
        const auto message = trim_feedback(message_.data());
        feedback_error_.clear();
        if (name.empty() || character_count(name) > content::nameMax) feedback_error_ = "Enter a name, up to "+std::to_string(content::nameMax)+" characters.";
        else if (character_count(email) > content::emailMax || !valid_feedback_email(email)) feedback_error_ = "Enter a valid email address, up to "+std::to_string(content::emailMax)+" characters.";
        else if (message.empty() || character_count(message) > content::messageMax) feedback_error_ = "Enter a message, up to "+std::to_string(content::messageMax)+" characters.";
        else {
            feedback_sent_seen_ = false;
            sender_.send(feedback_payload(name, email, message, identity.appVersion,identity.appId));
        }
    }
    ImGui::EndDisabled();
    ImGui::EndPopup();
}
} // namespace adamch_support
