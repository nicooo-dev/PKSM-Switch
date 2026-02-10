#include "data/providers/SaveDataAccessor.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include <switch.h>

#include "pksmcore/enums/GameVersion.hpp"
#include "pksmcore/enums/Generation.hpp"
#include "pksmcore/enums/Gender.hpp"
#include "pksmcore/sav/Sav.hpp"
#include "utils/Logger.hpp"

using namespace pksm;

namespace {

pksm::saves::Generation ToAppGeneration(pksm::Generation gen) {
    switch (gen) {
        case pksm::Generation::ONE:
            return pksm::saves::Generation::ONE;
        case pksm::Generation::TWO:
            return pksm::saves::Generation::TWO;
        case pksm::Generation::THREE:
            return pksm::saves::Generation::THREE;
        case pksm::Generation::FOUR:
            return pksm::saves::Generation::FOUR;
        case pksm::Generation::FIVE:
            return pksm::saves::Generation::FIVE;
        case pksm::Generation::SIX:
            return pksm::saves::Generation::SIX;
        case pksm::Generation::SEVEN:
            return pksm::saves::Generation::SEVEN;
        case pksm::Generation::LGPE:
            return pksm::saves::Generation::LGPE;
        case pksm::Generation::EIGHT:
            return pksm::saves::Generation::EIGHT;
        default:
            return pksm::saves::Generation::EIGHT;
    }

}

pksm::saves::BagPouch ToAppBagPouch(const pksm::Sav::Pouch pouch) {
    using SP = pksm::Sav::Pouch;
    using AP = pksm::saves::BagPouch;

    switch (pouch) {
        case SP::NormalItem:
            return AP::NormalItem;
        case SP::KeyItem:
            return AP::KeyItem;
        case SP::TM:
            return AP::TM;
        case SP::Mail:
            return AP::Mail;
        case SP::Medicine:
            return AP::Medicine;
        case SP::Berry:
            return AP::Berry;
        case SP::Ball:
            return AP::Ball;
        case SP::Battle:
            return AP::Battle;
        case SP::Candy:
            return AP::Candy;
        case SP::ZCrystals:
            return AP::ZCrystals;
        case SP::Treasure:
            return AP::Treasure;
        case SP::Ingredient:
            return AP::Ingredient;
        case SP::PCItem:
            return AP::PCItem;
        case SP::RotomPower:
            return AP::RotomPower;
        case SP::CatchingItem:
            return AP::CatchingItem;
        default:
            return AP::Unknown;
    }
}

pksm::saves::Gender ToAppGender(pksm::Gender gender) {
    switch (gender) {
        case pksm::Gender::Female:
            return pksm::saves::Gender::Female;
        case pksm::Gender::Male:
        default:
            return pksm::saves::Gender::Male;
    }
}

pksm::saves::GameVersion ToAppGameVersion(pksm::GameVersion version) {
    using GV = pksm::GameVersion;
    using AGV = pksm::saves::GameVersion;

    switch (version) {
        case GV::RD:
            return AGV::RD;
        case GV::GN:
            return AGV::GN;
        case GV::BU:
            return AGV::BU;
        case GV::YW:
            return AGV::YW;
        case GV::GD:
            return AGV::GD;
        case GV::SV:
            return AGV::SV;
        case GV::C:
            return AGV::C;
        case GV::R:
            return AGV::R;
        case GV::S:
            return AGV::S;
        case GV::E:
            return AGV::E;
        case GV::FR:
            return AGV::FR;
        case GV::LG:
            return AGV::LG;
        case GV::D:
            return AGV::D;
        case GV::P:
            return AGV::P;
        case GV::Pt:
            return AGV::Pt;
        case GV::HG:
            return AGV::HG;
        case GV::SS:
            return AGV::SS;
        case GV::W:
            return AGV::W;
        case GV::B:
            return AGV::B;
        case GV::W2:
            return AGV::W2;
        case GV::B2:
            return AGV::B2;
        case GV::X:
            return AGV::X;
        case GV::Y:
            return AGV::Y;
        case GV::OR:
            return AGV::OR;
        case GV::AS:
            return AGV::AS;
        case GV::SN:
            return AGV::SN;
        case GV::MN:
            return AGV::MN;
        case GV::US:
            return AGV::US;
        case GV::UM:
            return AGV::UM;
        case GV::GP:
            return AGV::GP;
        case GV::GE:
            return AGV::GE;
        case GV::SW:
            return AGV::SW;
        case GV::SH:
            return AGV::SH;
        default:
            return AGV::SW;
    }
}

}  // namespace

SaveDataAccessor::SaveDataAccessor(AccountUid currentUserId)
  : currentSave(nullptr), hasChanges(false), currentUserId(currentUserId) {}

void SaveDataAccessor::unmountSaveDevice() {
    LOG_DEBUG("Unmounting save device");
    Result rc = fsdevUnmountDevice("save");
    if (R_FAILED(rc)) {
        std::stringstream hexStream;
        hexStream << std::hex << rc;
        LOG_DEBUG("Save device was not mounted or unmount failed: " + std::to_string(rc) + " (0x" + hexStream.str() + ")");
    } else {
        LOG_DEBUG("Successfully unmounted save device");
    }
}

pksm::saves::SaveData::Ref SaveDataAccessor::getCurrentSaveData() const {
    return currentSave;
}

bool SaveDataAccessor::loadSave(const pksm::titles::Title::Ref title, const std::string saveName) {
    if (!title) {
        return false;
    }

    auto loaded = LoadSaveDataFromFile(title, saveName);
    if (!loaded) {
        this->unmountSaveDevice();
        return false;
    }

    currentSave = loaded;
    hasChanges = false;

    if (onSaveDataChanged) {
        onSaveDataChanged(currentSave);
    }

    return true;
}

bool SaveDataAccessor::saveChanges() {
    if (!currentSave) {
        LOG_ERROR("No save data to save");
        return false;
    }

    // TODO: Implement actual save writing using PKSM-Core
    // for now, just mark as saved
    hasChanges = false;
    LOG_DEBUG("Save changes requested (not fully implemented)");
    return true;
}

bool SaveDataAccessor::hasUnsavedChanges() const {
    return hasChanges;
}

pksm::saves::SaveData::Ref SaveDataAccessor::LoadSaveDataFromFile(
    const pksm::titles::Title::Ref& title,
    const std::string& saveName
) const {
    if (!title) {
        return nullptr;
    }

    const bool isSaveDevicePath = saveName.rfind("save:/", 0) == 0;
    const std::string savePath = isSaveDevicePath ? saveName : (std::string("save:/") + saveName);

    if (isSaveDevicePath) {
        LOG_DEBUG("Mounting save data for title ID: " + std::to_string(title->getTitleId()) + 
                  " user ID: " + std::to_string(currentUserId.uid[1]) + 
                  " save path: " + savePath);
        
        // Mount save data for read-write access
        Result rc = fsdevMountSaveData("save", title->getTitleId(), currentUserId);
        if (R_FAILED(rc)) {
            std::stringstream hexStream;
            hexStream << std::hex << rc;
            LOG_ERROR("Failed to mount save data. Title ID: " + std::to_string(title->getTitleId()) + 
                      " Result: " + std::to_string(rc) + " (0x" + hexStream.str() + ")");
            return nullptr;
        }
        LOG_DEBUG("Successfully mounted save data");
    }

    if (!std::filesystem::exists(savePath)) {
        if (isSaveDevicePath) {
            fsdevUnmountDevice("save");
        }
        LOG_ERROR("Save file does not exist: " + savePath);
        return nullptr;
    }

    std::ifstream in(savePath, std::ios::binary | std::ios::ate);
    if (!in.good()) {
        if (isSaveDevicePath) {
            fsdevUnmountDevice("save");
        }
        LOG_ERROR("Failed to open save file: " + savePath);
        return nullptr;
    }

    const std::streamsize size = in.tellg();
    if (size <= 0) {
        if (isSaveDevicePath) {
            fsdevUnmountDevice("save");
        }
        LOG_ERROR("Invalid save file size: " + savePath);
        return nullptr;
    }

    in.seekg(0, std::ios::beg);

    std::vector<u8> buffer_vec;
    try {
        buffer_vec.resize(size);
        if (!in.read(reinterpret_cast<char*>(buffer_vec.data()), size)) {
            if (isSaveDevicePath) {
                fsdevUnmountDevice("save");
            }
            LOG_ERROR("Failed to read save file: " + savePath);
            return nullptr;
        }
    } catch (const std::bad_alloc& e) {
        LOG_ERROR("Memory allocation failed while reading save: " + std::string(e.what()));
        if (isSaveDevicePath) {
            fsdevUnmountDevice("save");
        }
        return nullptr;
    }

    std::shared_ptr<u8[]> buffer;
    try {
        buffer = std::shared_ptr<u8[]>(new u8[size], std::default_delete<u8[]>());
        std::copy(buffer_vec.begin(), buffer_vec.end(), buffer.get());
    } catch (const std::bad_alloc& e) {
        LOG_ERROR("Memory allocation failed for PKSM-Core buffer: " + std::string(e.what()));
        return nullptr;
    }
    
    // keep device mounted for StorageScreen access, don't unmount here

    std::unique_ptr<pksm::Sav> sav;
    try {
        sav = pksm::Sav::getSave(buffer, static_cast<size_t>(size));
    } catch (const std::exception& e) {
        LOG_ERROR("PKSM-Core failed to parse save: " + std::string(e.what()));
        LOG_ERROR("This save file may be corrupted, incompatible, or from an unsupported game.");
        LOG_ERROR("Please try selecting a different save file or check if the game is properly installed.");
        if (isSaveDevicePath) {
            fsdevUnmountDevice("save");
        }
        return nullptr;
    }

    if (!sav) {
        LOG_ERROR("PKSM-Core could not detect a valid save type");
        if (isSaveDevicePath) {
            fsdevUnmountDevice("save");
        }
        return nullptr;
    }

    const auto generation = ToAppGeneration(sav->generation());
    const auto version = ToAppGameVersion(sav->version());
    const auto gender = ToAppGender(sav->gender());

    const u16 tid = sav->TID();
    const u16 sid = sav->SID();
    const u8 badges = sav->badges();

    const int dexSeen = sav->dexSeen();
    const int dexCaught = sav->dexCaught();

    const u16 playedHours = sav->playedHours();
    const u8 playedMinutes = sav->playedMinutes();
    const u8 playedSeconds = sav->playedSeconds();

    LOG_DEBUG("[SaveDataAccessor] Loaded save. Dex seen = " + std::to_string(dexSeen) + ", Dex Caught = " + std::to_string(dexCaught));

    // keep device mounted here aswell for StorageScreen access

    auto save_data = std::make_shared<pksm::saves::SaveData>(
        saveName,
        generation,
        version,
        sav->otName(),
        tid,
        sid,
        gender,
        badges,
        static_cast<u16>(std::max(0, dexSeen)),
        static_cast<u16>(std::max(0, dexCaught)),
        0,
        playedHours,
        playedMinutes,
        playedSeconds
    );

    std::vector<pksm::saves::BagItem> bag_items;
    try {
        const auto pouches = sav->pouches();
        for (const auto& pouch_info : pouches) {
            const auto pouch = pouch_info.first;
            const auto slot_count = pouch_info.second;
            for (int slot = 0; slot < slot_count; slot++) {
                auto item = sav->item(pouch, static_cast<u16>(slot));
                if (!item) {
                    continue;
                }

                const auto id = item->id();
                const auto count = item->count();
                if ((id == 0) || (count == 0)) {
                    continue;
                }

                bag_items.push_back(pksm::saves::BagItem{
                    ToAppBagPouch(pouch),
                    static_cast<u16>(id),
                    static_cast<u16>(count),
                });
            }
        }
    } catch (const std::exception& e) {
        LOG_ERROR("[SaveDataAccessor] Failed to extract bag items: " + std::string(e.what()));
    }

    save_data->setBagItems(std::move(bag_items));
    return save_data;
}
