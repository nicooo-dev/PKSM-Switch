#pragma once

#include <functional>
#include <memory>
#include <string>

#include "data/providers/interfaces/ISaveDataAccessor.hpp"
#include "data/saves/SaveData.hpp"
#include "data/titles/Title.hpp"

class SaveDataAccessor : public ISaveDataAccessor {
private:
    pksm::saves::SaveData::Ref currentSave;
    std::function<void(pksm::saves::SaveData::Ref)> onSaveDataChanged;
    bool hasChanges;

    // Load actual save data from file
    pksm::saves::SaveData::Ref LoadSaveDataFromFile(
        const pksm::titles::Title::Ref& title,
        const std::string& saveName
    ) const;

public:
    SaveDataAccessor();
    virtual ~SaveDataAccessor() = default;

    // ISaveDataAccessor interface implementation
    pksm::saves::SaveData::Ref getCurrentSaveData() const override;
    bool loadSave(const pksm::titles::Title::Ref title, const std::string saveName) override;
    void setOnSaveDataChanged(std::function<void(pksm::saves::SaveData::Ref)> callback) override {
        onSaveDataChanged = callback;
    }
    bool saveChanges() override;
    bool hasUnsavedChanges() const override;
};
