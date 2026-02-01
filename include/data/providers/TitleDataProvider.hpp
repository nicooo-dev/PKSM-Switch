#pragma once

#include <vector>
#include <memory>
#include <string>
#include <nlohmann/json.hpp>
#include <switch/types.h> // for u64
#include <set>
#include "data/titles/Title.hpp"  // use the existing Title class
#include "data/providers/interfaces/ITitleDataProvider.hpp"

namespace pksm::titles {

class TitleDataProvider : public ITitleDataProvider {
public:
    using Ref = std::shared_ptr<TitleDataProvider>;

    TitleDataProvider(); // default constructor, loads JSON from fixed path

    std::vector<Title::Ref> GetInstalledTitles(const AccountUid& userId) const override;
    Title::Ref GetGameCardTitle() const override;
    std::vector<Title::Ref> GetEmulatorTitles() const override;
    std::vector<Title::Ref> GetCustomTitles() const override;
    void GetInstalledApplicationIds(std::set<u64>& out_ids) const;

private:
    std::vector<std::shared_ptr<Title>> installedTitles;
    std::vector<Title::Ref> customTitles;

    Title::Ref mockCartridgeTitle;
    std::vector<Title::Ref> mockEmulatorTitles;
};

} // namespace pksm::titles