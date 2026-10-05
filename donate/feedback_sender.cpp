#include "feedback_sender.h"
#include "support_content.hpp"
#include "feedback_payload.h"
#include <windows.h>
#include <winhttp.h>

namespace adamch_support {
namespace {
struct HttpHandle {
    HINTERNET handle = nullptr;
    explicit HttpHandle(HINTERNET value) : handle(value) {}
    ~HttpHandle() { if (handle) WinHttpCloseHandle(handle); }
};
bool post_feedback(const std::string& payload) {
    // The public relay owns delivery credentials. No webhook secret belongs in the app.
    if (content::feedbackEndpoint[0] == '\0') return false;
    const int size = MultiByteToWideChar(CP_UTF8, 0, content::feedbackEndpoint, -1, nullptr, 0);
    if (size <= 1) return false;
    std::wstring url(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, content::feedbackEndpoint, -1, url.data(), size);
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = static_cast<DWORD>(-1);
    parts.dwUrlPathLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS)
        return false;
    const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    const std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
    HttpHandle session(WinHttpOpen(L"AdamChApps/feedback",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session.handle) return false;
    WinHttpSetTimeouts(session.handle, 5000, 5000, 5000, 5000);
    HttpHandle connection(WinHttpConnect(session.handle, host.c_str(), parts.nPort, 0));
    if (!connection.handle) return false;
    HttpHandle request(WinHttpOpenRequest(connection.handle, L"POST", path.c_str(),
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
    if (!request.handle) return false;
    // Do not forward contact details to a redirected destination.
    DWORD redirects = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    WinHttpSetOption(request.handle, WINHTTP_OPTION_REDIRECT_POLICY, &redirects, sizeof(redirects));
    if (!WinHttpSendRequest(request.handle, L"Content-Type: application/json\r\n", -1L,
        const_cast<char*>(payload.data()), static_cast<DWORD>(payload.size()),
        static_cast<DWORD>(payload.size()), 0) || !WinHttpReceiveResponse(request.handle, nullptr))
        return false;
    DWORD status = 0, status_size = sizeof(status);
    if (!(WinHttpQueryHeaders(request.handle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size, WINHTTP_NO_HEADER_INDEX) &&
        status == content::feedbackHttpStatus)) return false;
    wchar_t media_type[128]{};
    DWORD media_size=sizeof(media_type);
    if(!WinHttpQueryHeaders(request.handle,WINHTTP_QUERY_CONTENT_TYPE,
        WINHTTP_HEADER_NAME_BY_INDEX,media_type,&media_size,WINHTTP_NO_HEADER_INDEX)) return false;
    std::string mime;
    for(wchar_t c:media_type){if(!c)break;if(c>127)return false;mime+=static_cast<char>(c);}
    const auto semicolon=mime.find(';');
    mime=trim_feedback(mime.substr(0,semicolon));
    for(char& c:mime)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if(mime!=content::feedbackContentType)return false;
    std::string response;
    char buffer[1024];
    DWORD received=0;
    do {
        if(!WinHttpReadData(request.handle,buffer,sizeof(buffer),&received)) return false;
        if(response.size()+received>content::feedbackMaxBytes) return false;
        response.append(buffer,received);
    }while(received);
    return feedback_acknowledged(response);
}
}
FeedbackSender::~FeedbackSender() { shutdown(); }
void FeedbackSender::shutdown() { if (thread_.joinable()) thread_.join(); }
FeedbackSender::State FeedbackSender::state() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}
void FeedbackSender::send(std::string payload) {
    if (state() == State::Sending) return;
    shutdown();
    { std::lock_guard<std::mutex> lock(mutex_); state_ = State::Sending; }
    try { thread_ = std::thread(&FeedbackSender::run, this, std::move(payload)); }
    catch (...) { std::lock_guard<std::mutex> lock(mutex_); state_ = State::Failed; }
}
void FeedbackSender::run(std::string payload) {
    bool sent = false;
    try { sent = post_feedback(payload); } catch (...) {}
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = sent ? State::Sent : State::Failed;
}
} // namespace adamch_support
