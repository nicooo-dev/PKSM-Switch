#pragma once

#include <functional>
#include <memory>
#include <string>

#include "data/providers/interfaces/ISaveDataAccessor.hpp"
#include "data/saves/SaveData.hpp"
#include "data/titles/Title.hpp"
#include "utils/Logger.hpp"

using namespace pksm;

class SaveDataAccessor : public ISaveDataAccessor {
private:
    pksm::saves::SaveData::Ref currentSave;
    std::function<void(pksm::saves::SaveData::Ref)> onSaveDataChanged;
    bool hasChanges;
    AccountUid currentUserId;

    // Load actual save data from file
    pksm::saves::SaveData::Ref LoadSaveDataFromFile(
        const pksm::titles::Title::Ref& title,
        const std::string& saveName
    ) const;

public:
    explicit SaveDataAccessor(AccountUid currentUserId);
    virtual ~SaveDataAccessor() = default;

    void SetCurrentUser(AccountUid userId) { 
        LOG_DEBUG("SaveDataAccessor: Changing user ID from " + std::to_string(currentUserId.uid[1]) + 
                  " to " + std::to_string(userId.uid[1]));
        currentUserId = userId; 
    }

    // Unmount save device if mounted
    void unmountSaveDevice() override;

    // ISaveDataAccessor interface implementation
    pksm::saves::SaveData::Ref getCurrentSaveData() const override;
    bool loadSave(const pksm::titles::Title::Ref title, const std::string saveName) override;
    void setOnSaveDataChanged(std::function<void(pksm::saves::SaveData::Ref)> callback) override {
        onSaveDataChanged = callback;
    }
    bool saveChanges() override;
    bool hasUnsavedChanges() const override;
};
