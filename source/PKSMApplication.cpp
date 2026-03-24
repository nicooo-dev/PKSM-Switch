#include "PKSMApplication.hpp"

#include <array>
#include <chrono>
#include <filesystem>
#include <format>
#include <switch.h>

#include "data/providers/BankBoxDataProvider.hpp"
#include "data/providers/BoxDataProvider.hpp"
#include "data/providers/PartyDataProvider.hpp"
#include "data/providers/SaveDataAccessor.hpp"
#include "data/providers/SaveDataProvider.hpp"
#include "data/providers/TitleDataProvider.hpp"
#include "gui/screens/emulator-fileselect-screen/EmulatorFileSelectScreen.hpp"
#include "gui/shared/FontManager.hpp"
#include "gui/shared/UIConstants.hpp"
#include "utils/Logger.hpp"
#include "utils/NotificationManager.hpp"
#include "utils/SettingsManager.hpp"
#include "utils/SpriteAssetDownloader.hpp"
#include "utils/PokemonSpriteManager.hpp"

namespace pksm {

namespace {
constexpr std::size_t kSpriteSyncBatchMaxDownloads = 32;
constexpr std::uint32_t kSpriteSyncBatchMaxMilliseconds = 25000;
constexpr std::size_t kSpriteSyncMaxNoProgressBatches = 5;
}  // namespace

pu::ui::render::RendererInitOptions PKSMApplication::CreateRendererOptions() {
    LOG_DEBUG("Creating renderer options...");
    LOG_MEMORY();

    // Initialize SDL with hardware acceleration and vsync
    // This enables proper display, audio, and controller support
    auto renderer_opts = pu::ui::render::RendererInitOptions(SDL_INIT_EVERYTHING, pu::ui::render::RendererHardwareFlags);

    // Enable PNG/JPG loading for UI assets
    renderer_opts.init_img = true;
    renderer_opts.sdl_img_flags = IMG_INIT_PNG | IMG_INIT_JPG;

    // Enable romfs for loading assets bundled with the NRO
    renderer_opts.init_romfs = true;

    LOG_DEBUG("Renderer options created successfully");
    return renderer_opts;
}

void PKSMApplication::ConfigureFonts(pu::ui::render::RendererInitOptions& renderer_opts) {
    LOG_DEBUG("Configuring fonts...");

    // Register default (light) font
    renderer_opts.AddDefaultFontPath("romfs:/gfx/fonts/dinnextw1g_light.ttf");

    // Register all custom font sizes
    pksm::ui::FontManager::ConfigureRendererFontSizes(renderer_opts);

    LOG_DEBUG("Fonts configured successfully");
}

void PKSMApplication::ConfigureInput(pu::ui::render::RendererInitOptions& renderer_opts) {
    LOG_DEBUG("Configuring input...");

    renderer_opts.SetInputPlayerCount(1);  // Accept input from one player
    renderer_opts.AddInputNpadStyleTag(HidNpadStyleSet_NpadStandard);  // Accept standard controller input
    renderer_opts.AddInputNpadIdType(HidNpadIdType_Handheld);  // Accept handheld mode input
    renderer_opts.AddInputNpadIdType(HidNpadIdType_No1);  // Accept controller 1 input

    LOG_DEBUG("Input configured successfully");
}

void PKSMApplication::RegisterAdditionalFonts() {
    LOG_DEBUG("Registering additional fonts...");

    try {
        // Register heavy font for all custom sizes
        pksm::ui::FontManager::RegisterFont(
            "romfs:/gfx/fonts/dinnextw1g_heavy.ttf",
            pksm::ui::global::MakeHeavyFontName
        );

        // Register medium font for all custom sizes
        pksm::ui::FontManager::RegisterFont(
            "romfs:/gfx/fonts/dinnextw1g_medium.ttf",
            pksm::ui::global::MakeMediumFontName
        );

        // Register switch button font for all custom sizes
        pksm::ui::FontManager::RegisterFont(
            "romfs:/gfx/fonts/NintendoExtLE003-M.ttf",
            pksm::ui::global::MakeSwitchButtonFontName
        );

        LOG_DEBUG("Additional fonts registered successfully");
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to register additional fonts: " + std::string(e.what()));
        throw;
    }
}

PKSMApplication::PKSMApplication(
    pu::ui::render::Renderer::Ref renderer,
    std::unique_ptr<data::AccountManager> accountManager,
    ITitleDataProvider::Ref titleProvider,
    ISaveDataProvider::Ref saveProvider,
    ISaveDataAccessor::Ref saveDataAccessor,
    IBoxDataProvider::Ref boxDataProvider,
    IBoxDataProvider::Ref bankBoxDataProvider,
    IPartyDataProvider::Ref partyDataProvider
)
  : pu::ui::Application(renderer),
    accountManager(std::move(accountManager)),
    titleProvider(std::move(titleProvider)),
    saveProvider(std::move(saveProvider)),
    saveDataAccessor(std::move(saveDataAccessor)),
    boxDataProvider(std::move(boxDataProvider)),
    bankBoxDataProvider(std::move(bankBoxDataProvider)),
    partyDataProvider(std::move(partyDataProvider)) {
    // Add render callback to process account updates
    AddRenderCallback([this]() {
        this->accountManager->ProcessPendingUpdates();
        this->UpdateStartupSyncUI();
    });

    // global notifications: render above layouts/overlays
    notificationCenter = pksm::ui::NotificationCenter::New(0, 0, 640);
    AddRenderTopCallback([this](pu::ui::render::Renderer::Ref& drawer) {
        if (notificationCenter) {
            notificationCenter->OnRender(drawer, notificationCenter->GetProcessedX(), notificationCenter->GetProcessedY());
        }
    });
}

PKSMApplication::~PKSMApplication() {
    spriteSyncStopRequested = true;
    if (spriteSyncWorker.joinable()) {
        spriteSyncWorker.join();
    }
}

PKSMApplication::Ref PKSMApplication::Initialize() {
    try {
        // Initialize logger first
        utils::Logger::Initialize();
        LOG_INFO("Initializing PKSM...");
        LOG_MEMORY();  // Initial memory state

        // Initialize settings manager
        LOG_DEBUG("Initializing settings manager...");
        auto& settingsManager = utils::SettingsManager::getInstance();
        if (!settingsManager.initialize()) {
            LOG_WARNING("Failed to initialize settings manager, using defaults");
        } else {
            LOG_DEBUG("Settings manager initialized successfully");
        }

        // Initialize renderer with all configurations
        auto renderer_opts = CreateRendererOptions();
        ConfigureFonts(renderer_opts);
        ConfigureInput(renderer_opts);

        LOG_DEBUG("Creating renderer...");
        auto renderer = pu::ui::render::Renderer::New(renderer_opts);

        LOG_DEBUG("Initializing renderer...");
        renderer->Initialize();
        LOG_MEMORY();  // Memory after renderer initialization

        // Register additional fonts after romfs is mounted
        RegisterAdditionalFonts();

        // Initialize Pokemon sprite manager (optional)
        // Prefer external SD assets (3DS-like), fallback to romfs
        if (!utils::PokemonSpriteManager::Initialize("sdmc:/switch/PKSM/assets/data.json")) {
            if (!utils::PokemonSpriteManager::Initialize("romfs:/gfx/data/data.json")) {
                LOG_WARNING("Failed to initialize sprite manager, continuing without sprites");
                utils::NotificationManager::Push("Sprites", "Missing sprites pack: sdmc:/switch/PKSM/assets");
            } else {
                LOG_INFO("Sprite manager initialized from romfs fallback");
            }
        } else {
            LOG_INFO("Sprite manager initialized from sdmc external assets");
        }

        auto recordingInitResult = appletInitializeGamePlayRecording();
        if (R_FAILED(recordingInitResult)) {
            LOG_ERROR("Failed to initialize game play recording");
        } else {
            appletSetGamePlayRecordingState(true);
        }

        // Initialize account manager
        LOG_DEBUG("Initializing account manager...");
        auto accountManager = std::make_unique<data::AccountManager>();
        Result res = accountManager->Initialize();
        if (R_FAILED(res)) {
            LOG_ERROR("Failed to initialize account manager");
            throw std::runtime_error("Account manager initialization failed");
        }

        // -----------------------------
        // Create data providers
        // -----------------------------
        LOG_DEBUG("Creating data providers...");
        auto titleProviderConcrete = std::make_shared<pksm::titles::TitleDataProvider>();
        auto saveProviderConcrete = std::make_shared<SaveDataProvider>(accountManager->GetCurrentAccount());
        auto saveDataAccessorConcrete = std::make_shared<SaveDataAccessor>(accountManager->GetCurrentAccount());
        auto boxDataProviderConcrete = std::make_shared<BoxDataProvider>();
        auto bankBoxDataProviderConcrete = std::make_shared<BankBoxDataProvider>();
        auto partyDataProviderConcrete = std::make_shared<PartyDataProvider>();

        // Cast to interface types expected by PKSMApplication
        ITitleDataProvider::Ref titleProvider = std::static_pointer_cast<ITitleDataProvider>(titleProviderConcrete);
        ISaveDataProvider::Ref saveProvider = std::static_pointer_cast<ISaveDataProvider>(saveProviderConcrete);
        ISaveDataAccessor::Ref saveDataAccessor = std::static_pointer_cast<ISaveDataAccessor>(saveDataAccessorConcrete);
        IBoxDataProvider::Ref boxDataProvider = std::static_pointer_cast<IBoxDataProvider>(boxDataProviderConcrete);
        IBoxDataProvider::Ref bankBoxDataProvider = std::static_pointer_cast<IBoxDataProvider>(bankBoxDataProviderConcrete);
        IPartyDataProvider::Ref partyDataProvider = std::static_pointer_cast<IPartyDataProvider>(partyDataProviderConcrete);

        LOG_MEMORY();  // Memory after data provider initialization

        // -----------------------------
        // Create PKSMApplication instance
        // -----------------------------
        LOG_DEBUG("Creating PKSMApplication instance...");
        auto app = PKSMApplication::New(
            renderer,
            std::move(accountManager),
            titleProvider,
            saveProvider,
            saveDataAccessor,
            boxDataProvider,
            bankBoxDataProvider,
            partyDataProvider
        );

        LOG_DEBUG("Preparing application...");
        app->Prepare();

        LOG_INFO("PKSM initialization complete");
        LOG_MEMORY();  // Final memory state
        return app;

    } catch (const std::exception& e) {
        LOG_ERROR("Failed to initialize application: " + std::string(e.what()));
        throw;
    }
}

void PKSMApplication::ShowStartupScreen() {
    if (startupScreen) {
        this->LoadLayout(startupScreen);
    }
}

void PKSMApplication::StartSpriteSyncIfNeeded() {
    if (spriteSyncRunning.load() || spriteSyncDone.load()) {
        return;
    }

    spriteSyncUiCompleted = false;
    spriteSyncProcessed = 0;
    spriteSyncDownloaded = 0;
    spriteSyncFailed = 0;
    spriteSyncNoProgressBatches = 0;
    spriteSyncPhase = static_cast<int>(utils::SpriteAssetDownloader::ProgressInfo::Phase::None);
    spriteSyncStageCurrent = 0;
    spriteSyncStageTotal = 0;
    spriteSyncUsedZip = false;
    spriteSyncUsedFallback = false;
    spriteSyncStopRequested = false;

    if (spriteSyncWorker.joinable()) {
        spriteSyncWorker.join();
    }

    std::string countError;
    const std::size_t missing = utils::SpriteAssetDownloader::CountMissingSprites(&countError);
    spriteSyncTotal = missing;
    spriteSyncRemaining = missing;

    if (!countError.empty()) {
        LOG_WARNING("Sprite asset precheck warning: " + countError);
    }

    if (missing == 0) {
        LOG_INFO("Sprite assets already complete; skipping startup sync");
        spriteSyncDone = true;
        return;
    }

    LOG_INFO("Sprite sync starting: " + std::to_string(missing) + " sprites missing");
    spriteSyncRunning = true;
    spriteSyncDone = false;

    spriteSyncWorker = std::thread([this]() {
        try {
            std::size_t cumulativeDownloaded = spriteSyncDownloaded.load(std::memory_order_relaxed);
            std::size_t cumulativeFailed = spriteSyncFailed.load(std::memory_order_relaxed);

            while (!spriteSyncStopRequested.load(std::memory_order_relaxed)) {
                const std::size_t total = spriteSyncTotal.load(std::memory_order_relaxed);
                const std::size_t processed = spriteSyncProcessed.load(std::memory_order_relaxed);

                if (total == 0 || processed >= total) {
                    break;
                }

                const std::size_t batchBaseProcessed = processed;
                const std::size_t batchBaseDownloaded = cumulativeDownloaded;
                const std::size_t batchBaseFailed = cumulativeFailed;

                const auto sync = utils::SpriteAssetDownloader::SyncFromCdn(
                    "https://cdn.sigkill.tech/",
                    kSpriteSyncBatchMaxDownloads,
                    kSpriteSyncBatchMaxMilliseconds,
                    [this, total, batchBaseProcessed, batchBaseDownloaded, batchBaseFailed](const utils::SpriteAssetDownloader::ProgressInfo& info) {
                        spriteSyncPhase = static_cast<int>(info.phase);
                        spriteSyncStageCurrent = info.stageCurrent;
                        spriteSyncStageTotal = info.stageTotal;

                        if (info.usedZip) {
                            spriteSyncUsedZip = true;
                        }
                        if (info.usingFallback) {
                            spriteSyncUsedFallback = true;
                        }

                        if (info.phase == utils::SpriteAssetDownloader::ProgressInfo::Phase::IncrementalDownload) {
                            const std::size_t globalProcessed = std::min(total, batchBaseProcessed + info.processed);
                            spriteSyncProcessed = globalProcessed;
                            spriteSyncDownloaded = batchBaseDownloaded + info.downloaded;
                            spriteSyncFailed = batchBaseFailed + info.failed;
                        }
                    }
                );

                if (!sync.error.empty()) {
                    LOG_WARNING("Sprite asset sync warning: " + sync.error);
                }

                if (sync.usedZipPack) {
                    spriteSyncUsedZip = true;
                }
                if (sync.zipFallbackTriggered) {
                    spriteSyncUsedFallback = true;
                }

                cumulativeDownloaded += sync.downloadedSprites;
                cumulativeFailed += sync.failedSprites;
                spriteSyncDownloaded = cumulativeDownloaded;
                spriteSyncFailed = cumulativeFailed;
                spriteSyncRemaining = sync.remainingSprites;

                const std::size_t newlyProcessed = sync.downloadedSprites + sync.failedSprites;
                const std::size_t nextProcessed = std::min(total, processed + newlyProcessed);
                spriteSyncProcessed = nextProcessed;

                if (sync.downloadedSprites == 0) {
                    spriteSyncNoProgressBatches = spriteSyncNoProgressBatches.load(std::memory_order_relaxed) + 1;
                } else {
                    spriteSyncNoProgressBatches = 0;
                }

                const bool noProgressLimitReached = spriteSyncNoProgressBatches.load(std::memory_order_relaxed) >= kSpriteSyncMaxNoProgressBatches;
                const bool finished = (sync.remainingSprites == 0) || noProgressLimitReached || (nextProcessed >= total);

                if (noProgressLimitReached && sync.remainingSprites > 0) {
                    LOG_WARNING(
                        "Sprite sync stopped after repeated zero-download batches; continuing startup with "
                        + std::to_string(sync.remainingSprites) + " sprites still missing"
                    );
                }

                if (finished) {
                    break;
                }
            }
        } catch (const std::exception& e) {
            LOG_ERROR("Sprite sync worker exception: " + std::string(e.what()));
        } catch (...) {
            LOG_ERROR("Sprite sync worker unknown exception");
        }

        spriteSyncRunning = false;
        spriteSyncDone = true;
    });
}

void PKSMApplication::UpdateStartupSyncUI() {
    if (!startupScreen || spriteSyncUiCompleted.load()) {
        return;
    }

    const std::size_t total = spriteSyncTotal.load(std::memory_order_relaxed);
    const std::size_t processed = spriteSyncProcessed.load(std::memory_order_relaxed);

    if (spriteSyncRunning.load(std::memory_order_relaxed)) {
        using SyncPhase = utils::SpriteAssetDownloader::ProgressInfo::Phase;
        const auto phase = static_cast<SyncPhase>(spriteSyncPhase.load(std::memory_order_relaxed));
        const std::size_t stageCurrent = spriteSyncStageCurrent.load(std::memory_order_relaxed);
        const std::size_t stageTotal = spriteSyncStageTotal.load(std::memory_order_relaxed);

        static const std::array<const char*, 4> dots = {"", ".", "..", "..."};
        const auto dotIndex = static_cast<std::size_t>(
            (std::chrono::duration_cast<std::chrono::milliseconds>(
                 std::chrono::steady_clock::now().time_since_epoch()
             ).count() / 350) % static_cast<long long>(dots.size())
        );

        if (phase == SyncPhase::ZipDownload) {
            float progress = 0.0f;
            if (stageTotal > 0) {
                progress = static_cast<float>(stageCurrent) / static_cast<float>(stageTotal);
            }
            startupScreen->SetProgress(progress);

            std::string text = "Downloading sprite pack";
            if (stageTotal > 0) {
                text += " "
                    + std::to_string(stageCurrent / 1024)
                    + "/"
                    + std::to_string(stageTotal / 1024)
                    + "KB";
            }
            text += dots[dotIndex];
            startupScreen->SetLoadingText(text);
            return;
        }

        if (phase == SyncPhase::ZipExtract) {
            float progress = 0.0f;
            if (stageTotal > 0) {
                progress = static_cast<float>(stageCurrent) / static_cast<float>(stageTotal);
            }
            startupScreen->SetProgress(progress);
            startupScreen->SetLoadingText(
                "Extracting sprite pack "
                + std::to_string(stageCurrent)
                + "/"
                + std::to_string(stageTotal)
                + dots[dotIndex]
            );
            return;
        }

        if (total == 0) {
            startupScreen->SetProgress(0.0f);
            startupScreen->SetLoadingText("Preparing sprite sync" + std::string(dots[dotIndex]));
            return;
        }

        startupScreen->SetProgress(static_cast<float>(processed) / static_cast<float>(total));
        startupScreen->SetLoadingText(
            "Downloading sprites "
            + std::to_string(processed)
            + "/"
            + std::to_string(total)
            + dots[dotIndex]
        );
        return;
    }

    if (!spriteSyncDone.load(std::memory_order_relaxed)) {
        return;
    }

    if (spriteSyncWorker.joinable()) {
        spriteSyncWorker.join();
    }

    startupScreen->SetProgress(1.0f);

    if (total == 0) {
        startupScreen->SetLoadingText("Sprites ready");
    } else {
        const std::size_t downloaded = spriteSyncDownloaded.load(std::memory_order_relaxed);
        const std::size_t failed = spriteSyncFailed.load(std::memory_order_relaxed);
        const std::size_t remaining = spriteSyncRemaining.load(std::memory_order_relaxed);
        const bool usedZip = spriteSyncUsedZip.load(std::memory_order_relaxed);
        const bool usedFallback = spriteSyncUsedFallback.load(std::memory_order_relaxed);

        startupScreen->SetLoadingText("Sprite sync complete");

        std::string body = std::to_string(downloaded) + " downloaded";
        if (failed > 0) {
            body += ", " + std::to_string(failed) + " failed";
        }
        if (remaining > 0) {
            body += ", " + std::to_string(remaining) + " remaining";
        }
        if (usedZip) {
            body += ", zip pack used";
        }
        if (usedFallback) {
            body += ", fallback incremental used";
        }
        utils::NotificationManager::Push("Sprites", body);
        LOG_INFO("Sprite assets sync complete: " + body);
    }

    spriteSyncUiCompleted = true;
    startupScreen->Complete();
}

void PKSMApplication::ShowMainMenu() {
    LOG_DEBUG("Switching to main menu");
    this->LoadLayout(this->mainMenu);
}

void PKSMApplication::ShowSettingsScreen() {
    LOG_DEBUG("Switching to settings screen");
    this->LoadLayout(this->settingsScreen);
}

void PKSMApplication::ShowEditorScreen() {
    LOG_DEBUG("Switching to editor screen");

    // Recover save context if it was lost
    if ((!saveDataAccessor || !saveDataAccessor->getCurrentSaveData()) && saveDataAccessor && lastLoadedTitle && !lastLoadedSavePath.empty()) {
        LOG_WARNING("Editor requested without current save; trying reload from cached selection");
        if (!saveDataAccessor->loadSave(lastLoadedTitle, lastLoadedSavePath)) {
            utils::NotificationManager::Push("Editor", "No save loaded. Please load a save first.");
            this->ShowTitleLoadScreen();
            return;
        }
    }

    this->LoadLayout(this->editorScreen);
}

void PKSMApplication::ShowTitleLoadScreen() {
    LOG_DEBUG("Switching to title load screen");
    // unmount any save that may be mounted still from previous save load
    saveDataAccessor->unmountSaveDevice();
    this->LoadLayout(this->titleLoadScreen);
    // refresh the save list when returning to title load screen
    if (titleLoadScreen) {
        try {
            titleLoadScreen->RefreshSaves();
            LOG_DEBUG("Save list refreshed after returning to title load screen");
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to refresh save list: " + std::string(e.what()));
        }
    }
}

void PKSMApplication::ShowStorageScreen() {
    LOG_DEBUG("Switching to storage screen");
    this->LoadLayout(this->storageScreen);
}

void PKSMApplication::ShowBagScreen() {
    LOG_DEBUG("Switching to bag screen");
    this->LoadLayout(this->bagScreen);
}

void PKSMApplication::ShowEmulatorFileSelectScreen() {
    LOG_DEBUG("Switching to emulator file select screen");
    if (this->emulatorFileSelectScreen) {
        this->LoadLayout(this->emulatorFileSelectScreen);
    }
}

void PKSMApplication::OnSaveSelected(pksm::titles::Title::Ref title, pksm::saves::Save::Ref save) {
    LOG_DEBUG("Save selected: " + save->getName() + " for title: " + title->getName());
    LOG_DEBUG("Title ID: " + std::to_string(title->getTitleId()));
    LOG_DEBUG("Save path: " + save->getPath());
    
    // load the save data using the actual file path
    if (saveDataAccessor->loadSave(title, save->getPath())) {
        LOG_DEBUG("Successfully loaded save data");

        utils::NotificationManager::Push("Save Loaded", save->getName() + " Loaded successfully.  (" + title->getName() + ")");

        // Cache last loaded selection for lazy recovery in other screens
        this->lastLoadedTitle = title;
        this->lastLoadedSavePath = save->getPath();

        // Now that the save is loaded, show the main menu
        this->ShowMainMenu();
    } else {
        LOG_ERROR("Failed to load save data");

        utils::NotificationManager::Push("Title Not Supported", title->getName() + " is not currently supported.");
    }
}

void PKSMApplication::OnLoad() {
    try {
        LOG_DEBUG("Loading title screen...");
        LOG_MEMORY();

        emulatorFileSelectScreen = pksm::layout::EmulatorFileSelectScreen::New(
            [this]() { this->ShowTitleLoadScreen(); },
            [this](pu::ui::Overlay::Ref overlay) { this->StartOverlay(overlay); },
            [this]() { this->EndOverlay(); }
        );

        // Create title load screen
        LOG_DEBUG("Creating title load screen...");
        titleLoadScreen = pksm::layout::TitleLoadScreen::New(
            titleProvider,
            saveProvider,
            *accountManager,
            [this](pu::ui::Overlay::Ref overlay) { this->StartOverlay(overlay); },
            [this]() { this->EndOverlay(); },
            [this]() {
                this->Close(false);
            },
            [this]() {
                if (this->emulatorFileSelectScreen) {
                    this->LoadLayout(this->emulatorFileSelectScreen);
                }
            },
            [this](pksm::titles::Title::Ref title, pksm::saves::Save::Ref save) { this->OnSaveSelected(title, save); }
        );

        const bool firstEmuSetup = !std::filesystem::exists("sdmc:/switch/PKSM/emulator_saves.json");

        auto& emulatorFileSelectScreen = this->emulatorFileSelectScreen;

        // Create main menu with back callback and overlay handlers
        // Create navigation callbacks for menu buttons
        LOG_DEBUG("Creating navigation callbacks...");
        std::map<pksm::ui::MenuButtonType, std::function<void()>> navigationCallbacks = {
            {pksm::ui::MenuButtonType::Storage, [this]() { this->ShowStorageScreen(); }},
            {pksm::ui::MenuButtonType::Editor, [this]() { this->ShowEditorScreen(); }},
            {pksm::ui::MenuButtonType::Events, [this]() { LOG_DEBUG("Events button pressed (not implemented)"); }},
            {pksm::ui::MenuButtonType::Bag, [this]() { this->ShowBagScreen(); }},
            {pksm::ui::MenuButtonType::Scripts, [this]() { LOG_DEBUG("Scripts button pressed (not implemented)"); }},
            {pksm::ui::MenuButtonType::Settings, [this]() { this->ShowSettingsScreen(); }}
        };

        // Create startup screen
        LOG_DEBUG("Creating startup screen...");
        startupScreen = pksm::layout::StartupScreen::New(
            [this, firstEmuSetup, emulatorFileSelectScreen]() {
                // When startup screen completes, continue to firstEmuSetup if needed
                if (firstEmuSetup) {
                    this->LoadLayout(emulatorFileSelectScreen);
                } else {
                    this->ShowTitleLoadScreen();
                }
            },
            [this, firstEmuSetup, emulatorFileSelectScreen]() {
                // Skip sprite download
                spriteSyncStopRequested = true;
                spriteSyncUiCompleted = true;

                if (firstEmuSetup) {
                    this->LoadLayout(emulatorFileSelectScreen);
                } else {
                    this->ShowTitleLoadScreen();
                }
            }
        );
        startupScreen->SetProgress(0.0f);
        startupScreen->SetLoadingText("Checking sprites...");

        // Create main menu
        LOG_DEBUG("Creating main menu...");
        mainMenu = pksm::layout::MainMenu::New(
            [this]() { this->ShowTitleLoadScreen(); },
            [this](pu::ui::Overlay::Ref overlay) { this->StartOverlay(overlay); },
            [this]() { this->EndOverlay(); },
            saveDataAccessor,  // Pass the save data accessor to the main menu
            boxDataProvider,  // Allow main menu to flush/discard save box changes
            partyDataProvider,  // Allow main menu to flush/discard party changes
            bankBoxDataProvider,  // Allow main menu to flush/discard bank changes
            navigationCallbacks  // Pass navigation callbacks to the main menu
        );

        // Create storage screen
        LOG_DEBUG("Creating storage screen...");
        storageScreen = pksm::layout::StorageScreen::New(
            [this]() { this->ShowMainMenu(); },  // Back handler goes to main menu
            [this](pu::ui::Overlay::Ref overlay) { this->StartOverlay(overlay); },
            [this]() { this->EndOverlay(); },
            saveDataAccessor,  // Pass the save data accessor
            boxDataProvider,  // Pass the box data provider
            bankBoxDataProvider  // Pass the bank box data provider
        );

        // Create bag screen
        LOG_DEBUG("Creating bag screen...");
        bagScreen = pksm::layout::BagScreen::New(
            saveDataAccessor,
            [this]() { this->ShowMainMenu(); },  // Back handler goes to main menu
            [this](pu::ui::Overlay::Ref overlay) { this->StartOverlay(overlay); },
            [this]() { this->EndOverlay(); }
        );

        // Create settings screen
        LOG_DEBUG("Creating settings screen...");
        settingsScreen = pksm::layout::SettingsScreen::New(
            [this]() { this->ShowMainMenu(); },  // Back handler goes to main menu
            [this](pu::ui::Overlay::Ref overlay) { this->StartOverlay(overlay); },
            [this]() { this->EndOverlay(); },
            [this]() { this->ShowEmulatorFileSelectScreen(); }  // Emulator config callback
        );

        // Create editor screen
        LOG_DEBUG("Creating editor screen...");
        editorScreen = pksm::layout::EditorScreen::New(
            [this]() { this->ShowMainMenu(); },
            [this](pu::ui::Overlay::Ref overlay) { this->StartOverlay(overlay); },
            [this]() { this->EndOverlay(); },
            saveDataAccessor,
            boxDataProvider,
            partyDataProvider
        );

        // Register for save data changes in both MainMenu and StorageScreen
        LOG_DEBUG("Setting up save data change callbacks...");
        saveDataAccessor->setOnSaveDataChanged([this](pksm::saves::SaveData::Ref saveData) {
            LOG_DEBUG("Save data changed, updating UI");

            // Update main menu with new save data
            if (mainMenu) {
                LOG_DEBUG("Updating MainMenu with new save data");
                mainMenu->UpdateTrainerInfo();
            }

            // Preload box data for storage screen
            if (storageScreen && saveData) {
                LOG_DEBUG("Preloading box data for StorageScreen");
                storageScreen->LoadBoxData();
            }

            if (bagScreen) {
                bagScreen->RefreshCategories();
            }

            // Refresh editor screen with new save data
            if (editorScreen) {
                LOG_DEBUG("Refreshing EditorScreen with new save data");
                editorScreen->LoadData();
            }
        });

        // Start with startup screen
        LOG_DEBUG("Loading initial screen...");
        this->ShowStartupScreen();
        this->StartSpriteSyncIfNeeded();

        LOG_DEBUG("Application loaded successfully");
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to load application: " + std::string(e.what()));
        throw;
    }
}

}  // namespace pksm