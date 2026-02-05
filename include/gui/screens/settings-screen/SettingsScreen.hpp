#pragma once

#include <functional>
#include <vector>
#include <pu/Plutonium>

#include "gui/shared/components/AnimatedBackground.hpp"
#include "gui/shared/components/BaseLayout.hpp"
#include "gui/shared/components/FocusableButton.hpp"
#include "gui/shared/interfaces/IHelpProvider.hpp"
#include "input/ButtonInputHandler.hpp"
#include "input/visual-feedback/FocusManager.hpp"

namespace pksm::layout {

class SettingsScreen : public BaseLayout {
public:
    SettingsScreen(
        std::function<void()> onBack,
        std::function<void(pu::ui::Overlay::Ref)> onShowOverlay,
        std::function<void()> onHideOverlay
    );
    PU_SMART_CTOR(SettingsScreen)
    ~SettingsScreen();

private:
    // UI Elements
    pu::ui::elm::Element::Ref background;
    pu::ui::Color bgColor = pu::ui::Color(39, 66, 164, 255);
    std::function<void()> onBack;
    
    // Header
    pu::ui::elm::TextBlock::Ref headerText;
    
    // Settings sections
    pu::ui::elm::TextBlock::Ref generalHeader;
    pu::ui::elm::TextBlock::Ref appearanceHeader;
    pu::ui::elm::TextBlock::Ref advancedHeader;
    
    // Settings items
    std::vector<pksm::ui::FocusableButton::Ref> settingButtons;
    
    // Input handling
    pksm::input::ButtonInputHandler buttonHandler;
    pksm::input::FocusManager::Ref focusManager;

    // Layout constants
    static constexpr pu::i32 HEADER_TOP_MARGIN = 60;
    static constexpr pu::i32 CONTENT_TOP_MARGIN = 140;
    static constexpr pu::i32 SIDE_MARGIN = 70;
    static constexpr pu::i32 BUTTON_HEIGHT = 80;
    static constexpr pu::i32 BUTTON_SPACING = 20;
    static constexpr pu::i32 SECTION_SPACING = 40;
    static constexpr pu::i32 BUTTON_WIDTH = 1140;

    // Methods
    void OnInput(u64 down, u64 up, u64 held);
    void CreateSettingButton(
        const std::string& label,
        const std::string& value,
        pu::i32 y,
        std::function<void()> onClick
    );
    void CreateSectionHeader(const std::string& text, pu::i32 y);

    // Override BaseLayout methods
    std::vector<pksm::ui::HelpItem> GetHelpOverlayItems() const override;
    void OnHelpOverlayShown() override;
    void OnHelpOverlayHidden() override;
};

}  // namespace pksm::layout
