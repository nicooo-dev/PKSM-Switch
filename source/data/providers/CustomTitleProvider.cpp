#include "data/providers/CustomTitleProvider.hpp"

#include "utils/Logger.hpp"

namespace {
std::vector<pksm::titles::Title::Ref> createInitialTitles() {
    std::vector<pksm::titles::Title::Ref> titles;
    
    // Create titles with error handling - don't crash if icons don't exist
    try {
        titles.push_back(pksm::titles::Title::New("Pokémon Yellow", "romfs:/gfx/mock/emulator/yellow_menu_icon.jpg", 0x00164A00));
    } catch (...) {
        LOG_WARNING("Failed to create Yellow title");
    }
    
    try {
        titles.push_back(pksm::titles::Title::New("Pokémon Crystal", "romfs:/gfx/mock/emulator/crystal_menu_icon.jpg", 0x00164B00));
    } catch (...) {
        LOG_WARNING("Failed to create Crystal title");
    }
    
    try {
        titles.push_back(pksm::titles::Title::New("Pokémon Emerald", "romfs:/gfx/mock/emulator/emerald_menu_icon.jpg", 0x00164C00));
    } catch (...) {
        LOG_WARNING("Failed to create Emerald title");
    }
    
    return titles;
}
}  // namespace

CustomTitleProvider::CustomTitleProvider() : customTitles(createInitialTitles()) {
    LOG_DEBUG("CustomTitleProvider initialized with " + std::to_string(customTitles.size()) + " titles");
}

std::vector<pksm::titles::Title::Ref> CustomTitleProvider::GetCustomTitles() const {
    return customTitles;
}
