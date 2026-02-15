#include "gui/screens/storage-screen/StorageScreen.hpp"

#include <ctime>

#include "gui/screens/main-menu/sub-components/menu-grid/MenuButtonGrid.hpp"
#include "utils/Logger.hpp"

namespace pksm::layout {

StorageScreen::StorageScreen(
    std::function<void()> onBack,
    std::function<void(pu::ui::Overlay::Ref)> onShowOverlay,
    std::function<void()> onHideOverlay,
    ISaveDataAccessor::Ref saveDataAccessor,
    IBoxDataProvider::Ref boxDataProvider
)
  : BaseLayout(onShowOverlay, onHideOverlay),
    onBack(onBack),
    saveDataAccessor(saveDataAccessor),
    boxDataProvider(boxDataProvider),
    isSummaryOverlayVisible(false) {
    LOG_DEBUG("Initializing StorageScreen...");

    this->SetBackgroundColor(bgColor);
    background = ui::AnimatedBackground::New();
    this->Add(background);

    headerText = pu::ui::elm::TextBlock::New(SIDE_MARGIN, HEADER_TOP_MARGIN, "Storage");
    headerText->SetColor(pksm::ui::global::TEXT_WHITE);
    headerText->SetFont(pksm::ui::global::MakeHeavyFontName(pksm::ui::global::FONT_SIZE_TITLE));
    this->Add(headerText);

    // Initialize focus management
    InitializeFocusManagement();

    // Initialize BoxGrid
    InitializePokemonBoxes();

    // Initialize help footer
    InitializeHelpFooter();

    // Set up button input handler for Back button
    buttonHandler.RegisterButton(HidNpadButton_B, nullptr, [this]() {
        LOG_DEBUG("B button pressed, returning to main menu");
        if (this->onBack) {
            this->onBack();
        }
    });

    buttonHandler.RegisterButton(HidNpadButton_X, nullptr, [this]() {
        if (isSummaryOverlayVisible) {
            return;
        }

        if (!this->saveDataAccessor || !this->boxDataProvider) {
            return;
        }

        auto saveData = this->saveDataAccessor->getCurrentSaveData();
        if (!saveData) {
            return;
        }

        pksm::ui::PokemonBox::Ref targetBox;
        if (activeBox == ActiveBox::Save) {
            targetBox = pokemonSaveBox;
        } else {
            targetBox = pokemonBankBox;
        }

        if (!targetBox) {
            return;
        }

        const int boxIndex = targetBox->GetCurrentBox();
        const int slotIndex = targetBox->GetSelectedSlot();
        if (slotIndex < 0) {
            return;
        }

        const auto slotData = targetBox->GetPokemonData(boxIndex, slotIndex);
        if (slotData.isEmpty()) {
            return;
        }

        auto pk = this->boxDataProvider->GetPokemon(saveData, boxIndex, slotIndex);
        if (!pk) {
            return;
        }

        auto overlay = pksm::ui::PokemonSummaryOverlay::New(0, 0, GetWidth(), GetHeight());
        overlay->SetPokemon(std::move(pk));
        this->onShowOverlay(overlay);
        isSummaryOverlayVisible = true;

        if (pokemonBankBox) {
            pokemonBankBox->SetDisabled(true);
        }
        if (pokemonSaveBox) {
            pokemonSaveBox->SetDisabled(true);
        }
    });

    // Set initial help items
    std::vector<pksm::ui::HelpItem> helpItems = {
        {{{pksm::ui::global::ButtonGlyph::A}}, "Select"},
        {{{pksm::ui::global::ButtonGlyph::B}}, "Back to Main Menu"},
        {{{pksm::ui::global::ButtonGlyph::X}}, "Summary"},
        {{{pksm::ui::global::ButtonGlyph::L}, {pksm::ui::global::ButtonGlyph::R}}, "Switch Box"},
        {{{pksm::ui::global::ButtonGlyph::DPad}}, "Navigate Box"},
    };
    helpFooter->SetHelpItems(helpItems);

    // Set up input handling
    this->SetOnInput(
        std::bind(&StorageScreen::OnInput, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)
    );

    LOG_DEBUG("StorageScreen initialization complete");
    PreRender();
}

void StorageScreen::InitializeFocusManagement() {
    LOG_DEBUG("Initializing focus and selection management...");

    // Initialize focus managers
    storageScreenFocusManager = pksm::input::FocusManager::New("StorageScreen Manager");
    storageScreenFocusManager->SetActive(true);  // since this is the root manager
    pokemonBankBoxFocusManager = pksm::input::FocusManager::New("PokemonBankBox Manager"); // bank box focus manager
    pokemonSaveBoxFocusManager = pksm::input::FocusManager::New("PokemonSaveBox Manager"); // save box focus manager

    // Initialize selection managers
    storageScreenSelectionManager = pksm::input::SelectionManager::New("StorageScreen Manager");
    storageScreenSelectionManager->SetActive(true);  // since this is the root manager
    pokemonBankBoxSelectionManager = pksm::input::SelectionManager::New("PokemonBankBox Manager"); // bank box selection manager
    pokemonSaveBoxSelectionManager = pksm::input::SelectionManager::New("PokemonSaveBox Manager"); // save box selection manager

    storageScreenFocusManager->RegisterChildManager(pokemonBankBoxFocusManager); // register bank box focus manager
    storageScreenFocusManager->RegisterChildManager(pokemonSaveBoxFocusManager); // register save box focus manager
    storageScreenSelectionManager->RegisterChildManager(pokemonBankBoxSelectionManager); // register bank box selection manager
    storageScreenSelectionManager->RegisterChildManager(pokemonSaveBoxSelectionManager); // register save box selection manager

    // Set up directional input handlers
    pokemonBoxDirectionalHandler.SetOnMoveLeft([this]() {
        if (activeBox == ActiveBox::Save) {
            SetActiveBox(ActiveBox::Bank);
        }
    });
    pokemonBoxDirectionalHandler.SetOnMoveRight([this]() {
        if (activeBox == ActiveBox::Bank) {
            SetActiveBox(ActiveBox::Save);
        }
    });

    LOG_DEBUG("Focus and selection management initialization complete");
}

void StorageScreen::InitializePokemonBoxes() {
    LOG_DEBUG("Initializing PokemonBoxes...");

    pokemonBankBox = pksm::ui::PokemonBox::New(
        BOX_GRID_SIDE_MARGIN,
        BOX_GRID_TOP_MARGIN,
        BOX_ITEM_SIZE,
        pokemonBankBoxFocusManager,
        pokemonBankBoxSelectionManager
    );
    this->Add(pokemonBankBox);
    pokemonBankBox->SetName("PokemonBankBox Element");
    pokemonBankBox->EstablishOwningRelationship();

    pokemonSaveBox = pksm::ui::PokemonBox::New(
        SAVE_BOX_SIDE_MARGIN,
        BOX_GRID_TOP_MARGIN,
        BOX_ITEM_SIZE,
        pokemonSaveBoxFocusManager,
        pokemonSaveBoxSelectionManager
    );
    this->Add(pokemonSaveBox);
    pokemonSaveBox->SetName("PokemonSaveBox Element");
    pokemonSaveBox->EstablishOwningRelationship();

    // Initialize bank box with placeholder data
    pokemonBankBox->SetBoxCount(32);
    for (int i = 0; i < 32; i++) {
        pksm::ui::BoxData boxData;
        boxData.name = "Bank Box " + std::to_string(i + 1);
        pokemonBankBox->SetBoxData(i, boxData);
    }
    pokemonBankBox->SetCurrentBox(0);

    // Load box data for the current save into the save box
    LoadBoxData();

    pokemonBankBox->SetOnSelectionChanged([this](int boxIndex, int slotIndex) {
        LOG_DEBUG("Bank box selection changed: Box " + std::to_string(boxIndex) + ", Slot " + std::to_string(slotIndex));
    });
    pokemonSaveBox->SetOnSelectionChanged([this](int boxIndex, int slotIndex) {
        LOG_DEBUG("Save box selection changed: Box " + std::to_string(boxIndex) + ", Slot " + std::to_string(slotIndex));
    });

    SetActiveBox(ActiveBox::Save);

    LOG_DEBUG("PokemonBoxes initialization complete");
}

void StorageScreen::LoadBoxData() {
    LOG_DEBUG("Loading box data from provider...");

    auto currentSave = saveDataAccessor->getCurrentSaveData();
    if (!currentSave) {
        LOG_DEBUG("No save data available, using fallback box data");
        // Set a default box count if no save data available
        pokemonSaveBox->SetBoxCount(1);
        // Start at box 0
        pokemonSaveBox->SetCurrentBox(0);
        LOG_DEBUG("Fallback box data loaded");
        return;
    }

    // Get box count from the provider
    size_t boxCount = boxDataProvider->GetBoxCount(currentSave);
    LOG_DEBUG("Setting box count to " + std::to_string(boxCount));
    pokemonSaveBox->SetBoxCount(boxCount);

    // Load all boxes at once to ensure the box data provider knows about them
    for (size_t i = 0; i < boxCount; ++i) {
        auto boxData = boxDataProvider->GetBoxData(currentSave, i);
        pokemonSaveBox->SetBoxData(i, boxData);
    }

    // start at box 0
    pokemonSaveBox->SetCurrentBox(0);

    LOG_DEBUG("Box data loaded successfully");
}

StorageScreen::~StorageScreen() = default;

void StorageScreen::OnInput(u64 down, u64 up, u64 held) {
    if (isSummaryOverlayVisible) {
        if (down & HidNpadButton_B) {
            onHideOverlay();
            isSummaryOverlayVisible = false;
            SetActiveBox(activeBox);
        }
        return;
    }

    if (HandleHelpInput(down)) {
        return;
    }

    static constexpr int ITEMS_PER_ROW = 6;
    bool shouldHandleBoxSwitch = false;

    if (activeBox == ActiveBox::Save && pokemonSaveBox) {
        const int slotIndex = pokemonSaveBox->GetSelectedSlot();
        if (slotIndex >= 0) {
            shouldHandleBoxSwitch = (slotIndex % ITEMS_PER_ROW) == 0;
        }
    } else if (activeBox == ActiveBox::Bank && pokemonBankBox) {
        const int slotIndex = pokemonBankBox->GetSelectedSlot();
        if (slotIndex >= 0) {
            shouldHandleBoxSwitch = (slotIndex % ITEMS_PER_ROW) == (ITEMS_PER_ROW - 1);
        }
    }

    // process directional inputs for cross-box switching at the box edge
    if (shouldHandleBoxSwitch) {
        pokemonBoxDirectionalHandler.HandleInput(down, held);
    }

    // Process button inputs
    buttonHandler.HandleInput(down, up, held);
}

std::vector<pksm::ui::HelpItem> StorageScreen::GetHelpOverlayItems() const {
    return {
        {{{pksm::ui::global::ButtonGlyph::A}}, "Select Pokémon"},
        {{{pksm::ui::global::ButtonGlyph::B}}, "Back to Main Menu"},
        {{{pksm::ui::global::ButtonGlyph::DPad}, {pksm::ui::global::ButtonGlyph::AnalogStick}}, "Navigate Box"},
        {{{pksm::ui::global::ButtonGlyph::L}}, "Previous Box"},
        {{{pksm::ui::global::ButtonGlyph::R}}, "Next Box"},
        {{{pksm::ui::global::ButtonGlyph::Minus}}, "Close Help"}
    };
}

void StorageScreen::OnHelpOverlayShown() {
    LOG_DEBUG("Help overlay shown, disabling UI elements");
    if (pokemonBankBox) {
        pokemonBankBox->SetDisabled(true);
    }
    if (pokemonSaveBox) {
        pokemonSaveBox->SetDisabled(true);
    }
}

void StorageScreen::OnHelpOverlayHidden() {
    LOG_DEBUG("Help overlay hidden, re-enabling UI elements");
    SetActiveBox(activeBox);
}

void StorageScreen::SetActiveBox(ActiveBox box) {
    static constexpr int ITEMS_PER_ROW = 6;

    const ActiveBox previousBox = activeBox;
    int previousSelectedSlot = -1;
    if (previousBox == ActiveBox::Bank && pokemonBankBox) {
        previousSelectedSlot = pokemonBankBox->GetSelectedSlot();
    } else if (previousBox == ActiveBox::Save && pokemonSaveBox) {
        previousSelectedSlot = pokemonSaveBox->GetSelectedSlot();
    }

    activeBox = box;

    if (pokemonBankBox) {
        pokemonBankBox->SetDisabled(activeBox != ActiveBox::Bank);
    }
    if (pokemonSaveBox) {
        pokemonSaveBox->SetDisabled(activeBox != ActiveBox::Save);
    }

    if (activeBox == ActiveBox::Bank && pokemonBankBox) {
        if (previousSelectedSlot >= 0) {
            const int row = previousSelectedSlot / ITEMS_PER_ROW;
            pokemonBankBox->SetSelectedSlot(row * ITEMS_PER_ROW + (ITEMS_PER_ROW - 1));
        }
        pokemonBankBox->RequestFocus();
    } else if (activeBox == ActiveBox::Save && pokemonSaveBox) {
        if (previousSelectedSlot >= 0) {
            const int row = previousSelectedSlot / ITEMS_PER_ROW;
            pokemonSaveBox->SetSelectedSlot(row * ITEMS_PER_ROW);
        }
        pokemonSaveBox->RequestFocus();
    }

    pokemonBoxDirectionalHandler.ClearState();
}

}  // namespace pksm::layout