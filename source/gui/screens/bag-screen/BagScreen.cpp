#include "gui/screens/bag-screen/BagScreen.hpp"
#include "gui/shared/UIConstants.hpp"
#include "gui/shared/components/AnimatedBackground.hpp"
#include "gui/shared/interfaces/IHelpProvider.hpp"
#include "utils/Logger.hpp"

#include "data/saves/SaveData.hpp"
#include "pksmcore/enums/Language.hpp"
#include "pksmcore/utils/i18n.hpp"

namespace pksm::layout {

BagScreen::BagScreen(
    ISaveDataAccessor::Ref saveDataAccessor,
    std::function<void()> onBack,
    std::function<void(pu::ui::Overlay::Ref)> onShowOverlay,
    std::function<void()> onHideOverlay
)
  : BaseLayout(onShowOverlay, onHideOverlay),
    saveDataAccessor(std::move(saveDataAccessor)),
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

void BagScreen::RefreshCategories() {
    BuildCategoriesForCurrentSave();

    const size_t visible_count = categoryLabels.size();
    for (size_t i = 0; i < categoryButtons.size(); i++) {
        if (i < visible_count) {
            categoryButtons[i]->SetContent(categoryLabels[i]);
            categoryButtons[i]->SetHelpText("Open " + categoryLabels[i] + " category");
            categoryButtons[i]->SetVisible(true);
        } else {
            categoryButtons[i]->SetVisible(false);
        }
    }

    if (currentCategory == -1) {
        for (auto &btn : categoryButtons) {
            if (btn->IsVisible()) {
                btn->RequestFocus();
                break;
            }
        }
    }
}

void BagScreen::OnInput(u64 down, u64 up, u64 held) {
    if (HandleHelpInput(down)) {
        return;
    }

    buttonHandler.HandleInput(down, up, held);

    if ((currentCategory != -1) && itemList && itemList->IsFocused()) {
        itemList->OnInput(down, up, held, pu::ui::TouchPoint());
        UpdateItemDisplay();
        return;
    }

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

void BagScreen::BuildCategoriesForCurrentSave() {
    categoryLabels.clear();
    categoryPouches.clear();

    auto version = pksm::saves::GameVersion::RD;
    if (this->saveDataAccessor) {
        auto saveData = this->saveDataAccessor->getCurrentSaveData();
        if (saveData) {
            version = saveData->getVersion();
        }
    }

    const bool is_swsh = (version == pksm::saves::GameVersion::SW) || (version == pksm::saves::GameVersion::SH);
    const bool is_lgpe = (version == pksm::saves::GameVersion::GP) || (version == pksm::saves::GameVersion::GE);

    if (is_lgpe) {
        categoryLabels = {
            "Medicine Pocket",
            "TM Case",
            "Candy Jar",
            "Power-Up Pocket",
            "Catching Pocket",
            "Battle Pocket",
            "Bag",
            "Unused",
        };

        categoryPouches = {
            pksm::saves::BagPouch::Medicine,
            pksm::saves::BagPouch::TM,
            pksm::saves::BagPouch::Candy,
            pksm::saves::BagPouch::ZCrystals,
            pksm::saves::BagPouch::CatchingItem,
            pksm::saves::BagPouch::Battle,
            pksm::saves::BagPouch::NormalItem,
            pksm::saves::BagPouch::Unknown,
        };
    } else if (is_swsh) {
        categoryLabels = {
            "Medicine",
            "Poke Balls",
            "Battle Items",
            "Berries",
            "Other Items",
            "TMs",
            "Treasures",
            "Ingredients",
            "Key Items",
        };

        categoryPouches = {
            pksm::saves::BagPouch::Medicine,
            pksm::saves::BagPouch::Ball,
            pksm::saves::BagPouch::Battle,
            pksm::saves::BagPouch::Berry,
            pksm::saves::BagPouch::NormalItem,
            pksm::saves::BagPouch::TM,
            pksm::saves::BagPouch::Treasure,
            pksm::saves::BagPouch::Ingredient,
            pksm::saves::BagPouch::KeyItem,
        };
    } else {
        categoryLabels = {
            "Items",
            "Key Items",
            "TMs",
            "Medicine",
            "Berries",
            "Z-Crystals",
            "Rotom Powers",
        };

        categoryPouches = {
            pksm::saves::BagPouch::NormalItem,
            pksm::saves::BagPouch::KeyItem,
            pksm::saves::BagPouch::TM,
            pksm::saves::BagPouch::Medicine,
            pksm::saves::BagPouch::Berry,
            pksm::saves::BagPouch::ZCrystals,
            pksm::saves::BagPouch::RotomPower,
        };
    }
}

void BagScreen::CreateCategoryButtons() {
    BuildCategoriesForCurrentSave();

    pu::i32 currentY = CATEGORY_TOP_MARGIN;
    for (size_t i = 0; i < MAX_CATEGORY_BUTTONS; i++) {
        auto button = pksm::ui::FocusableButton::New(
            SIDE_MARGIN,
            currentY,
            BUTTON_WIDTH,
            BUTTON_HEIGHT,
            "",
            pu::ui::Color(140, 110, 0, 200),
            pu::ui::Color(200, 160, 0, 255)
        );

        button->SetContentFont(pksm::ui::global::MakeMediumFontName(pksm::ui::global::FONT_SIZE_BUTTON));
        button->SetContentColor(pksm::ui::global::TEXT_WHITE);
        button->SetOnClick([this, idx = i]() {
            LOG_DEBUG("Category " + std::to_string(idx) + " clicked");
            ShowCategory(static_cast<int>(idx));
        });

        this->Add(button);
        categoryButtons.push_back(button);
        currentY += BUTTON_HEIGHT + BUTTON_SPACING;
    }

    RefreshCategories();
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

    itemList = pksm::ui::FocusableMenu::New(
        SIDE_MARGIN,
        ITEM_CONTROL_TOP_MARGIN,
        700,
        pu::ui::Color(140, 110, 0, 200),
        pu::ui::Color(200, 160, 0, 255),
        70,
        5
    );
    itemList->SetVisible(false);
    itemList->SetOnSelectionChanged([this]() { this->UpdateItemDisplay(); });
    bagScreenFocusManager->RegisterFocusable(itemList);
    this->Add(itemList);

    decreaseButton = pksm::ui::FocusableButton::New(
        SIDE_MARGIN,
        ITEM_CONTROL_TOP_MARGIN + 400,
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
        ITEM_CONTROL_TOP_MARGIN + 425,
        "x10"
    );
    itemQuantityText->SetColor(pksm::ui::global::TEXT_WHITE);
    itemQuantityText->SetFont(pksm::ui::global::MakeHeavyFontName(pksm::ui::global::FONT_SIZE_BUTTON));
    itemQuantityText->SetVisible(false);
    this->Add(itemQuantityText);

    increaseButton = pksm::ui::FocusableButton::New(
        SIDE_MARGIN + 340,
        ITEM_CONTROL_TOP_MARGIN + 400,
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
        ITEM_CONTROL_TOP_MARGIN + 500,
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
    currentItemIndex = 0;

    if (categoryIndex >= 0 && categoryIndex < static_cast<int>(categoryLabels.size())) {
        for (auto& button : categoryButtons) {
            button->SetVisible(false);
        }
        
        categoryHeaderText->SetText(categoryLabels[categoryIndex]);
        categoryHeaderText->SetVisible(true);
        
        itemNameText->SetVisible(false);

        itemList->SetVisible(true);
        itemList->SetFocused(true);

        decreaseButton->SetVisible(true);
        increaseButton->SetVisible(true);
        itemQuantityText->SetVisible(true);
        backButton->SetVisible(true);

        RefreshItemListForCurrentCategory();
        
        UpdateItemDisplay();
        
        itemList->RequestFocus();
        
        std::vector<pksm::ui::HelpItem> helpItems = {
            {{{pksm::ui::global::ButtonGlyph::A}}, "Adjust Quantity"},
            {{{pksm::ui::global::ButtonGlyph::B}}, "Back to Main Menu"},
            {{{pksm::ui::global::ButtonGlyph::DPad}}, "Navigate Controls"},
        };
        helpFooter->SetHelpItems(helpItems);
    } else {
        categoryHeaderText->SetVisible(false);
        itemNameText->SetVisible(false);
        itemList->SetVisible(false);
        itemList->SetFocused(false);
        decreaseButton->SetVisible(false);
        increaseButton->SetVisible(false);
        itemQuantityText->SetVisible(false);
        backButton->SetVisible(false);
        
        for (auto& button : categoryButtons) {
            button->SetVisible(true);
        }

        RefreshCategories();
        
        std::vector<pksm::ui::HelpItem> helpItems = {
            {{{pksm::ui::global::ButtonGlyph::A}}, "Select Category"},
            {{{pksm::ui::global::ButtonGlyph::B}}, "Back to Main Menu"},
            {{{pksm::ui::global::ButtonGlyph::DPad}}, "Navigate Categories"},
        };
        helpFooter->SetHelpItems(helpItems);
    }
}

void BagScreen::RefreshItemListForCurrentCategory() {
    currentItemMap.clear();

    if (!itemList) {
        return;
    }

    if (!this->saveDataAccessor) {
        itemList->SetDataSource({"No save loaded"});
        return;
    }

    auto saveData = this->saveDataAccessor->getCurrentSaveData();
    if (!saveData) {
        itemList->SetDataSource({"No save loaded"});
        return;
    }

    pksm::saves::BagPouch pouch = pksm::saves::BagPouch::Unknown;
    if ((this->currentCategory >= 0) && (static_cast<size_t>(this->currentCategory) < categoryPouches.size())) {
        pouch = categoryPouches.at(static_cast<size_t>(this->currentCategory));
    }

    std::vector<std::string> names;
    const auto &items = saveData->getBagItems();
    for (size_t i = 0; i < items.size(); i++) {
        const auto &it = items[i];
        if (it.pouch != pouch) {
            continue;
        }

        auto name = i18n::item(pksm::Language::ENG, it.itemId);
        if (name.empty()) {
            name = "Item #" + std::to_string(it.itemId);
        }
        names.push_back(name);
        currentItemMap.push_back(i);
    }

    if (names.empty()) {
        itemList->SetDataSource({"No items"});
        currentItemMap.clear();
    } else {
        itemList->SetDataSource(names);
    }
}

void BagScreen::UpdateItemDisplay() {
    using BP = pksm::saves::BagPouch;

    if (!this->saveDataAccessor) {
        itemNameText->SetText("No save loaded");
        itemQuantityText->SetText("x0");
        return;
    }

    auto saveData = this->saveDataAccessor->getCurrentSaveData();
    if (!saveData) {
        itemNameText->SetText("No save loaded");
        itemQuantityText->SetText("x0");
        return;
    }

    const auto &items = saveData->getBagItems();

    if (!itemList || currentItemMap.empty()) {
        currentItemQuantity = 0;
        itemQuantityText->SetText("x0");
        return;
    }

    const auto sel = itemList->GetSelectedIndex();
    if ((sel < 0) || (static_cast<size_t>(sel) >= currentItemMap.size())) {
        currentItemQuantity = 0;
        itemQuantityText->SetText("x0");
        return;
    }

    const auto bag_idx = currentItemMap.at(static_cast<size_t>(sel));
    if (bag_idx >= items.size()) {
        currentItemQuantity = 0;
        itemQuantityText->SetText("x0");
        return;
    }

    const auto &cur = items.at(bag_idx);
    currentItemQuantity = cur.count;
    itemQuantityText->SetText("x" + std::to_string(currentItemQuantity));
}

}  // namespace pksm::layout
