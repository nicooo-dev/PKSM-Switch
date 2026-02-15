#pragma once

#include <memory>
#include <pu/Plutonium>
#include <switch.h>

#include "data/AccountManager.hpp"
#include "data/providers/interfaces/IBoxDataProvider.hpp"
#include "gui/screens/bag-screen/BagScreen.hpp"
#include "gui/screens/main-menu/MainMenu.hpp"
#include "gui/screens/settings-screen/SettingsScreen.hpp"
#include "gui/screens/startup-screen/StartupScreen.hpp"
#include "gui/screens/storage-screen/StorageScreen.hpp"
#include "gui/screens/title-load-screen/TitleLoadScreen.hpp"
#include "gui/shared/components/NotificationCenter.hpp"

namespace pksm {

class PKSMApplication : public pu::ui::Application {
private:
    // Screens
    pksm::layout::BagScreen::Ref bagScreen;
    pksm::layout::StartupScreen::Ref startupScreen;
    pksm::layout::MainMenu::Ref mainMenu;
    pksm::layout::SettingsScreen::Ref settingsScreen;
    pksm::layout::TitleLoadScreen::Ref titleLoadScreen;
    pksm::layout::StorageScreen::Ref storageScreen;

    // Data providers and managers
    std::unique_ptr<pksm::data::AccountManager> accountManager;
    ITitleDataProvider::Ref titleProvider;
    ISaveDataProvider::Ref saveProvider;
    ISaveDataAccessor::Ref saveDataAccessor;
    IBoxDataProvider::Ref boxDataProvider;

    // global toast notifications (drawn above everything)
    pksm::ui::NotificationCenter::Ref notificationCenter;

    // Initialize renderer options with basic configuration
    static pu::ui::render::RendererInitOptions CreateRendererOptions();

    // Configure font settings
    static void ConfigureFonts(pu::ui::render::RendererInitOptions& renderer_opts);

    // Configure input settings
    static void ConfigureInput(pu::ui::render::RendererInitOptions& renderer_opts);

    // Register additional fonts that require romfs to be mounted
    static void RegisterAdditionalFonts();

    // Navigation methods
    void ShowBagScreen();
    void ShowStartupScreen();
    void ShowMainMenu();
    void ShowSettingsScreen();
    void ShowTitleLoadScreen();
    void ShowStorageScreen();

    // Save handling
    void OnSaveSelected(pksm::titles::Title::Ref title, pksm::saves::Save::Ref save);

public:
    PKSMApplication(
        pu::ui::render::Renderer::Ref renderer,
        std::unique_ptr<data::AccountManager> accountManager,
        ITitleDataProvider::Ref titleProvider,
        ISaveDataProvider::Ref saveProvider,
        ISaveDataAccessor::Ref saveDataAccessor,
        IBoxDataProvider::Ref boxDataProvider
    );
    PU_SMART_CTOR(PKSMApplication)

    // Initialize the application with all necessary configuration
    static PKSMApplication::Ref Initialize();

    void OnLoad() override;
};

}  // namespace pksm