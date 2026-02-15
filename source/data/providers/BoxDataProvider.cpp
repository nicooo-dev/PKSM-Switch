#include "data/providers/BoxDataProvider.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>

#include <switch.h>

#include "pksmcore/pkx/PKX.hpp"
#include "pksmcore/sav/Sav.hpp"
#include "utils/Logger.hpp"

namespace {

std::unique_ptr<pksm::Sav> LoadSavFromPath(const std::string &save_name) {
    const bool isSaveDevicePath = save_name.rfind("save:/", 0) == 0;
    const std::string savePath = isSaveDevicePath ? save_name : (std::string("save:/") + save_name);

    if (!std::filesystem::exists(savePath)) {
        throw std::runtime_error("Save file does not exist: " + savePath);
    }

    std::ifstream in(savePath, std::ios::binary | std::ios::ate);
    if (!in.good()) {
        throw std::runtime_error("Failed to open save file: " + savePath);
    }

    const std::streamsize size = in.tellg();
    if (size <= 0) {
        throw std::runtime_error("Invalid save file size: " + savePath);
    }

    in.seekg(0, std::ios::beg);

    std::vector<u8> buffer_vec;
    buffer_vec.resize(size);
    if (!in.read(reinterpret_cast<char*>(buffer_vec.data()), size)) {
        throw std::runtime_error("Failed to read save file: " + savePath);
    }

    auto buffer = std::shared_ptr<u8[]>(new u8[size], std::default_delete<u8[]>());
    std::copy(buffer_vec.begin(), buffer_vec.end(), buffer.get());

    auto sav = pksm::Sav::getSave(buffer, static_cast<size_t>(size));
    if (!sav) {
        throw std::runtime_error("PKSM-Core could not detect a valid save type");
    }

    return sav;
}

} // namespace

BoxDataProvider::BoxDataProvider() = default;

BoxDataProvider::~BoxDataProvider() = default;

pksm::Sav* BoxDataProvider::GetSavForSaveData(const pksm::saves::SaveData::Ref& saveData) const {
    if (!saveData) {
        cachedSav.reset();
        cachedSaveName.clear();
        return nullptr;
    }

    const auto &save_name = saveData->getName();
    if (cachedSav && (cachedSaveName == save_name)) {
        return cachedSav.get();
    }

    try {
        cachedSav = LoadSavFromPath(save_name);
        cachedSaveName = save_name;
        return cachedSav.get();
    } catch (const std::exception &e) {
        pksm::utils::Logger::Error(std::string("[BoxDataProvider] Failed to cache save: ") + e.what());
        cachedSav.reset();
        cachedSaveName.clear();
        return nullptr;
    }
}

size_t BoxDataProvider::GetBoxCount(const pksm::saves::SaveData::Ref& saveData) const {
    if (!saveData) {
        return 0;
    }

    try {
        auto *sav = GetSavForSaveData(saveData);
        if (!sav) {
            return 0;
        }
        const int count = sav->maxBoxes();
        return count > 0 ? static_cast<size_t>(count) : 0;
    } catch (const std::exception &e) {
        pksm::utils::Logger::Error(std::string("[BoxDataProvider] GetBoxCount failed: ") + e.what());
        return 0;
    }
}

pksm::ui::BoxData BoxDataProvider::GetBoxData(const pksm::saves::SaveData::Ref& saveData, int boxIndex) const {
    return LoadBoxDataFromSave(saveData, boxIndex);
}

bool BoxDataProvider::SetBoxData(
    const pksm::saves::SaveData::Ref& saveData,
    int boxIndex,
    const pksm::ui::BoxData& boxData
) {
    (void)saveData;
    (void)boxIndex;
    (void)boxData;
    pksm::utils::Logger::Error("[BoxDataProvider] SetBoxData not implemented yet");
    return false;
}

bool BoxDataProvider::SetPokemonData(
    const pksm::saves::SaveData::Ref& saveData,
    int boxIndex,
    int slotIndex,
    const pksm::ui::BoxPokemonData& pokemonData
) {
    (void)saveData;
    (void)boxIndex;
    (void)slotIndex;
    (void)pokemonData;
    pksm::utils::Logger::Error("[BoxDataProvider] SetPokemonData not implemented yet");
    return false;
}

std::unique_ptr<pksm::PKX> BoxDataProvider::GetPokemon(
    const pksm::saves::SaveData::Ref& saveData,
    int boxIndex,
    int slotIndex
) const {
    if (!saveData) {
        return nullptr;
    }

    if ((boxIndex < 0) || (slotIndex < 0) || (slotIndex >= 30)) {
        return nullptr;
    }

    try {
        auto *sav = GetSavForSaveData(saveData);
        if (!sav) {
            return nullptr;
        }

        const int max_boxes = sav->maxBoxes();
        if ((boxIndex < 0) || (boxIndex >= max_boxes)) {
            return nullptr;
        }

        auto pk = sav->pkm(static_cast<u8>(boxIndex), static_cast<u8>(slotIndex));
        if (!pk) {
            return nullptr;
        }

        if (pk->isEncrypted()) {
            pk->decrypt();
        }

        if (static_cast<u16>(pk->species()) == 0) {
            return nullptr;
        }

        return pk;
    } catch (const std::exception &e) {
        pksm::utils::Logger::Error(std::string("[BoxDataProvider] GetPokemon failed: ") + e.what());
        return nullptr;
    }
}

pksm::ui::BoxData BoxDataProvider::LoadBoxDataFromSave(
    const pksm::saves::SaveData::Ref& saveData,
    int boxIndex
) const {
    pksm::ui::BoxData boxData;

    if (!saveData) {
        boxData.name = "Box";
        boxData.resize(30);
        return boxData;
    }

    try {
        auto *sav = GetSavForSaveData(saveData);
        if (!sav) {
            boxData.name = "Box";
            boxData.resize(30);
            return boxData;
        }

        const int max_boxes = sav->maxBoxes();
        if ((boxIndex < 0) || (boxIndex >= max_boxes)) {
            boxData.name = "Box";
            boxData.resize(30);
            return boxData;
        }

        boxData.name = sav->boxName(static_cast<u8>(boxIndex));

        // normalize box names (some games may return empty strings by design, like LGPE)
        boxData.name.erase(std::remove(boxData.name.begin(), boxData.name.end(), '\0'), boxData.name.end());
        const bool nameAllWhitespace = std::all_of(boxData.name.begin(), boxData.name.end(), [](unsigned char c) {
            return std::isspace(c) != 0;
        });
        if (boxData.name.empty() || nameAllWhitespace) {
            boxData.name = "Box " + std::to_string(boxIndex + 1);
        }

        boxData.resize(30);

        for (int slot = 0; slot < 30; slot++) {
            auto pk = sav->pkm(static_cast<u8>(boxIndex), static_cast<u8>(slot));
            if (!pk) {
                boxData.pokemon[slot] = pksm::ui::BoxPokemonData();
                continue;
            }

            // make fields readable across gens if the PKX is still encrypted
            if (pk->isEncrypted()) {
                pk->decrypt();
            }

            const u16 species = static_cast<u16>(pk->species());
            if (species == 0) {
                boxData.pokemon[slot] = pksm::ui::BoxPokemonData();
                continue;
            }

            const u16 form_u16 = pk->alternativeForm();
            const u8 form = form_u16 > 255 ? 0 : static_cast<u8>(form_u16);
            const bool shiny = pk->shiny();

            boxData.pokemon[slot] = pksm::ui::BoxPokemonData(species, form, shiny);
        }

        return boxData;
    } catch (const std::exception &e) {
        pksm::utils::Logger::Error(std::string("[BoxDataProvider] LoadBoxDataFromSave failed: ") + e.what());
        boxData.name = "Box";
        boxData.resize(30);
        return boxData;
    }
}

bool BoxDataProvider::SaveBoxDataToFile(
    const pksm::saves::SaveData::Ref& saveData,
    int boxIndex,
    const pksm::ui::BoxData& boxData
) const {
    (void)saveData;
    (void)boxIndex;
    (void)boxData;
    return false;
}