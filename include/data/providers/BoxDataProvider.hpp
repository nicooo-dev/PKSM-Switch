#pragma once

#include <memory>
#include <string>
#include <vector>

#include "data/providers/interfaces/IBoxDataProvider.hpp"
#include "data/saves/SaveData.hpp"
#include "gui/shared/components/BoxPokemonData.hpp"

namespace pksm {
class Sav;
}

class BoxDataProvider : public IBoxDataProvider {
private:
    mutable std::string cachedSaveName;
    mutable std::unique_ptr<pksm::Sav> cachedSav;

    pksm::Sav* GetSavForSaveData(const pksm::saves::SaveData::Ref& saveData) const;

    // load box data from save file
    pksm::ui::BoxData LoadBoxDataFromSave(
        const pksm::saves::SaveData::Ref& saveData,
        int boxIndex
    ) const;

    // save box data to save file (TODO)
    bool SaveBoxDataToFile(
        const pksm::saves::SaveData::Ref& saveData,
        int boxIndex,
        const pksm::ui::BoxData& boxData
    ) const;

public:
    BoxDataProvider();
    ~BoxDataProvider() override;

    // IBoxDataProvider interface implementation
    size_t GetBoxCount(const pksm::saves::SaveData::Ref& saveData) const override;
    pksm::ui::BoxData GetBoxData(const pksm::saves::SaveData::Ref& saveData, int boxIndex) const override;
    bool SetBoxData(
        const pksm::saves::SaveData::Ref& saveData,
        int boxIndex,
        const pksm::ui::BoxData& boxData
    ) override;
    bool SetPokemonData(
        const pksm::saves::SaveData::Ref& saveData,
        int boxIndex,
        int slotIndex,
        const pksm::ui::BoxPokemonData& pokemonData
    ) override;

    std::unique_ptr<pksm::PKX> GetPokemon(
        const pksm::saves::SaveData::Ref& saveData,
        int boxIndex,
        int slotIndex
    ) const override;
};
