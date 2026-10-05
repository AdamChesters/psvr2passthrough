#include "feedback_payload.h"
#include <cassert>
#include <iostream>
int main() {
    using namespace adamch_support;
    assert(trim_feedback(" \r\n hello\t ") == "hello");
    assert(trim_feedback(" \n ").empty());
    assert(feedback_acknowledged(" { \"ok\" : true }\n"));
    assert(!feedback_acknowledged("{\"ok\":false}"));
    assert(!feedback_acknowledged("{}"));
    assert(!feedback_acknowledged("{\"o k\":true}"));
    assert(!feedback_acknowledged("not json"));
    assert(valid_feedback_email("person@example.com"));
    for (const char* email : {"", "bad", "@example.com", "x@y", "x@@z.com", "x@z.", "x@ .com"})
        assert(!valid_feedback_email(email));
    const auto payload = feedback_payload("Name\"\\", "person@example.com", "line one\nline two\t\x01", "v0.6.1-alpha", "psvr2passthrough");
    assert(payload.find("\"app\":\"psvr2passthrough\"") != std::string::npos);
    assert(payload.find("Name\\\"\\\\") != std::string::npos);
    assert(payload.find("\\u000a") != std::string::npos);
    assert(payload.find("\\u0001") != std::string::npos);
    std::cout << payload;
}
