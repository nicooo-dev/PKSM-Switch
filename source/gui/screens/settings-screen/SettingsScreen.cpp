#include "gui/screens/settings-screen/SettingsScreen.hpp"
#include "gui/shared/UIConstants.hpp"
#include "utils/Logger.hpp"

namespace pksm::layout {

SettingsScreen::SettingsScreen(
    std::function<void()> onBack,
    std::function<void(pu::ui::Overlay::Ref)> onShowOverlay,
    std::function<void()> onHideOverlay
)
  : BaseLayout(onShowOverlay, onHideOverlay),
    onBack(onBack),
    buttonHandler() {
    
    LOG_DEBUG("Initializing SettingsScreen...");

    this->SetBackgroundColor(bgColor);
    background = ui::AnimatedBackground::New();
    this->Add(background);

    // Create header text
    headerText = pu::ui::elm::TextBlock::New(SIDE_MARGIN, HEADER_TOP_MARGIN, "Settings");
    headerText->SetColor(pksm::ui::global::TEXT_WHITE);
    headerText->SetFont(pksm::ui::global::MakeHeavyFontName(pksm::ui::global::FONT_SIZE_TITLE));
    this->Add(headerText);

    pu::i32 currentY = CONTENT_TOP_MARGIN;

    // General Section
    CreateSectionHeader("General", currentY);
    currentY += SECTION_SPACING;

    CreateSettingButton(
        "Language",
        "English",
        currentY,
        []() { LOG_DEBUG("Language setting clicked"); }
    );
    currentY += BUTTON_HEIGHT + BUTTON_SPACING;

    CreateSettingButton(
        "Auto-save",
        "Enabled",
        currentY,
        []() { LOG_DEBUG("Auto-save setting clicked"); }
    );
    currentY += BUTTON_HEIGHT + BUTTON_SPACING;

    // Appearance Section
    CreateSectionHeader("Appearance", currentY);
    currentY += SECTION_SPACING;

    CreateSettingButton(
        "Theme",
        "Dark",
        currentY,
        []() { LOG_DEBUG("Theme setting clicked"); }
    );
    currentY += BUTTON_HEIGHT + BUTTON_SPACING;

    CreateSettingButton(
        "Font Size",
        "Medium",
        currentY,
        []() { LOG_DEBUG("Font size setting clicked"); }
    );
    currentY += BUTTON_HEIGHT + BUTTON_SPACING;

    CreateSettingButton(
        "Animation Speed",
        "Normal",
        currentY,
        []() { LOG_DEBUG("Animation speed setting clicked"); }
    );
    currentY += BUTTON_HEIGHT + BUTTON_SPACING + SECTION_SPACING;

    // Advanced Section
    CreateSectionHeader("Advanced", currentY);
    currentY += SECTION_SPACING;

    CreateSettingButton(
        "Debug Mode",
        "Disabled",
        currentY,
        []() { LOG_DEBUG("Debug mode setting clicked"); }
    );
    currentY += BUTTON_HEIGHT + BUTTON_SPACING;

    CreateSettingButton(
        "Clear Cache",
        "45.2 MB",
        currentY,
        []() { LOG_DEBUG("Clear cache setting clicked"); }
    );

    // Initialize focus manager
    focusManager = pksm::input::FocusManager::New("SettingsScreen Manager");
    focusManager->SetActive(true);
    for (auto& button : settingButtons) {
        focusManager->RegisterFocusable(button);
    }

    InitializeHelpFooter();

    // Set help items
    std::vector<pksm::ui::HelpItem> helpItems = {
        {{{pksm::ui::global::ButtonGlyph::A}}, "Select Setting"},
        {{{pksm::ui::global::ButtonGlyph::B}}, "Back"},
        {{{pksm::ui::global::ButtonGlyph::Minus}}, "Help"}
    };
    helpFooter->SetHelpItems(helpItems);

    // Setup back button handler
    buttonHandler.RegisterButton(
        HidNpadButton_B,
        nullptr,
        [this]() {
            LOG_DEBUG("B button pressed, returning to previous screen");
            if (this->onBack) {
                this->onBack();
            }
        }
    );

    // Set up input handling
    this->SetOnInput(
        std::bind(&SettingsScreen::OnInput, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)
    );

    LOG_DEBUG("SettingsScreen initialization complete");
}

SettingsScreen::~SettingsScreen() = default;

void SettingsScreen::CreateSectionHeader(const std::string& text, pu::i32 y) {
    auto header = pu::ui::elm::TextBlock::New(SIDE_MARGIN, y, text);
    header->SetColor(pksm::ui::global::TEXT_WHITE);
    header->SetFont(pksm::ui::global::MakeMediumFontName(pksm::ui::global::FONT_SIZE_HEADER));
    this->Add(header);
}

void SettingsScreen::CreateSettingButton(
    const std::string& label,
    const std::string& value,
    pu::i32 y,
    std::function<void()> onClick
) {
    auto button = pksm::ui::FocusableButton::New(
        SIDE_MARGIN,
        y,
        BUTTON_WIDTH,
        BUTTON_HEIGHT,
        label + ": " + value,
        pu::ui::Color(40, 40, 60, 200),
        pu::ui::Color(60, 60, 100, 255)
    );
    
    button->SetContentFont(pksm::ui::global::MakeMediumFontName(pksm::ui::global::FONT_SIZE_BUTTON));
    button->SetContentColor(pksm::ui::global::TEXT_WHITE);
    button->SetOnClick(onClick);
    button->SetHelpText("Change " + label);
    
    this->Add(button);
    settingButtons.push_back(button);
}

void SettingsScreen::OnInput(u64 down, u64 up, u64 held) {
    if (HandleHelpInput(down)) {
        return;  // Input was handled by help system
    }

    buttonHandler.HandleInput(down, up, held);

    // Handle focus navigation with D-pad
    if (down & HidNpadButton_Down) {
        auto currentFocused = focusManager->GetFocusedElement();
        if (currentFocused) {
            for (size_t i = 0; i < settingButtons.size(); i++) {
                if (settingButtons[i] == currentFocused && i + 1 < settingButtons.size()) {
                    settingButtons[i + 1]->RequestFocus();
                    break;
                }
            }
        } else if (!settingButtons.empty()) {
            settingButtons[0]->RequestFocus();
        }
    } else if (down & HidNpadButton_Up) {
        // Move to previous button
        auto currentFocused = focusManager->GetFocusedElement();
        if (currentFocused) {
            for (size_t i = 0; i < settingButtons.size(); i++) {
                if (settingButtons[i] == currentFocused && i > 0) {
                    settingButtons[i - 1]->RequestFocus();
                    break;
                }
            }
        } else if (!settingButtons.empty()) {
            settingButtons[0]->RequestFocus();
        }
    }

    // Let focused button handle A button press - call OnInput directly on the button
    auto focused = focusManager->GetFocusedElement();
    if (focused && (down & HidNpadButton_A)) {
        for (auto& button : settingButtons) {
            if (button == focused) {
                button->OnInput(down, up, held, pu::ui::TouchPoint());
                break;
            }
        }
    }
}

std::vector<pksm::ui::HelpItem> SettingsScreen::GetHelpOverlayItems() const {
    return {
        {{{pksm::ui::global::ButtonGlyph::A}}, "Select Setting"},
        {{{pksm::ui::global::ButtonGlyph::B}}, "Back"},
        {{{pksm::ui::global::ButtonGlyph::Minus}}, "Help"},
        {{{pksm::ui::global::ButtonGlyph::DPad}}, "Navigate"}
    };
}

void SettingsScreen::OnHelpOverlayShown() {
    for (auto& button : settingButtons) {
        button->SetVisible(false);
    }
    headerText->SetVisible(false);
}

void SettingsScreen::OnHelpOverlayHidden() {
    for (auto& button : settingButtons) {
        button->SetVisible(true);
    }
    headerText->SetVisible(true);
}

}  // namespace pksm::layout
