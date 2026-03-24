#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace pksm::utils
{

    class SpriteAssetDownloader
    {
    public:
        struct ProgressInfo
        {
            enum class Phase
            {
                None = 0,
                ZipDownload,
                ZipExtract,
                IncrementalDownload
            };

            Phase phase = Phase::None;
            bool usedZip = false;
            bool usingFallback = false;
            std::size_t stageCurrent = 0;
            std::size_t stageTotal = 0;
            std::size_t totalMissing = 0;
            std::size_t processed = 0;
            std::size_t downloaded = 0;
            std::size_t failed = 0;
            std::size_t remaining = 0;
        };

        struct SyncResult
        {
            bool attemptedNetwork = false;
            bool downloadedDataJson = false;
            bool usedZipPack = false;
            bool zipPackVerified = false;
            bool zipFallbackTriggered = false;
            std::size_t downloadedSprites = 0;
            std::size_t skippedSprites = 0;
            std::size_t failedSprites = 0;
            std::size_t totalMissingSprites = 0;
            std::size_t remainingSprites = 0;
            bool budgetReached = false;
            std::string error;
        };

        static std::size_t CountMissingSprites(std::string *error = nullptr);

        static SyncResult SyncFromCdn(
            const std::string &cdnBase = "https://cdn.sigkill.tech/",
            std::size_t maxDownloadsPerRun = 24,
            std::uint32_t maxMilliseconds = 12000,
            const std::function<void(const ProgressInfo &)> &onProgress = nullptr);
    };

} // namespace pksm::utils
