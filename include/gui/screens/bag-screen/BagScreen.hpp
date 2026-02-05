#pragma once

#include <functional>
#include <pu/Plutonium>

#include "gui/shared/components/AnimatedBackground.hpp"
#include "gui/shared/components/BaseLayout.hpp"
#include "gui/shared/components/FocusableButton.hpp"
#include "input/ButtonInputHandler.hpp"
#include "input/visual-feedback/FocusManager.hpp"

namespace pksm::layout {

class BagScreen : public BaseLayout {
public:
    BagScreen(
        std::function<void()> onBack,
        std::function<void(pu::ui::Overlay::Ref)> onShowOverlay,
        std::function<void()> onHideOverlay
    );
    PU_SMART_CTOR(BagScreen)
    ~BagScreen();

private:
    pu::ui::elm::Element::Ref background;
    pu::ui::Color bgColor = pu::ui::Color(180, 140, 0, 255);  // yellow/gold theme for bag screen
    std::function<void()> onBack;

    // UI Elements
    pu::ui::elm::TextBlock::Ref headerText;
    pu::ui::elm::TextBlock::Ref categoryHeaderText;
    pu::ui::elm::TextBlock::Ref itemNameText;
    pu::ui::elm::TextBlock::Ref itemQuantityText;
    
    // Bag category buttons
    std::vector<pksm::ui::FocusableButton::Ref> categoryButtons;
    
    // Item control buttons
    pksm::ui::FocusableButton::Ref decreaseButton;
    pksm::ui::FocusableButton::Ref increaseButton;
    pksm::ui::FocusableButton::Ref backButton;

    // Focus management
    pksm::input::FocusManager::Ref bagScreenFocusManager;

    // Input handlers
    pksm::input::ButtonInputHandler buttonHandler;

    // Current state
    int currentCategory = -1;
    int currentItemQuantity = 99; // default quantity

    // Layout constants
    static constexpr pu::i32 HEADER_TOP_MARGIN = 60;
    static constexpr pu::i32 CATEGORY_TOP_MARGIN = 140;
    static constexpr pu::i32 SIDE_MARGIN = 70;
    static constexpr pu::i32 BUTTON_HEIGHT = 80;
    static constexpr pu::i32 BUTTON_SPACING = 20;
    static constexpr pu::i32 BUTTON_WIDTH = 1140;
    static constexpr pu::i32 ITEM_CONTROL_TOP_MARGIN = 400;

    // Methods
    void OnInput(u64 down, u64 up, u64 held);
    void CreateCategoryButtons();
    void CreateItemControls();
    void ShowCategory(int categoryIndex);
    void UpdateItemDisplay();

    // Override BaseLayout methods
    std::vector<pksm::ui::HelpItem> GetHelpOverlayItems() const override;
    void OnHelpOverlayShown() override;
    void OnHelpOverlayHidden() override;
};

}  // namespace pksm::layout
