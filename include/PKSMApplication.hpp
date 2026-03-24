#pragma once

#include <atomic>
#include <memory>
#include <thread>
#include <pu/Plutonium>
#include <switch.h>

#include "data/AccountManager.hpp"
#include "data/providers/interfaces/IBoxDataProvider.hpp"
#include "data/providers/interfaces/IPartyDataProvider.hpp"
#include "gui/screens/bag-screen/BagScreen.hpp"
#include "gui/screens/editor-screen/EditorScreen.hpp"
#include "gui/screens/main-menu/MainMenu.hpp"
#include "gui/screens/settings-screen/SettingsScreen.hpp"
#include "gui/screens/startup-screen/StartupScreen.hpp"
#include "gui/screens/storage-screen/StorageScreen.hpp"
#include "gui/screens/title-load-screen/TitleLoadScreen.hpp"
#include "gui/screens/emulator-fileselect-screen/EmulatorFileSelectScreen.hpp"
#include "gui/shared/components/NotificationCenter.hpp"

namespace pksm
{

    class PKSMApplication : public pu::ui::Application
    {
    private:
        // Screens
        pksm::layout::BagScreen::Ref bagScreen;
        pksm::layout::StartupScreen::Ref startupScreen;
        pksm::layout::MainMenu::Ref mainMenu;
        pksm::layout::EditorScreen::Ref editorScreen;
        pksm::layout::SettingsScreen::Ref settingsScreen;
        pksm::layout::TitleLoadScreen::Ref titleLoadScreen;
        pksm::layout::StorageScreen::Ref storageScreen;
        pksm::layout::EmulatorFileSelectScreen::Ref emulatorFileSelectScreen;

        // Data providers and managers
        std::unique_ptr<pksm::data::AccountManager> accountManager;
        ITitleDataProvider::Ref titleProvider;
        ISaveDataProvider::Ref saveProvider;
        ISaveDataAccessor::Ref saveDataAccessor;
        IBoxDataProvider::Ref boxDataProvider;
        IBoxDataProvider::Ref bankBoxDataProvider;
        IPartyDataProvider::Ref partyDataProvider;
        pksm::titles::Title::Ref lastLoadedTitle;
        std::string lastLoadedSavePath;

        // Startup sprite sync state
        std::atomic<bool> spriteSyncRunning{false};
        std::atomic<bool> spriteSyncDone{false};
        std::atomic<bool> spriteSyncUiCompleted{false};
        std::atomic<std::size_t> spriteSyncTotal{0};
        std::atomic<std::size_t> spriteSyncProcessed{0};
        std::atomic<std::size_t> spriteSyncDownloaded{0};
        std::atomic<std::size_t> spriteSyncFailed{0};
        std::atomic<std::size_t> spriteSyncRemaining{0};
        std::atomic<int> spriteSyncPhase{0};
        std::atomic<std::size_t> spriteSyncStageCurrent{0};
        std::atomic<std::size_t> spriteSyncStageTotal{0};
        std::atomic<bool> spriteSyncUsedZip{false};
        std::atomic<bool> spriteSyncUsedFallback{false};
        std::atomic<std::size_t> spriteSyncNoProgressBatches{0};
        std::atomic<bool> spriteSyncStopRequested{false};
        std::thread spriteSyncWorker;

        // global toast notifications (drawn above everything)
        pksm::ui::NotificationCenter::Ref notificationCenter;

        // Initialize renderer options with basic configuration
        static pu::ui::render::RendererInitOptions CreateRendererOptions();

        // Configure font settings
        static void ConfigureFonts(pu::ui::render::RendererInitOptions &renderer_opts);

        // Configure input settings
        static void ConfigureInput(pu::ui::render::RendererInitOptions &renderer_opts);

        // Register additional fonts that require romfs to be mounted
        static void RegisterAdditionalFonts();

        // Navigation methods
        void ShowBagScreen();
        void ShowStartupScreen();
        void ShowMainMenu();
        void ShowEditorScreen();
        void ShowSettingsScreen();
        void ShowTitleLoadScreen();
        void ShowStorageScreen();
        void ShowEmulatorFileSelectScreen();

        // Save handling
        void OnSaveSelected(pksm::titles::Title::Ref title, pksm::saves::Save::Ref save);

        // Startup sprite sync workflow
        void StartSpriteSyncIfNeeded();
        void UpdateStartupSyncUI();

    public:
        PKSMApplication(
            pu::ui::render::Renderer::Ref renderer,
            std::unique_ptr<data::AccountManager> accountManager,
            ITitleDataProvider::Ref titleProvider,
            ISaveDataProvider::Ref saveProvider,
            ISaveDataAccessor::Ref saveDataAccessor,
            IBoxDataProvider::Ref boxDataProvider,
            IBoxDataProvider::Ref bankBoxDataProvider,
            IPartyDataProvider::Ref partyDataProvider);
        ~PKSMApplication();
        PU_SMART_CTOR(PKSMApplication)

        // Initialize the application with all necessary configuration
        static PKSMApplication::Ref Initialize();

        void OnLoad() override;
    };

} // namespace pksm