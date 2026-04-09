#include "gui/shared/components/PokemonBox.hpp"

#include <SDL2/SDL.h>
#include <SDL2/SDL2_gfxPrimitives.h>

#include "gui/shared/UIConstants.hpp"
#include "utils/Logger.hpp"
#include "utils/SDLHelper.hpp"

namespace pksm::ui {

PokemonBox::PokemonBox(
    const pu::i32 x,
    const pu::i32 y,
    const pu::i32 itemSize,
    input::FocusManager::Ref parentFocusManager,
    input::SelectionManager::Ref parentSelectionManager,
    const std::map<ShakeDirection, bool> shouldConsiderSideOutOfBounds,
    const pu::i32 numberOfRows,
        const pu::i32 itemsPerRow,
        const pu::i32 topFrameHeight,
        const pu::i32 bottomFrameHeight
)
    : Element(),
        x(x),
        y(y),
        topFrameHeight(topFrameHeight),
        bottomFrameHeight(bottomFrameHeight),
        focused(false),
        selected(false),
        disabled(false),
        navigationControlsVisible(true),
        footerControlsVisible(true),
        currentBox(0),
        maskTexture(nullptr) {
    buttonGridFocusManager = input::FocusManager::New("BoxGrid Focus Manager");
    buttonGridSelectionManager = input::SelectionManager::New("BoxGrid Selection Manager");

    // Set default colors
    frameColor = DEFAULT_FRAME_COLOR;
    borderColor = DEFAULT_BORDER_COLOR;

    // Calculate grid position with padding (extra padding at the top)
    pu::i32 gridX = x + FRAME_PADDING;
    pu::i32 gridY = y + this->topFrameHeight;

    // Create a local copy of the map that we can modify
    std::map<ShakeDirection, bool> localShouldConsiderSideOutOfBounds = shouldConsiderSideOutOfBounds;
    localShouldConsiderSideOutOfBounds[ShakeDirection::UP] = false;
    localShouldConsiderSideOutOfBounds[ShakeDirection::DOWN] = false;

    // Create the BoxGrid
    boxGrid = BoxGrid::New(
        gridX,
        gridY,
        itemSize,
        buttonGridFocusManager,
        buttonGridSelectionManager,
        localShouldConsiderSideOutOfBounds,
        numberOfRows,
        itemsPerRow
    );
    boxGrid->SetName("BoxGrid Element");
    boxGrid->EstablishOwningRelationship();

    // Initialize container for child elements
    container = pu::ui::Container::New(x, y, GetWidth(), GetHeight());

    // Create the navigation buttons
    leftNavigationButton = BoxNavigationButton::NewLeftButton(0, 0);
    rightNavigationButton = BoxNavigationButton::NewRightButton(0, 0);

    // Create border outline
    borderOutline = RectangularOutline::New(x, y, GetWidth(), GetHeight(), borderColor, CORNER_RADIUS, BORDER_THICKNESS);

    // Create the box name pill
    boxNamePill = PillTextContainer::New(x, y, 0, 0, "");

    // Create the box counter text
    boxCounterText = pu::ui::elm::TextBlock::New(0, 0, "1/1");
    boxCounterText->SetColor(pu::ui::Color(255, 255, 255, 255));
    boxCounterText->SetFont(global::MakeMediumFontName(global::FONT_SIZE_BOX_SPACES_BUTTON));

    // Initialize the Box Spaces button
    InitializeBoxSpacesButton();

    // Position will be set in UpdateBoxNameText
    UpdateBoxNameText();

    // Update box counter position and text
    UpdateBoxCounterText();

    // Add components to our container
    container->Add(borderOutline);
    container->Add(boxGrid);
    container->Add(boxNamePill);
    container->Add(leftNavigationButton);
    container->Add(rightNavigationButton);
    container->Add(boxSpacesButton);
    container->Add(boxCounterText);

    UpdateMaskTexture();

    // Set up button handler for L/R buttons for box navigation
    buttonHandler.RegisterButton(
        HidNpadButton_L,
        [this]() {
            // Change navigation button state when L button is pressed
            leftNavigationButton->SetPressed(true);
        },
        [this]() {
            // Restore original state when released
            leftNavigationButton->SetPressed(false);
            PreviousBox();
        }
    );

    buttonHandler.RegisterButton(
        HidNpadButton_R,
        [this]() {
            // Change navigation button state when R button is pressed
            rightNavigationButton->SetPressed(true);
        },
        [this]() {
            // Restore original state when released
            rightNavigationButton->SetPressed(false);
            NextBox();
        }
    );

    // Connect the BoxGrid's selection changed callback
    boxGrid->SetOnSelectionChanged([this](int slotIndex) {
        if (onSelectionChangedCallback) {
            onSelectionChangedCallback(currentBox, slotIndex);
        }
    });

    SetBoxCount(1);
    container->PreRender();

    // Set up directional input handlers
    boxGridDirectionalHandler.SetOnMoveUp([this]() {
        if (boxGrid->ShouldResignUpFocus()) {
            boxNamePill->RequestFocus();
        }
    });
    boxGridDirectionalHandler.SetOnMoveDown([this]() {
        if (boxGrid->ShouldResignDownFocus()) {
            if (footerControlsVisible) {
                boxSpacesButton->RequestFocus();
            }
        }
    });

    boxHeaderDirectionalHandler.SetOnMoveUp([this]() { boxNamePill->shakeOutOfBounds(ShakeDirection::UP); });
    boxHeaderDirectionalHandler.SetOnMoveDown([this]() { boxGrid->RequestFocus(); });
    boxHeaderDirectionalHandler.SetOnMoveLeft([this]() {
        this->PreviousBox();
        boxNamePill->shakeOutOfBounds(ShakeDirection::LEFT);
    });
    boxHeaderDirectionalHandler.SetOnMoveRight([this]() {
        this->NextBox();
        boxNamePill->shakeOutOfBounds(ShakeDirection::RIGHT);
    });

    boxFooterDirectionalHandler.SetOnMoveUp([this]() { boxGrid->RequestFocus(); });
    boxFooterDirectionalHandler.SetOnMoveDown([this]() { boxSpacesButton->shakeOutOfBounds(ShakeDirection::DOWN); });
    boxFooterDirectionalHandler.SetOnMoveLeft([this]() { boxSpacesButton->shakeOutOfBounds(ShakeDirection::LEFT); });
    boxFooterDirectionalHandler.SetOnMoveRight([this]() { boxSpacesButton->shakeOutOfBounds(ShakeDirection::RIGHT); });

    // Establish relationships for input handling
    SetFocusManager(parentFocusManager);
    SetSelectionManager(parentSelectionManager);
    LOG_DEBUG("PokemonBox component initialization complete");
}

pu::i32 PokemonBox::GetX() {
    return x;
}

pu::i32 PokemonBox::GetY() {
    return y;
}

pu::i32 PokemonBox::GetWidth() {
    // Base our width on the BoxGrid's width plus padding
    return boxGrid->GetWidth() + (FRAME_PADDING * 2);
}

pu::i32 PokemonBox::GetHeight() {
    // Base our height on the BoxGrid's height plus top and bottom frame heights
    return boxGrid->GetHeight() + topFrameHeight + bottomFrameHeight;
}

PokemonBox::~PokemonBox() {
    // Clean up the mask texture
    if (maskTexture) {
        SDL_DestroyTexture(maskTexture);
        maskTexture = nullptr;
    }
}

void PokemonBox::UpdateMaskTexture() {
    // Clean up existing texture
    if (maskTexture) {
        SDL_DestroyTexture(maskTexture);
        maskTexture = nullptr;
    }

    // Calculate our dimensions
    pu::i32 width = GetWidth();
    pu::i32 height = GetHeight();

    if (width <= 0 || height <= 0) {
        LOG_ERROR(
            "UpdateMaskTexture: invalid dimensions " + std::to_string(width) + "x" + std::to_string(height) +
            ", skipping"
        );
        return;
    }

    // Calculate grid dimensions and position
    pu::i32 gridWidth = boxGrid->GetWidth();
    pu::i32 gridHeight = boxGrid->GetHeight();
    pu::i32 gridX = boxGrid->GetX();
    pu::i32 gridY = boxGrid->GetY();

    // Create new mask texture
    maskTexture = SDL_CreateTexture(
        pu::ui::render::GetMainRenderer(),
        SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_TARGET,
        width,
        height
    );

    if (!maskTexture) {
        LOG_ERROR("Failed to create mask texture for PokemonBox");
        return;
    }

    // Set up blend mode for texture
    if (SDL_SetTextureBlendMode(maskTexture, pksm::sdl::BlendModeBlend()) < 0) {
        LOG_ERROR("Failed to set texture blend mode: " + std::string(SDL_GetError()));
        SDL_DestroyTexture(maskTexture);
        maskTexture = nullptr;
        return;
    }

    // Set render target to texture
    SDL_Renderer* renderer = pu::ui::render::GetMainRenderer();
    if (SDL_SetRenderTarget(renderer, maskTexture) < 0) {
        LOG_ERROR("Failed to set render target: " + std::string(SDL_GetError()));
        SDL_DestroyTexture(maskTexture);
        maskTexture = nullptr;
        return;
    }

    // Clear with full transparency
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    SDL_RenderClear(renderer);

    // Draw the outer frame with rounded corners
    // Use SDL2_gfx primitives directly for the filled rounded rectangle
    pu::ui::Color color = frameColor;
    roundedBoxRGBA(renderer, 0, 0, width - 1, height - 1, CORNER_RADIUS, color.r, color.g, color.b, color.a);

    // Calculate grid cutout position relative to the frame texture
    pu::i32 gridRelativeX = gridX - x;
    pu::i32 gridRelativeY = gridY - y;

    // Cut out the grid area with full transparency
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    SDL_Rect gridRect = {gridRelativeX, gridRelativeY, gridWidth, gridHeight};
    SDL_RenderFillRect(renderer, &gridRect);

    // Reset render target to NULL instead of previous target
    if (SDL_SetRenderTarget(renderer, NULL) < 0) {
        LOG_ERROR("Failed to reset render target: " + std::string(SDL_GetError()));
        SDL_DestroyTexture(maskTexture);
        maskTexture = nullptr;
        return;
    }

    LOG_DEBUG("PokemonBox mask texture created successfully");
}

void PokemonBox::UpdateBoxNameText() {
    // Get current box name
    std::string boxName = "Box 1";  // Default name
    if (!boxes.empty() && static_cast<size_t>(currentBox) < boxes.size()) {
        boxName = boxes[currentBox].name;
    }

    // Calculate vertical position (center vertically in the top frame area)
    pu::i32 navY = y + (topFrameHeight - leftNavigationButton->GetHeight()) / 2;
    pu::i32 boxNameContainerHeight = std::max(topFrameHeight - (30 * 2), 24);
    pu::i32 boxNameContainerY = y + std::max((topFrameHeight - boxNameContainerHeight) / 2, 0);

    if (navigationControlsVisible) {
        leftNavigationButton->SetVisible(true);
        rightNavigationButton->SetVisible(true);

        // Position the left navigation button
        pu::i32 leftNavX = x + NAVIGATION_SIDE_MARGIN;
        leftNavigationButton->SetPosition(leftNavX, navY);

        // Position the right navigation button
        pu::i32 rightNavX = x + GetWidth() - NAVIGATION_SIDE_MARGIN - rightNavigationButton->GetWidth();
        rightNavigationButton->SetPosition(rightNavX, navY);

        // Calculate the right-most point of left navigation
        pu::i32 leftElementsEndX = leftNavX + leftNavigationButton->GetWidth();

        // Calculate the left-most point of right navigation
        pu::i32 rightElementsStartX = rightNavX;

        // This is the space between left elements end and right elements start
        pu::i32 availableWidth = rightElementsStartX - leftElementsEndX - (40 * 2);  // Horizontal margin of 40px

        // Ensure we have a minimum width for the container
        pu::i32 boxNameContainerWidth = std::max(availableWidth, 100);  // Minimum width of 100 pixels

        // Center the box name container in the available space
        pu::i32 boxNameContainerX = leftElementsEndX + 40;  // Horizontal margin of 40px
        boxNamePill->SetDimensions(boxNameContainerX, boxNameContainerY, boxNameContainerWidth, boxNameContainerHeight);
    } else {
        leftNavigationButton->SetVisible(false);
        rightNavigationButton->SetVisible(false);

        const pu::i32 sidePad = 26;
        pu::i32 boxNameContainerX = x + sidePad;
        pu::i32 boxNameContainerWidth = std::max(GetWidth() - (sidePad * 2), 100);
        boxNamePill->SetDimensions(boxNameContainerX, boxNameContainerY, boxNameContainerWidth, boxNameContainerHeight);
    }

    boxNamePill->SetText(boxName);
}

void PokemonBox::InitializeBoxSpacesButton() {
    // Load icon texture
    pu::sdl2::Texture iconTexture = pu::ui::render::LoadImage("romfs:/gfx/ui/icon_storage.png");
    pu::sdl2::TextureHandle::Ref iconHandle = iconTexture ? pu::sdl2::TextureHandle::New(iconTexture) : nullptr;

    if (!iconTexture) {
        LOG_ERROR("Failed to load box spaces icon texture");
    }

    // In compact variants (e.g. Team panel), there may be no footer area.
    const pu::i32 effectiveBottomHeight = std::max(bottomFrameHeight, 0);
    pu::i32 boxSpacesHeight = std::max(effectiveBottomHeight - (BOX_SPACES_BOTTOM_MARGIN * 2), 1);

    // Position in bottom frame area (centered vertically)
    pu::i32 boxSpacesY = y + GetHeight() - effectiveBottomHeight;
    if (effectiveBottomHeight > 0) {
        boxSpacesY += std::max((effectiveBottomHeight - boxSpacesHeight) / 2, 0);
    }

    // Create button with calculated height
    boxSpacesButton = BoxSpacesButton::New(0, boxSpacesY, boxSpacesHeight, iconHandle);

    // Center horizontally
    pu::i32 boxSpacesX = x + (GetWidth() - boxSpacesButton->GetWidth()) / 2;
    boxSpacesButton->SetX(boxSpacesX);
    boxSpacesButton->SetY(boxSpacesY);
    boxSpacesButton->SetVisible(footerControlsVisible);

    LOG_DEBUG("BoxSpacesButton initialized");
}

void PokemonBox::UpdateBoxCounterText() {
    if (!boxCounterText || boxes.empty()) {
        return;
    }

    if (!footerControlsVisible) {
        boxCounterText->SetVisible(false);
        return;
    }
    boxCounterText->SetVisible(true);

    // Format the box counter text as "CurrentBox/TotalBoxes"
    std::string counterText = std::to_string(currentBox + 1) + "/" + std::to_string(boxes.size());
    boxCounterText->SetText(counterText);

    // Position the counter text at the right side of the bottom frame
    // Vertically centered in the bottom frame area
    pu::i32 boxCounterY = y + GetHeight() - (bottomFrameHeight / 2) - (boxCounterText->GetHeight() / 2);
    pu::i32 boxCounterX = x + GetWidth() - BOX_COUNTER_RIGHT_MARGIN - boxCounterText->GetWidth();

    boxCounterText->SetX(boxCounterX);
    boxCounterText->SetY(boxCounterY);
}

void PokemonBox::OnRender(pu::ui::render::Renderer::Ref& drawer, const pu::i32 x, const pu::i32 y) {
    // Draw the frame with the cached mask texture if available
    if (maskTexture) {
        SDL_Rect destRect = {x, y, GetWidth(), GetHeight()};
        SDL_RenderCopy(pu::ui::render::GetMainRenderer(), maskTexture, nullptr, &destRect);
    } else {
        UpdateMaskTexture();
    }

    // Render only visible elements in the container
    for (auto& element : container->GetElements()) {
        if (element->IsVisible()) {
            element->OnRender(drawer, element->GetX(), element->GetY());
        }
    }
}

void PokemonBox::OnInput(
    const u64 keys_down,
    const u64 keys_up,
    const u64 keys_held,
    const pu::ui::TouchPoint touch_pos
) {
    if (disabled) {
        return;
    }

    // Only allow L/R box navigation when this panel is focused (prevents
    // conflicts when another panel owns L/R for its own purpose, e.g. the
    // EditorScreen uses L/R to switch between the Team and Box panels).
    if (focused && navigationControlsVisible) {
        buttonHandler.HandleInput(keys_down, keys_up, keys_held);
    }
    if (boxGrid->IsFocused()) {
        boxGridDirectionalHandler.HandleInput(keys_down, keys_held);
    } else if (boxNamePill->IsFocused()) {
        boxHeaderDirectionalHandler.HandleInput(keys_down, keys_held);
    } else if (footerControlsVisible && boxSpacesButton->IsFocused()) {
        boxFooterDirectionalHandler.HandleInput(keys_down, keys_held);
    }

    boxGrid->OnInput(keys_down, keys_up, keys_held, touch_pos);
    boxNamePill->OnInput(keys_down, keys_up, keys_held, touch_pos);
    if (footerControlsVisible) {
        boxSpacesButton->OnInput(keys_down, keys_up, keys_held, touch_pos);
    }
}

void PokemonBox::SetFocused(bool focused) {
    LOG_DEBUG("[BoxGrid] SetFocused: " + std::to_string(focused));
    if (this->focused != focused) {
        this->focused = focused;

        // Propagate focus to / from the BoxGrid so that its directional
        // input handler only fires when this panel is actually active.
        boxGrid->SetFocused(focused);
        if (focused) {
            boxGrid->RequestFocus();
        }
    }
}

bool PokemonBox::IsFocused() const {
    return focused;
}

void PokemonBox::SetSelected(bool selected) {
    this->selected = selected;
}

bool PokemonBox::IsSelected() const {
    return selected;
}

void PokemonBox::EstablishOwningRelationship() {
    IFocusable::EstablishOwningRelationship();
    ISelectable::EstablishOwningRelationship();
}

// Box management methods
void PokemonBox::SetBoxCount(size_t count) {
    // Resize the boxes vector to hold the requested number of boxes
    boxes.resize(count);

    // Initialize each box with empty Pokémon data if it's a new box
    for (size_t i = 0; i < boxes.size(); i++) {
        // Calculate number of slots based on GridBox dimensions
        size_t slotsPerBox = static_cast<size_t>(boxGrid->GetItemCount());
        if (boxes[i].size() == 0) {
            boxes[i].resize(slotsPerBox);

            // Set a default box name with index
            boxes[i].name = "Box " + std::to_string(i + 1);
        } else if (boxes[i].size() != slotsPerBox) {
            // Resize existing boxes to match the expected size
            boxes[i].resize(slotsPerBox);
        }
    }

    // Make sure current box index is valid
    if (static_cast<size_t>(currentBox) >= count) {
        SetCurrentBox(0);
    } else {
        // Update the grid to display the current box
        UpdateBoxGrid();
        // Update the box counter
        UpdateBoxCounterText();
    }
}

void PokemonBox::NextBox() {
    int nextBox = currentBox + 1;
    if (static_cast<size_t>(nextBox) < boxes.size()) {
        SetCurrentBox(nextBox);
    } else {
        SetCurrentBox(0);
    }
}

void PokemonBox::PreviousBox() {
    if (currentBox > 0) {
        SetCurrentBox(currentBox - 1);
    } else {
        SetCurrentBox(boxes.size() - 1);
    }
}

void PokemonBox::SetCurrentBox(int boxIndex) {
    if (boxIndex >= 0 && static_cast<size_t>(boxIndex) < boxes.size() && boxIndex != currentBox) {
        currentBox = boxIndex;

        // Update the grid with data from the selected box
        UpdateBoxGrid();

        // Update box name display
        UpdateBoxNameText();

        // Update box counter display
        UpdateBoxCounterText();

        // Update sprite cache
        UpdateSpriteCache();

        // Notify listeners that the selected slot (always slot 0 after a box
        // change because UpdateGridFromBoxData resets to slot 0) has changed.
        // This keeps the info panel in sync when the player navigates between
        // boxes — the BoxGrid callback alone can't do this because the
        // selectedIndex guard prevents a 0→0 re-fire.
        if (onSelectionChangedCallback) {
            onSelectionChangedCallback(currentBox, static_cast<int>(boxGrid->GetSelectedIndex()));
        }
    }
}

void PokemonBox::ForceRefreshCurrentBox() {
    // Unconditionally refresh the grid for the current box and fire the
    // selection-changed callback.  This is needed for the team panel where
    // currentBox is always 0, so SetCurrentBox(0) is a no-op due to its
    // same-index guard.
    UpdateBoxGrid();
    UpdateBoxNameText();
    UpdateBoxCounterText();
    UpdateSpriteCache();
    if (onSelectionChangedCallback) {
        onSelectionChangedCallback(currentBox, static_cast<int>(boxGrid->GetSelectedIndex()));
    }
}

void PokemonBox::UpdateBoxGrid() {
    // Only update if we have boxes
    if (boxes.empty()) {
        return;
    }

    // Send the current box data to the grid
    boxGrid->SetBoxData(boxes[currentBox]);
}

void PokemonBox::UpdateSpriteCache() {
    // Build a list of active sprites to keep in memory
    std::set<std::string> activeKeys;

    if (!boxes.empty() && static_cast<size_t>(currentBox) < boxes.size()) {
        // Add sprites for the current box
        for (size_t i = 0; i < boxes[currentBox].size(); i++) {
            const auto& pokemonData = boxes[currentBox][i];
            if (!pokemonData.isEmpty()) {
                activeKeys.insert(
                    utils::PokemonSpriteManager::GenerateKey(pokemonData.species, pokemonData.form, pokemonData.shiny)
                );
            }
        }
    }

    // Release sprites that aren't in the current box
    utils::PokemonSpriteManager::ReleaseUnusedSprites(activeKeys);
}

void PokemonBox::SetPokemonData(int boxIndex, int slotIndex, const BoxPokemonData& data) {
    if (boxIndex >= 0 && static_cast<size_t>(boxIndex) < boxes.size() && slotIndex >= 0 &&
        static_cast<size_t>(slotIndex) < boxes[boxIndex].size()) {
        // Update our stored data
        boxes[boxIndex][slotIndex] = data;

        // If this is the current box, update the grid
        if (boxIndex == currentBox) {
            boxGrid->SetPokemonData(slotIndex, data);
        }
    }
}

void PokemonBox::SetBoxData(int boxIndex, const BoxData& boxData) {
    if (boxIndex >= 0 && static_cast<size_t>(boxIndex) < boxes.size()) {
        boxes[boxIndex] = boxData;

        // Resize if needed
        size_t expectedSize = static_cast<size_t>(boxGrid->GetItemCount());
        if (boxes[boxIndex].size() != expectedSize) {
            boxes[boxIndex].resize(expectedSize);
        }

        // If this is the current box, update the grid and box name
        if (boxIndex == currentBox) {
            boxGrid->SetBoxData(boxData);
            UpdateBoxNameText();
        }
    }
}

BoxPokemonData PokemonBox::GetPokemonData(int boxIndex, int slotIndex) const {
    if (boxIndex >= 0 && static_cast<size_t>(boxIndex) < boxes.size() && slotIndex >= 0 &&
        static_cast<size_t>(slotIndex) < boxes[boxIndex].size()) {
        return boxes[boxIndex][slotIndex];
    }
    return BoxPokemonData();  // Return empty data for invalid indices
}

int PokemonBox::GetSelectedSlot() const {
    return static_cast<int>(boxGrid->GetSelectedIndex());
}

std::pair<pu::i32, pu::i32> PokemonBox::GetSelectedSlotScreenPosition() const {
    return boxGrid->GetSelectedSlotPosition();
}

void PokemonBox::SetSelectedSlot(int slotIndex) {
    if (!boxGrid) {
        return;
    }

    if (slotIndex < 0) {
        return;
    }

    const size_t index = static_cast<size_t>(slotIndex);
    if (index >= boxGrid->GetItemCount()) {
        return;
    }

    boxGrid->SetSelectedIndex(index);
}

void PokemonBox::SetDisabled(bool disabled) {
    this->disabled = disabled;
}

void PokemonBox::SetColors(const pu::ui::Color& frameColor, const pu::ui::Color& borderColor) {
    this->frameColor = frameColor;
    this->borderColor = borderColor;

    // Update the border outline color
    if (borderOutline) {
        borderOutline->SetColor(borderColor);
    }
}

void PokemonBox::SetNavigationControlsVisible(bool visible) {
    navigationControlsVisible = visible;
    if (leftNavigationButton) {
        leftNavigationButton->SetVisible(visible);
    }
    if (rightNavigationButton) {
        rightNavigationButton->SetVisible(visible);
    }
    UpdateBoxNameText();
}

void PokemonBox::SetTwentySlotMode(bool on) {
    if (boxGrid) {
        boxGrid->SetTwentySlotMode(on);
    }
}

bool PokemonBox::GetTwentySlotMode() const {
    return boxGrid && boxGrid->GetTwentySlotMode();
}

void PokemonBox::SetFooterControlsVisible(bool visible) {
    footerControlsVisible = visible;
    if (boxSpacesButton) {
        boxSpacesButton->SetVisible(visible);
    }
    if (boxCounterText) {
        boxCounterText->SetVisible(visible);
    }

    if (!visible && boxSpacesButton && boxSpacesButton->IsFocused() && boxGrid) {
        boxGrid->RequestFocus();
    }

    UpdateBoxCounterText();
}

void PokemonBox::SetFocusManager(input::FocusManager::Ref focusManager) {
    LOG_DEBUG("Setting focus manager on GameList");
    IFocusable::SetFocusManager(focusManager);

    // Register all child managers
    focusManager->RegisterChildManager(buttonGridFocusManager);
    focusManager->RegisterFocusable(boxNamePill);
    focusManager->RegisterFocusable(boxSpacesButton);
}

void PokemonBox::SetSelectionManager(input::SelectionManager::Ref selectionManager) {
    LOG_DEBUG("Setting selection manager on GameList");
    ISelectable::SetSelectionManager(selectionManager);

    // Register all child managers
    selectionManager->RegisterChildManager(buttonGridSelectionManager);
}

}  // namespace pksm::ui