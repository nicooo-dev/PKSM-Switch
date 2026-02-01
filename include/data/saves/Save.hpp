#pragma once
#include <memory>
#include <pu/Plutonium>
#include <string>
#include <switch.h>

namespace pksm::saves {

class Save {
private:
    std::string name;
    std::string path;
    AccountUid userId;

public:
    Save(const std::string& name, const std::string& path, const AccountUid& userId) 
        : name(name), path(path), userId(userId) {}
    PU_SMART_CTOR(Save)

    const std::string& getName() const { return name; }
    const std::string& getPath() const { return path; }
    const AccountUid& getUserId() const { return userId; }
};

}  // namespace pksm::saves