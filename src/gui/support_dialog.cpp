#include "config_window.h"
#include "version.h"
#include "support_links.h"
#include "feedback_payload.h"

#include <imgui.h>
#include <windows.h>
#include <shellapi.h>
#include <algorithm>
#include <cstring>

namespace psvr2pt {
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
void brand() {
    // A native headset mark, drawn from geometry so no asset loader is needed.
    const auto origin = ImGui::GetCursorScreenPos();
    const float middle = origin.x + ImGui::GetContentRegionAvail().x * 0.5f;
    const auto color = ImGui::GetColorU32(ImGuiCol_Text);
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRect(ImVec2(middle - 39, origin.y + 8), ImVec2(middle + 39, origin.y + 43), color, 12.0f, ImDrawFlags(0), 3.0f);
    draw->AddBezierCubic(ImVec2(middle - 35, origin.y + 12), ImVec2(middle - 28, origin.y - 10),
        ImVec2(middle + 28, origin.y - 10), ImVec2(middle + 35, origin.y + 12), color, 3);
    draw->AddCircleFilled(ImVec2(middle - 22, origin.y + 25), 4, color);
    draw->AddCircleFilled(ImVec2(middle + 22, origin.y + 25), 4, color);
    ImGui::Dummy(ImVec2(0, 53));
    const char* title = "PSVR2 Passthrough";
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(title).x) * 0.5f);
    ImGui::TextUnformatted(title);
}
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

void ConfigWindow::draw_support_dialog() {
    popup_size(690, 620);
    bool open = true;
    if (!ImGui::BeginPopupModal("Feedback / Donate##support", &open, ImGuiWindowFlags_NoSavedSettings)) return;
    ImGui::BeginChild("support-content", ImVec2(0, -46));
    brand();
    ImGui::Text("Version %s", kCurrentVersion);
    const auto update = update_checker_.state();
    if (update == UpdateChecker::State::Pending) hint("Checking for updates...");
    else if (update == UpdateChecker::State::UpToDate) hint("Latest version installed.");
    else if (update == UpdateChecker::State::Available) ImGui::Text("Update available: %s", update_checker_.latest_tag().c_str());
    else hint("Update check failed. You can open the releases page.");
    ImGui::Spacing();
    if (ImGui::Button("Join the Discord", ImVec2(0, 36))) visit(kDiscordUrl);
    ImGui::SameLine();
    ImGui::BeginDisabled(update == UpdateChecker::State::Pending);
    if (ImGui::Button(update == UpdateChecker::State::Available ? "Open latest release" : "Check for updates", ImVec2(0, 36))) {
        if (update == UpdateChecker::State::Available) visit(kReleasesUrl);
        else update_checker_.start(kCurrentVersion);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Feedback", ImVec2(0, 36))) {
        open_feedback_ = true;
        ImGui::CloseCurrentPopup();
    }
    ImGui::Spacing();
    ImGui::TextWrapped("Thanks for using PSVR2 Passthrough. Feedback is always welcome. The quickest way to get my attention is the Feedback button above. You can also open an issue on GitHub or join the Discord.");
    ImGui::Spacing();
    ImGui::TextWrapped("In the meantime, I have a variety of other fun software projects. Check out:");
    if (ImGui::Button("GitHub")) visit("https://github.com/AdamChesters");
    ImGui::SameLine();
    if (ImGui::Button("AdamCh.com")) visit("https://adamch.com");
    ImGui::Spacing();
    ImGui::SeparatorText("Donate");
    ImGui::TextWrapped("I love making things. Anything I've ever built has been to have fun, share fun, and make life a bit easier. If you got value from one of these things, and you'd like to chuck us a coffee, a bottle, or a god damned Ferrari, go your hardest. Then hustle over to discord to claim your supporter role!");
    ImGui::Spacing();
    if (ImGui::BeginTable("donation-options", 3, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextColumn();
        if (ImGui::Button("GitHub Sponsors", ImVec2(-1, 44))) visit(kSponsorsUrl);
        hint("Account required\n0% fees to creator");
        ImGui::TableNextColumn();
        if (ImGui::Button("Buy Me a Coffee", ImVec2(-1, 44))) visit(kCoffeeUrl);
        hint("Guest checkout available");
        ImGui::TableNextColumn();
        if (ImGui::Button("Donate with PayPal", ImVec2(-1, 44))) visit(kPayPalUrl);
        hint("Guest checkout available\nLeast preferred option");
        ImGui::EndTable();
    }
    ImGui::EndChild();
    if (ImGui::Button("Close", ImVec2(0, 32)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void ConfigWindow::draw_feedback_dialog() {
    popup_size(640, 540);
    bool open = true;
    if (!ImGui::BeginPopupModal("Feedback / feature request##feedback", &open, ImGuiWindowFlags_NoSavedSettings)) return;
    const auto state = feedback_sender_.state();
    const bool sending = state == FeedbackSender::State::Sending;
    if (state == FeedbackSender::State::Sent && !feedback_sent_seen_) {
        feedback_message_[0] = '\0';
        feedback_sent_seen_ = true;
    }
    ImGui::BeginChild("feedback-content", ImVec2(0, -46));
    hint("Have an idea or found a problem? Send it to the PSVR2 Passthrough team.");
    ImGui::BeginDisabled(sending);
    ImGui::TextUnformatted("Name");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##feedback-name", feedback_name_, sizeof(feedback_name_));
    ImGui::TextUnformatted("Email");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##feedback-email", feedback_email_, sizeof(feedback_email_));
    hint("Your name and email are kept here while the config app is open.");
    ImGui::TextUnformatted("Message");
    if (ImGui::InputTextMultiline("##feedback-message", feedback_message_, sizeof(feedback_message_), ImVec2(-1, 135))) {
        feedback_sent_seen_ = true;
        feedback_error_.clear();
    }
    ImGui::EndDisabled();
    hint("Only these fields, the app name and version are sent. No logs are attached.");
    if (!feedback_error_.empty()) ImGui::TextWrapped("%s", feedback_error_.c_str());
    if (state == FeedbackSender::State::Failed) ImGui::TextWrapped("Could not send feedback. Your message is still here. Please try again.");
    else if (sending) ImGui::TextUnformatted("Sending...");
    else if (state == FeedbackSender::State::Sent && feedback_message_[0] == '\0') ImGui::TextUnformatted("Feedback sent. Thank you!");
    if (kFeedbackEndpoint[0] == '\0') hint("Feedback delivery is not configured yet.");
    ImGui::EndChild();
    if (ImGui::Button("Close", ImVec2(0, 32)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    ImGui::SameLine();
    ImGui::BeginDisabled(sending || kFeedbackEndpoint[0] == '\0');
    if (ImGui::Button(sending ? "Sending..." : "Send feedback", ImVec2(0, 32))) {
        const auto name = trim_feedback(feedback_name_);
        const auto email = trim_feedback(feedback_email_);
        const auto message = trim_feedback(feedback_message_);
        feedback_error_.clear();
        if (name.empty() || character_count(name) > 100) feedback_error_ = "Enter a name, up to 100 characters.";
        else if (character_count(email) > 254 || !valid_feedback_email(email)) feedback_error_ = "Enter a valid email address, up to 254 characters.";
        else if (message.empty() || character_count(message) > 4000) feedback_error_ = "Enter a message, up to 4000 characters.";
        else {
            feedback_sent_seen_ = false;
            feedback_sender_.send(feedback_payload(name, email, message, kCurrentVersion));
        }
    }
    ImGui::EndDisabled();
    ImGui::EndPopup();
}
} // namespace psvr2pt
