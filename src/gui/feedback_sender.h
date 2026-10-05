#pragma once
#include <mutex>
#include <string>
#include <thread>

namespace psvr2pt {
class FeedbackSender {
public:
    enum class State { Idle, Sending, Sent, Failed };
    ~FeedbackSender();
    void send(std::string payload);
    void shutdown();
    State state() const;
private:
    void run(std::string payload);
    std::thread thread_;
    mutable std::mutex mutex_;
    State state_ = State::Idle;
};
} // namespace psvr2pt
