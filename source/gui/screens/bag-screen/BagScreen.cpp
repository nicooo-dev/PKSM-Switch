#include "gui/screens/bag-screen/BagScreen.hpp"
#include "gui/shared/UIConstants.hpp"
#include "gui/shared/components/AnimatedBackground.hpp"
#include "gui/shared/interfaces/IHelpProvider.hpp"
#include "utils/Logger.hpp"

namespace pksm::layout {

BagScreen::BagScreen(
    std::function<void()> onBack,
    std::function<void(pu::ui::Overlay::Ref)> onShowOverlay,
    std::function<void()> onHideOverlay
)
  : BaseLayout(onShowOverlay, onHideOverlay),
    onBack(onBack),
    buttonHandler() {
    
    LOG_DEBUG("Initializing BagScreen...");

    this->SetBackgroundColor(bgColor);
    background = ui::AnimatedBackground::New();
    this->Add(background);

    headerText = pu::ui::elm::TextBlock::New(SIDE_MARGIN, HEADER_TOP_MARGIN, "Bag");
    headerText->SetColor(pksm::ui::global::TEXT_WHITE);
    headerText->SetFont(pksm::ui::global::MakeHeavyFontName(pksm::ui::global::FONT_SIZE_TITLE));
    this->Add(headerText);

    bagScreenFocusManager = pksm::input::FocusManager::New("BagScreen Manager");
    bagScreenFocusManager->SetActive(true);

    CreateCategoryButtons();

    CreateItemControls();

    for (auto& button : categoryButtons) {
        bagScreenFocusManager->RegisterFocusable(button);
    }

    if (!categoryButtons.empty()) {
        categoryButtons[0]->RequestFocus();
    }

    InitializeHelpFooter();

    buttonHandler.RegisterButton(HidNpadButton_B, nullptr, [this]() {
        LOG_DEBUG("B button pressed, returning to main menu");
        if (this->onBack) {
            this->onBack();
        }
    });

    this->SetOnInput(
        std::bind(&BagScreen::OnInput, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)
    );

    LOG_DEBUG("BagScreen initialization complete");
    PreRender();
}

BagScreen::~BagScreen() {
    LOG_DEBUG("BagScreen destructor");
}

void BagScreen::OnInput(u64 down, u64 up, u64 held) {
    if (HandleHelpInput(down)) {
        return;
    }

    buttonHandler.HandleInput(down, up, held);

    if (down & HidNpadButton_Down) {
        auto currentFocused = bagScreenFocusManager->GetFocusedElement();
        if (currentFocused) {
            if (currentCategory == -1) {
                for (size_t i = 0; i < categoryButtons.size(); i++) {
                    if (categoryButtons[i] == currentFocused && i + 1 < categoryButtons.size()) {
                        categoryButtons[i + 1]->RequestFocus();
                        break;
                    }
                }
            } else {
                if (currentFocused == decreaseButton) {
                    increaseButton->RequestFocus();
                } else if (currentFocused == increaseButton) {
                    backButton->RequestFocus();
                }
            }
        }
    } else if (down & HidNpadButton_Up) {
        auto currentFocused = bagScreenFocusManager->GetFocusedElement();
        if (currentFocused) {
            if (currentCategory == -1) {
                for (size_t i = 0; i < categoryButtons.size(); i++) {
                    if (categoryButtons[i] == currentFocused && i > 0) {
                        categoryButtons[i - 1]->RequestFocus();
                        break;
                    }
                }
            } else {
                if (currentFocused == backButton) {
                    increaseButton->RequestFocus();
                } else if (currentFocused == increaseButton) {
                    decreaseButton->RequestFocus();
                }
            }
        }
    }

    auto focused = bagScreenFocusManager->GetFocusedElement();
    if (focused && (down & HidNpadButton_A)) {
        if (currentCategory == -1) {
            for (auto& button : categoryButtons) {
                if (button == focused) {
                    button->OnInput(down, up, held, pu::ui::TouchPoint());
                    break;
                }
            }
        } else {
            if (focused == decreaseButton) {
                decreaseButton->OnInput(down, up, held, pu::ui::TouchPoint());
            } else if (focused == increaseButton) {
                increaseButton->OnInput(down, up, held, pu::ui::TouchPoint());
            } else if (focused == backButton) {
                backButton->OnInput(down, up, held, pu::ui::TouchPoint());
            }
        }
    }
}

std::vector<pksm::ui::HelpItem> BagScreen::GetHelpOverlayItems() const {
    return {
        {{{pksm::ui::global::ButtonGlyph::A}}, "Select"},
        {{{pksm::ui::global::ButtonGlyph::B}}, "Back to Main Menu"},
        {{{pksm::ui::global::ButtonGlyph::DPad}}, "Navigate"},
        {{{pksm::ui::global::ButtonGlyph::Minus}}, "Help"}
    };
}

void BagScreen::OnHelpOverlayShown() {
    LOG_DEBUG("Help overlay shown, disabling UI elements");
    if (currentCategory == -1) {
        for (auto& button : categoryButtons) {
            button->SetDisabled(true);
        }
    } else {
        decreaseButton->SetDisabled(true);
        increaseButton->SetDisabled(true);
        backButton->SetDisabled(true);
    }
}

void BagScreen::OnHelpOverlayHidden() {
    LOG_DEBUG("Help overlay hidden, re-enabling UI elements");
    if (currentCategory == -1) {
        for (auto& button : categoryButtons) {
            button->SetDisabled(false);
        }
    } else {
        decreaseButton->SetDisabled(false);
        increaseButton->SetDisabled(false);
        backButton->SetDisabled(false);
    }
}

void BagScreen::CreateCategoryButtons() {
    const std::vector<std::string> categories = {
        "Items",
        "Key Items", 
        "TMs",
        "Medicine",
        "Berries",
        "Z-Crystals",
        "Rotom Powers"
    };

    pu::i32 currentY = CATEGORY_TOP_MARGIN;

    for (size_t i = 0; i < categories.size(); i++) {
        auto button = pksm::ui::FocusableButton::New(
            SIDE_MARGIN,
            currentY,
            BUTTON_WIDTH,
            BUTTON_HEIGHT,
            categories[i],
            pu::ui::Color(140, 110, 0, 200),  // darker gold for background
            pu::ui::Color(200, 160, 0, 255)   // gold for focused state
        );
        
        button->SetContentFont(pksm::ui::global::MakeMediumFontName(pksm::ui::global::FONT_SIZE_BUTTON));
        button->SetContentColor(pksm::ui::global::TEXT_WHITE);
        button->SetOnClick([this, i]() {
            LOG_DEBUG("Category " + std::to_string(i) + " clicked");
            ShowCategory(i);
        });
        button->SetHelpText("Open " + categories[i] + " category");
        
        this->Add(button);
        categoryButtons.push_back(button);
        currentY += BUTTON_HEIGHT + BUTTON_SPACING;
    }
}

void BagScreen::CreateItemControls() {
    categoryHeaderText = pu::ui::elm::TextBlock::New(
        SIDE_MARGIN, 
        CATEGORY_TOP_MARGIN, 
        ""
    );
    categoryHeaderText->SetColor(pksm::ui::global::TEXT_WHITE);
    categoryHeaderText->SetFont(pksm::ui::global::MakeMediumFontName(pksm::ui::global::FONT_SIZE_HEADER));
    categoryHeaderText->SetVisible(false);
    this->Add(categoryHeaderText);

    itemNameText = pu::ui::elm::TextBlock::New(
        SIDE_MARGIN,
        ITEM_CONTROL_TOP_MARGIN,
        "Sample Item"
    );
    itemNameText->SetColor(pksm::ui::global::TEXT_WHITE);
    itemNameText->SetFont(pksm::ui::global::MakeMediumFontName(pksm::ui::global::FONT_SIZE_BUTTON));
    itemNameText->SetVisible(false);
    this->Add(itemNameText);

    decreaseButton = pksm::ui::FocusableButton::New(
        SIDE_MARGIN,
        ITEM_CONTROL_TOP_MARGIN + 60,
        100,
        BUTTON_HEIGHT,
        "-",
        pu::ui::Color(140, 110, 0, 200),
        pu::ui::Color(200, 160, 0, 255)
    );
    decreaseButton->SetContentFont(pksm::ui::global::MakeMediumFontName(pksm::ui::global::FONT_SIZE_BUTTON));
    decreaseButton->SetContentColor(pksm::ui::global::TEXT_WHITE);
    decreaseButton->SetOnClick([this]() {
        if (currentItemQuantity > 0) {
            currentItemQuantity--;
            UpdateItemDisplay();
        }
    });
    decreaseButton->SetVisible(false);
    this->Add(decreaseButton);

    itemQuantityText = pu::ui::elm::TextBlock::New(
        SIDE_MARGIN + 120,
        ITEM_CONTROL_TOP_MARGIN + 85,
        "x10"
    );
    itemQuantityText->SetColor(pksm::ui::global::TEXT_WHITE);
    itemQuantityText->SetFont(pksm::ui::global::MakeHeavyFontName(pksm::ui::global::FONT_SIZE_BUTTON));
    itemQuantityText->SetVisible(false);
    this->Add(itemQuantityText);

    increaseButton = pksm::ui::FocusableButton::New(
        SIDE_MARGIN + 200,
        ITEM_CONTROL_TOP_MARGIN + 60,
        100,
        BUTTON_HEIGHT,
        "+",
        pu::ui::Color(140, 110, 0, 200),
        pu::ui::Color(200, 160, 0, 255)
    );
    increaseButton->SetContentFont(pksm::ui::global::MakeMediumFontName(pksm::ui::global::FONT_SIZE_BUTTON));
    increaseButton->SetContentColor(pksm::ui::global::TEXT_WHITE);
    increaseButton->SetOnClick([this]() {
        currentItemQuantity++;
        UpdateItemDisplay();
    });
    increaseButton->SetVisible(false);
    this->Add(increaseButton);

    backButton = pksm::ui::FocusableButton::New(
        SIDE_MARGIN,
        ITEM_CONTROL_TOP_MARGIN + 160,
        BUTTON_WIDTH,
        BUTTON_HEIGHT,
        "Back to Categories",
        pu::ui::Color(140, 110, 0, 200),
        pu::ui::Color(200, 160, 0, 255)
    );
    backButton->SetContentFont(pksm::ui::global::MakeMediumFontName(pksm::ui::global::FONT_SIZE_BUTTON));
    backButton->SetContentColor(pksm::ui::global::TEXT_WHITE);
    backButton->SetOnClick([this]() {
        ShowCategory(-1);
    });
    backButton->SetVisible(false);
    this->Add(backButton);

    bagScreenFocusManager->RegisterFocusable(decreaseButton);
    bagScreenFocusManager->RegisterFocusable(increaseButton);
    bagScreenFocusManager->RegisterFocusable(backButton);
}

void BagScreen::ShowCategory(int categoryIndex) {
    currentCategory = categoryIndex;
    
    const std::vector<std::string> categoryNames = {
        "Items",
        "Key Items", 
        "TMs",
        "Medicine",
        "Berries",
        "Z-Crystals",
        "Rotom Powers"
    };

    if (categoryIndex >= 0 && categoryIndex < categoryNames.size()) {
        for (auto& button : categoryButtons) {
            button->SetVisible(false);
        }
        
        categoryHeaderText->SetText(categoryNames[categoryIndex]);
        categoryHeaderText->SetVisible(true);
        
        itemNameText->SetText("Sample " + categoryNames[categoryIndex].substr(0, categoryNames[categoryIndex].length() - 1));  // Remove 's' for singular
        itemNameText->SetVisible(true);
        
        decreaseButton->SetVisible(true);
        increaseButton->SetVisible(true);
        itemQuantityText->SetVisible(true);
        backButton->SetVisible(true);
        
        UpdateItemDisplay();
        
        decreaseButton->RequestFocus();
        
        std::vector<pksm::ui::HelpItem> helpItems = {
            {{{pksm::ui::global::ButtonGlyph::A}}, "Adjust Quantity"},
            {{{pksm::ui::global::ButtonGlyph::B}}, "Back to Main Menu"},
            {{{pksm::ui::global::ButtonGlyph::DPad}}, "Navigate Controls"},
        };
        helpFooter->SetHelpItems(helpItems);
    } else {
        categoryHeaderText->SetVisible(false);
        itemNameText->SetVisible(false);
        decreaseButton->SetVisible(false);
        increaseButton->SetVisible(false);
        itemQuantityText->SetVisible(false);
        backButton->SetVisible(false);
        
        for (auto& button : categoryButtons) {
            button->SetVisible(true);
        }
        
        if (!categoryButtons.empty()) {
            categoryButtons[0]->RequestFocus();
        }
        
        std::vector<pksm::ui::HelpItem> helpItems = {
            {{{pksm::ui::global::ButtonGlyph::A}}, "Select Category"},
            {{{pksm::ui::global::ButtonGlyph::B}}, "Back to Main Menu"},
            {{{pksm::ui::global::ButtonGlyph::DPad}}, "Navigate Categories"},
        };
        helpFooter->SetHelpItems(helpItems);
    }
}

void BagScreen::UpdateItemDisplay() {
    itemQuantityText->SetText("x" + std::to_string(currentItemQuantity));
}

}  // namespace pksm::layout
