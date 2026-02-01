#include "data/providers/SaveDataProvider.hpp"
#include <fstream>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <switch.h>
#include <unistd.h>

SaveDataProvider::SaveDataProvider(const AccountUid& initialUserId) 
    : initialUserId(initialUserId), titlesLoaded(false) {
}

std::vector<std::pair<pksm::titles::Title::Ref, std::string>> SaveDataProvider::LoadInstalledTitles() const {
    // Return cached titles if already loaded
    if (titlesLoaded) {
        return cachedTitles;
    }
    
    std::vector<std::pair<pksm::titles::Title::Ref, std::string>> installedTitles;
    
    std::ifstream f(JSON_PATH);
    if (!f.is_open()) {
        return installedTitles;
    }
    
    try {
        nlohmann::json j;
        f >> j;
        
        for (const auto& game : j["games"]) {
            if (game["category"] != "console") continue;
            
            u64 titleId = 0;
            try { 
                titleId = std::stoull(game["title_id"].get<std::string>(), nullptr, 16); 
            } catch (...) { 
                continue; 
            }
            
            std::string name = game["name"].get<std::string>();
            std::string iconPath = game["icon_path"].get<std::string>();
            std::string mainSaveFile = game["main_save_file"].get<std::string>();
            
            auto title = std::make_shared<pksm::titles::Title>(name, iconPath, titleId);
            installedTitles.push_back(std::make_pair(title, mainSaveFile));
        }
    } catch (const std::exception& e) {
    }
    
    // Cache the results
    cachedTitles = installedTitles;
    titlesLoaded = true;
    
    return installedTitles;
}

bool SaveDataProvider::FastSaveCheck(const pksm::titles::Title::Ref& title, const AccountUid& uid, const std::string& mainSaveFile) const {
    if (mainSaveFile.empty()) {
        return false; // No specific file to check
    }
    
    // Quick mount check without full directory scan
    Result rc = fsdevMountSaveData("save", title->getTitleId(), uid);
    if (R_FAILED(rc)) {
        return false;
    }
    
    // Only check for the specific file
    const std::string savePath = "save:/" + mainSaveFile;
    bool exists = std::filesystem::exists(savePath);
    
    // Immediately unmount
    fsdevUnmountDevice("save");
    
    return exists;
}

std::string SaveDataProvider::FindCheckpointDirectory(const std::string& titleIdStr) const {
    std::string checkpointBasePath = "sdmc:/switch/Checkpoint/saves";
    
    if (!std::filesystem::exists(checkpointBasePath)) {
        return "";
    }
    
    try {
        for (const auto& entry : std::filesystem::directory_iterator(checkpointBasePath)) {
            if (entry.is_directory()) {
                std::string dirName = entry.path().filename().string();
                
                // Check if directory name contains our title ID (partial match)
                if (dirName.find(titleIdStr) != std::string::npos) {
                    return entry.path().string();
                }
            }
        }
    } catch (const std::exception& e) {
        // Directory scan failed
    }
    
    return "";
}

std::string SaveDataProvider::ExtractZipFile(const std::string& zipPath, const std::string& titleIdStr) const {
    // Create temporary directory for extraction
    std::string tempDir = "sdmc:/switch/PKSM/temp/" + titleIdStr + "_" + std::to_string(std::time(nullptr));
    std::filesystem::create_directories(tempDir);
    
    // For now, return the zip path directly (extraction will be implemented later)
    // This is a placeholder until we add proper zip extraction
    return zipPath;
}

std::vector<pksm::saves::Save::Ref> SaveDataProvider::ScanBackupSaves(
    const pksm::titles::Title::Ref& title,
    const AccountUid& uid,
    const std::string& mainSaveFile
) const {
    std::vector<pksm::saves::Save::Ref> backupSaves;
    
    // Convert title ID to hex string for backup directories
    char titleIdStr[17];
    snprintf(titleIdStr, sizeof(titleIdStr), "%016lx", title->getTitleId());
    
    // Check Checkpoint backup directory (with partial match)
    std::string checkpointPath = FindCheckpointDirectory(titleIdStr);
    if (!checkpointPath.empty()) {
        try {
            for (const auto& entry : std::filesystem::directory_iterator(checkpointPath)) {
                std::string backupName = entry.path().filename().string();
                std::string saveFile;
                
                if (entry.is_directory()) {
                    // Directory backup
                    saveFile = entry.path().string() + "/" + mainSaveFile;
                    
                    if (std::filesystem::exists(saveFile)) {
                        backupSaves.push_back(pksm::saves::Save::New(
                            "[Backup] " + backupName, 
                            saveFile, 
                            uid
                        ));
                    }
                } else if (backupName.length() >= 4 && backupName.substr(backupName.length() - 4) == ".zip") {
                    // Zip backup - extract on the fly (placeholder for now)
                    std::string extractedPath = ExtractZipFile(entry.path().string(), titleIdStr);
                    backupSaves.push_back(pksm::saves::Save::New(
                        "[Backup] " + backupName, 
                        extractedPath, 
                        uid
                    ));
                }
            }
        } catch (const std::exception& e) {
            // Directory scan failed
        }
    }
    
    // Check JKSV backup directory
    std::string jksvPath = "sdmc:/JKSV/" + std::string(titleIdStr);
    if (std::filesystem::exists(jksvPath)) {
        try {
            for (const auto& entry : std::filesystem::directory_iterator(jksvPath)) {
                std::string backupName = entry.path().filename().string();
                std::string saveFile;
                
                if (entry.is_directory()) {
                    // Directory backup
                    saveFile = entry.path().string() + "/" + mainSaveFile;
                    
                    if (std::filesystem::exists(saveFile)) {
                        backupSaves.push_back(pksm::saves::Save::New(
                            "[JKSV] " + backupName, 
                            saveFile, 
                            uid
                        ));
                    }
                } else if (backupName.length() >= 4 && backupName.substr(backupName.length() - 4) == ".zip") {
                    // Zip backup - extract on the fly (placeholder for now)
                    std::string extractedPath = ExtractZipFile(entry.path().string(), titleIdStr);
                    backupSaves.push_back(pksm::saves::Save::New(
                        "[JKSV] " + backupName, 
                        extractedPath, 
                        uid
                    ));
                }
            }
        } catch (const std::exception& e) {
            // Directory scan failed
        }
    }
    
    return backupSaves;
}

std::vector<pksm::saves::Save::Ref> SaveDataProvider::ScanSavesForTitle(
    const pksm::titles::Title::Ref& title,
    const AccountUid& uid,
    const std::string& mainSaveFile
) const {
    std::vector<pksm::saves::Save::Ref> saves;
    
    // try to mount save data
    Result rc = fsdevMountSaveData("save", title->getTitleId(), uid);
    if (R_FAILED(rc)) {
        // Even if mounting fails, we can still check for backups
        auto backupSaves = ScanBackupSaves(title, uid, mainSaveFile);
        saves.insert(saves.end(), backupSaves.begin(), backupSaves.end());
        return saves;
    }
    
    // Only look for the specific main save file
    if (!mainSaveFile.empty()) {
        const std::string savePath = "save:/" + mainSaveFile;
        if (std::filesystem::exists(savePath)) {
            saves.push_back(pksm::saves::Save::New("Current Save", savePath, uid));
        }
    } else {
        // If no main save file specified, scan all files (fallback)
        const std::string saveMountPath = "save:/";
        try {
            for (const auto& entry : std::filesystem::directory_iterator(saveMountPath)) {
                if (entry.is_regular_file()) {
                    std::string fileName = entry.path().filename().string();
                    std::string fullPath = entry.path().string();
                    
                    saves.push_back(pksm::saves::Save::New(fileName, fullPath, uid));
                }
            }
        } catch (const std::exception& e) {
        }
    }
    
    // unmount save data
    fsdevUnmountDevice("save");
    
    // Add backup saves
    auto backupSaves = ScanBackupSaves(title, uid, mainSaveFile);
    saves.insert(saves.end(), backupSaves.begin(), backupSaves.end());
    
    return saves;
}

std::vector<pksm::saves::Save::Ref> SaveDataProvider::GetUserSaves(const AccountUid& uid) {
    std::vector<pksm::saves::Save::Ref> allSaves;
    
    auto installedTitles = LoadInstalledTitles();
    
    for (const auto& [title, mainSaveFile] : installedTitles) {
        auto titleSaves = ScanSavesForTitle(title, uid, mainSaveFile);
        allSaves.insert(allSaves.end(), titleSaves.begin(), titleSaves.end());
    }
    
    return allSaves;
}

std::vector<pksm::saves::Save::Ref> SaveDataProvider::GetSavesForTitle(
    const pksm::titles::Title::Ref& title,
    const std::optional<AccountUid>& currentUser
) const {
    if (!title || !currentUser) {
        return {};
    }
    
    // Find the main save file for this title
    auto installedTitles = LoadInstalledTitles();
    for (const auto& [installedTitle, mainSaveFile] : installedTitles) {
        if (installedTitle->getTitleId() == title->getTitleId()) {
            // Use fast check first to avoid unnecessary operations
            if (!mainSaveFile.empty() && !FastSaveCheck(title, *currentUser, mainSaveFile)) {
                return {}; // No save file exists
            }
            return ScanSavesForTitle(title, *currentUser, mainSaveFile);
        }
    }
    
    // Fallback to scanning all files if title not found
    return ScanSavesForTitle(title, *currentUser, "");
}

bool SaveDataProvider::LoadSave(
    const pksm::titles::Title::Ref& title,
    const std::string& saveName,
    const AccountUid* userId
) {
    if (!title || !userId) {
        return false;
    }
    
    // mount save data
    Result rc = fsdevMountSaveData("save", title->getTitleId(), *userId);
    if (R_FAILED(rc)) {
        return false;
    }
    
    // Check if save file exists
    std::string savePath = "save:/" + saveName;
    bool exists = std::filesystem::exists(savePath);
    
    // Unmount
    fsdevUnmountDevice("save");
    
    return exists;
}