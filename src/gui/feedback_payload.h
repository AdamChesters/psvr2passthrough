#pragma once

#include <cctype>
#include <string>
#include <string_view>

namespace psvr2pt {
inline std::string trim_feedback(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    return value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
}
inline std::string json_feedback_string(std::string_view value) {
    std::string result = "\"";
    const char* hex = "0123456789abcdef";
    for (unsigned char c : value) {
        if (c == '\\' || c == '\"') { result += '\\'; result += c; }
        else if (c < 0x20) { result += "\\u00"; result += hex[c >> 4]; result += hex[c & 15]; }
        else result += c;
    }
    return result + "\"";
}
inline bool valid_feedback_email(std::string_view email) {
    const auto at = email.find('@');
    if (at == std::string_view::npos || at == 0 || at + 1 >= email.size() ||
        email.find('@', at + 1) != std::string_view::npos) return false;
    for (unsigned char c : email) if (std::isspace(c) || c < 0x20) return false;
    const auto dot = email.find('.', at + 1);
    return dot != std::string_view::npos && dot > at + 1 && dot + 1 < email.size();
}
inline std::string feedback_payload(std::string_view name, std::string_view email,
                                    std::string_view message, std::string_view version) {
    return "{\"app\":\"psvr2passthrough\",\"name\":" + json_feedback_string(name) +
        ",\"email\":" + json_feedback_string(email) + ",\"message\":" + json_feedback_string(message) +
        ",\"version\":" + json_feedback_string(version) + "}";
}
} // namespace psvr2pt
