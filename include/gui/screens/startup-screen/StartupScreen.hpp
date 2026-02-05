#pragma once

#include <memory>
#include <pu/Plutonium>
#include <chrono>
#include <thread>
#include <atomic>

#include "gui/shared/components/BaseLayout.hpp"
#include "gui/shared/components/FocusableImage.hpp"

namespace pksm::layout {

class StartupScreen : public BaseLayout {
private:
    static constexpr u32 DISPLAY_DURATION_MS = 5000;  // 5 seconds
    static constexpr u32 LOGO_SIZE = 256;
    static constexpr u32 LOGO_Y = 120;
    static constexpr u32 TEXT_Y = LOGO_Y + LOGO_SIZE + 60;
    static constexpr u32 LOADING_TEXT_Y = TEXT_Y + 50;
    static constexpr u32 LOADING_BAR_Y = LOADING_TEXT_Y + 60;
    static constexpr u32 LOADING_BAR_WIDTH = 600;
    static constexpr u32 LOADING_BAR_HEIGHT = 8;
    static constexpr u32 LOADING_BAR_X = (1280 - LOADING_BAR_WIDTH) / 2;

    // UI Elements
    pksm::ui::FocusableImage::Ref logoImage;
    pu::ui::elm::TextBlock::Ref titleText;
    pu::ui::elm::TextBlock::Ref loadingText;
    pu::ui::elm::Rectangle::Ref loadingBarBackground;
    pu::ui::elm::Rectangle::Ref loadingBarFill;

    // Timing
    std::chrono::steady_clock::time_point startTime;
    std::function<void()> onTimeout;
    std::atomic<bool> completed;
    std::thread timeoutThread;

    // Background thread function
    void TimeoutWorker();

public:
    StartupScreen(
        std::function<void()> onTimeout,
        std::function<void(pu::ui::Overlay::Ref)> onShowOverlay = nullptr,
        std::function<void()> onHideOverlay = nullptr
    );
    PU_SMART_CTOR(StartupScreen)

    // Destructor to clean up thread
    ~StartupScreen();

    // BaseLayout overrides
    void OnHelpOverlayShown() override;
    void OnHelpOverlayHidden() override;

    // Input handling method (not override)
    void OnInput(u64 down, u64 up, u64 held);

    // Update loading animation (called from main loop)
    void UpdateLoadingAnimation();
};

}  // namespace pksm::layout
