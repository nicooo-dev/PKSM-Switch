#pragma once

#include <memory>
#include <optional>
#include <string>
#include <switch.h>
#include <vector>

#include "data/providers/interfaces/ISaveDataProvider.hpp"
#include "data/saves/Save.hpp"
#include "data/titles/Title.hpp"

class SaveDataProvider : public ISaveDataProvider {
private:
    AccountUid initialUserId;
    static constexpr const char* JSON_PATH = "romfs:/gfx/data/data.json";
    
    // Cache for loaded titles to avoid repeated JSON parsing
    mutable std::vector<std::pair<pksm::titles::Title::Ref, std::string>> cachedTitles;
    mutable bool titlesLoaded = false;
    
    // Load installed console titles from JSON
    std::vector<std::pair<pksm::titles::Title::Ref, std::string>> LoadInstalledTitles() const;
    
    // Check if save exists without mounting (faster check)
    bool FastSaveCheck(const pksm::titles::Title::Ref& title, const AccountUid& uid, const std::string& mainSaveFile) const;
    
    // Find Checkpoint directory by partial title ID match
    std::string FindCheckpointDirectory(const std::string& titleIdStr) const;
    
    // Extract zip file to temporary directory
    std::string ExtractZipFile(const std::string& zipPath, const std::string& titleIdStr) const;
    
    // Scan for backup saves in Checkpoint/JKSV directories
    std::vector<pksm::saves::Save::Ref> ScanBackupSaves(
        const pksm::titles::Title::Ref& title,
        const AccountUid& uid,
        const std::string& mainSaveFile
    ) const;
    
    // Scan for save files for a given title
    std::vector<pksm::saves::Save::Ref> ScanSavesForTitle(
        const pksm::titles::Title::Ref& title,
        const AccountUid& uid,
        const std::string& mainSaveFile
    ) const;

public:
    explicit SaveDataProvider(const AccountUid& initialUserId);
    virtual ~SaveDataProvider() = default;

    // Main function to get all saves for a user
    std::vector<pksm::saves::Save::Ref> GetUserSaves(const AccountUid& uid);

    // ISaveDataProvider interface implementation
    std::vector<pksm::saves::Save::Ref> GetSavesForTitle(
        const pksm::titles::Title::Ref& title,
        const std::optional<AccountUid>& currentUser = std::nullopt
    ) const override;

    bool LoadSave(
        const pksm::titles::Title::Ref& title,
        const std::string& saveName,
        const AccountUid* userId = nullptr
    ) override;
};
