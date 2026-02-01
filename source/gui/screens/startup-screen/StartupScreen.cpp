#include "gui/screens/startup-screen/StartupScreen.hpp"
#include <algorithm>
#include <thread>
#include "gui/shared/UIConstants.hpp"

namespace pksm::layout {

StartupScreen::StartupScreen(
    std::function<void()> onTimeout,
    std::function<void(pu::ui::Overlay::Ref)> onShowOverlay,
    std::function<void()> onHideOverlay
) : BaseLayout(onShowOverlay, onHideOverlay), onTimeout(onTimeout), completed(false) {
    
    this->SetBackgroundColor(pu::ui::Color(20, 20, 30, 255));

    // Create PKSM logo (using FocusableImage like other screens)
    logoImage = pksm::ui::FocusableImage::New((1280 - LOGO_SIZE) / 2, LOGO_Y, nullptr, LOGO_SIZE, 0);
    try {
        pu::sdl2::Texture logoTexture = pu::ui::render::LoadImage("romfs:/gfx/logo/pksm_logo.png");
        if (logoTexture) {
            auto logoHandle = pu::sdl2::TextureHandle::New(logoTexture);
            logoImage->SetImage(logoHandle);
        }
    } catch (...) {
        logoImage = nullptr;
    }

    // Create title text (following the same pattern as other screens)
    titleText = pu::ui::elm::TextBlock::New(0, TEXT_Y, "PKSM - Pokémon Save Manager");
    titleText->SetColor(pksm::ui::global::TEXT_WHITE);
    titleText->SetFont(pksm::ui::global::MakeHeavyFontName(pksm::ui::global::FONT_SIZE_TITLE));

    // Create loading text
    loadingText = pu::ui::elm::TextBlock::New(0, TEXT_Y + 40, "Loading...");
    loadingText->SetColor(pksm::ui::global::TEXT_WHITE);
    loadingText->SetFont(pksm::ui::global::MakeMediumFontName(pksm::ui::global::FONT_SIZE_ACCOUNT_NAME));

    // Create loading bar background
    loadingBarBackground = pu::ui::elm::Rectangle::New(
        LOADING_BAR_X, LOADING_BAR_Y, LOADING_BAR_WIDTH, LOADING_BAR_HEIGHT,
        pu::ui::Color(60, 60, 80, 255)
    );

    // Create loading bar fill (will be animated)
    loadingBarFill = pu::ui::elm::Rectangle::New(
        LOADING_BAR_X, LOADING_BAR_Y, 0, LOADING_BAR_HEIGHT,
        pu::ui::Color(0, 150, 255, 255)
    );

    // Add elements to layout
    if (logoImage) {
        this->Add(logoImage);
    }
    this->Add(titleText);
    this->Add(loadingText);
    this->Add(loadingBarBackground);
    this->Add(loadingBarFill);

    // Set up input handling for the layout
    this->SetOnInput(
        std::bind(&StartupScreen::OnInput, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)
    );

    // Start the timer and background thread
    startTime = std::chrono::steady_clock::now();
    timeoutThread = std::thread(&StartupScreen::TimeoutWorker, this);
}

StartupScreen::~StartupScreen() {
    // Ensure the thread is properly joined
    if (timeoutThread.joinable()) {
        completed = true;  // Signal thread to exit
        timeoutThread.join();
    }
}

void StartupScreen::TimeoutWorker() {
    std::this_thread::sleep_for(std::chrono::milliseconds(DISPLAY_DURATION_MS));
    
    // Check if we haven't already completed
    if (!completed.exchange(true)) {
        // Call the timeout callback on the main thread
        if (onTimeout) {
            onTimeout();
        }
    }
}

void StartupScreen::OnHelpOverlayShown() {
    // Disable UI elements when help overlay is shown
    if (logoImage) logoImage->SetVisible(false);
    titleText->SetVisible(false);
    loadingText->SetVisible(false);
    loadingBarBackground->SetVisible(false);
    loadingBarFill->SetVisible(false);
}

void StartupScreen::OnHelpOverlayHidden() {
    // Re-enable UI elements when help overlay is hidden
    if (logoImage) logoImage->SetVisible(true);
    titleText->SetVisible(true);
    loadingText->SetVisible(true);
    loadingBarBackground->SetVisible(true);
    loadingBarFill->SetVisible(true);
}

void StartupScreen::OnInput(u64 down, u64 up, u64 held) {
    UpdateLoadingAnimation();
    
    // User can skip the startup screen by pressing A
    if (down & HidNpadButton_A && !completed.exchange(true)) {
        if (onTimeout) {
            onTimeout();
        }
    }
}

void StartupScreen::UpdateLoadingAnimation() {
    if (completed) return;

    auto currentTime = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - startTime).count();
    
    // Update loading bar animation
    float progress = std::min(static_cast<float>(elapsed) / DISPLAY_DURATION_MS, 1.0f);
    loadingBarFill->SetWidth(static_cast<u32>(LOADING_BAR_WIDTH * progress));
}

}  // namespace pksm::layout
