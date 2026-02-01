#pragma once

#include <memory>
#include <vector>

#include "data/providers/interfaces/IBoxDataProvider.hpp"
#include "data/saves/SaveData.hpp"
#include "gui/shared/components/BoxPokemonData.hpp"

class BoxDataProvider : public IBoxDataProvider {
private:
    // Load box data from actual save file
    pksm::ui::BoxData LoadBoxDataFromSave(
        const pksm::saves::SaveData::Ref& saveData,
        int boxIndex
    ) const;

    // Save box data to actual save file
    bool SaveBoxDataToFile(
        const pksm::saves::SaveData::Ref& saveData,
        int boxIndex,
        const pksm::ui::BoxData& boxData
    ) const;

public:
    BoxDataProvider();
    virtual ~BoxDataProvider() = default;

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
};
