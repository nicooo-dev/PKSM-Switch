#include "utils/NotificationManager.hpp"

namespace pksm::utils {

std::mutex NotificationManager::mutex;
std::vector<NotificationManager::Notification> NotificationManager::pending;

void NotificationManager::Push(const std::string& text) {
    std::scoped_lock lock(mutex);
    pending.push_back(Notification{text, ""});
}

void NotificationManager::Push(const std::string& title, const std::string& body) {
    std::scoped_lock lock(mutex);
    pending.push_back(Notification{title, body});
}

std::vector<NotificationManager::Notification> NotificationManager::ConsumeAll() {
    std::scoped_lock lock(mutex);
    auto out = pending;
    pending.clear();
    return out;
}

}  // namespace pksm::utils