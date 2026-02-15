#pragma once

#include <mutex>
#include <string>
#include <vector>

namespace pksm::utils {

class NotificationManager {
public:
    struct Notification {
        std::string title;
        std::string body;
    };

    static void Push(const std::string& text);
    static void Push(const std::string& title, const std::string& body);
    static std::vector<Notification> ConsumeAll();

private:
    static std::mutex mutex;
    static std::vector<Notification> pending;
};

}  // namespace pksm::utils