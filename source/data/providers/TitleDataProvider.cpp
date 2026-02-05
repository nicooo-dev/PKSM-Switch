#include "TitleDataProvider.hpp"
#include <fstream>
#include <filesystem>
#include <set>
#include <vector>
#include <cstring>
#include <stdio.h>

#include <switch.h>

#include <nlohmann/json.hpp>

namespace pksm::titles {

static constexpr const char* JSON_PATH = "romfs:/gfx/data/data.json";

void TitleDataProvider::GetInstalledApplicationIds(std::set<u64>& out_ids) const {
    Result rc = nsInitialize();
    if (R_FAILED(rc)) return;

    constexpr size_t MAX_ENTRIES = 0x400; // 1024 entries
    constexpr size_t BUFFER_SIZE = (sizeof(NsApplicationRecord) * MAX_ENTRIES);

    auto records = reinterpret_cast<NsApplicationRecord*>(std::calloc(1, BUFFER_SIZE));
    if (!records) {
        nsExit();
        return;
    }

    s32 count = 0;

    rc = nsListApplicationRecord(records, MAX_ENTRIES, 0, &count);
    if (R_SUCCEEDED(rc)) {
        out_ids.clear();
        for (s32 i = 0; i < count; i++) {
           NsApplicationRecord* cur_record = &(records[i]);
           if (cur_record->application_id) out_ids.insert(cur_record->application_id);
        }
    }

    // Cleanup

    std::free(records);
    nsExit();
}

TitleDataProvider::TitleDataProvider()
    : mockCartridgeTitle(nullptr)
{
    try {
        std::ifstream f(JSON_PATH);
        if (f.is_open()) {
            nlohmann::json j;
            f >> j;

            // load all console titles from JSON
            for (const auto& game : j["games"]) {
                if (game["category"] != "console") continue;

                u64 titleId = 0;
                try { titleId = std::stoull(game["title_id"].get<std::string>(), nullptr, 16); }
                catch (...) { continue; }

                std::string name = game["name"].get<std::string>();
                std::string iconPath = game["icon_path"].get<std::string>();

                installedTitles.push_back(std::make_shared<Title>(name, iconPath, titleId));
            }
        }
    } catch (...) {
        installedTitles.clear();
    }

    try {
        mockCartridgeTitle = std::make_shared<Title>(
            "Pokémon Legends: Arceus",
            "romfs:/gfx/mock/console/arceus_menu_icon.jpg",
            0x00175E00
        );

        // Mock emulator titles
        mockEmulatorTitles.clear();
        mockEmulatorTitles.push_back(std::make_shared<Title>("Pokémon Sun", "romfs:/gfx/mock/emulator/sun_menu_icon.jpg", 0x00180014));
        mockEmulatorTitles.push_back(std::make_shared<Title>("Pokémon Ultra Moon", "romfs:/gfx/mock/emulator/ultra_moon_menu_icon.jpg", 0x00180015));
    } catch (...) {
        mockCartridgeTitle = nullptr;
        mockEmulatorTitles.clear();
    }
}

Title::Ref TitleDataProvider::GetGameCardTitle() const {
    return mockCartridgeTitle;
}

std::vector<Title::Ref> TitleDataProvider::GetInstalledTitles(const AccountUid& /*userId*/) const {

    std::set<u64> installedIds;
    GetInstalledApplicationIds(installedIds);

    std::vector<std::shared_ptr<Title>> filteredList;
    filteredList.reserve(installedTitles.size());

    for (const auto& t : installedTitles) {
        if (!t) continue;

        if (installedIds.find(t->getTitleId()) != installedIds.end()) {
            filteredList.push_back(t);
        }
    }

    // return the filtered list of installed titles only
    return filteredList;
}

std::vector<Title::Ref> TitleDataProvider::GetEmulatorTitles() const {
    return mockEmulatorTitles;
}

std::vector<Title::Ref> TitleDataProvider::GetCustomTitles() const {
    return customTitles;
}

}