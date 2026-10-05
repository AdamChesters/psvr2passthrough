#pragma once
#include "feedback_sender.h"
#include "support_content.hpp"
#include <array>
#include <functional>
#include <string>
namespace adamch_support {
struct AppIdentity {
    std::string appName,appVersion,appId;
    std::function<void()> appLogo,checkForUpdates;
    std::string updateStatus;
    float scale=1.f;
};
class SupportPanel {
public:
    void open();
    void openFeedback();
    void draw(const AppIdentity& identity);
    const std::string& feedbackError()const{return feedback_error_;}
    void shutdown(){sender_.shutdown();}
private:
    void drawSupport(const AppIdentity& identity);
    void drawFeedback(const AppIdentity& identity);
    FeedbackSender sender_;
    bool open_feedback_=false,feedback_sent_seen_=false,update_requested_=false;
    std::array<char,content::nameMax*4+1> name_{};
    std::array<char,content::emailMax*4+1> email_{};
    std::array<char,content::messageMax*4+1> message_{};
    std::string feedback_error_;
};
} // namespace adamch_support
