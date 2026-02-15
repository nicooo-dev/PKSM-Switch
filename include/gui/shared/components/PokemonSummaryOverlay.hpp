#pragma once

#include <memory>
#include <pu/ui/ui_Overlay.hpp>

#include <pu/ui/elm/elm_Rectangle.hpp>
#include <pu/ui/elm/elm_TextBlock.hpp>

#include "pksmcore/pkx/PKX.hpp"

namespace pksm::ui {

class PokemonSummaryOverlay : public pu::ui::Overlay {
public:
    PokemonSummaryOverlay(const pu::i32 x, const pu::i32 y, const pu::i32 width, const pu::i32 height);
    PU_SMART_CTOR(PokemonSummaryOverlay)

    void SetPokemon(std::unique_ptr<pksm::PKX> pk);

private:
    void Rebuild();

    std::unique_ptr<pksm::PKX> pk;

    static constexpr pu::ui::Color OVERLAY_BG = pu::ui::Color(0, 0, 0, 160);

    static constexpr pu::i32 PANEL_X = 48;
    static constexpr pu::i32 PANEL_Y = 46;
    static constexpr pu::i32 PANEL_W = 1184;
    static constexpr pu::i32 PANEL_H = 628;
    static constexpr pu::i32 PANEL_RADIUS = 38;

    static constexpr pu::ui::Color SHADOW = pu::ui::Color(0, 0, 0, 90);

    static constexpr pu::ui::Color PANEL_BG = pu::ui::Color(246, 248, 246, 255);
    static constexpr pu::ui::Color PANEL_HIGHLIGHT = pu::ui::Color(18, 60, 30, 18);
    static constexpr pu::ui::Color PANEL_BORDER = pu::ui::Color(18, 60, 30, 255);
    static constexpr pu::ui::Color TEXT_DARK = pu::ui::Color(8, 20, 10, 255);

    static constexpr pu::ui::Color PILL_BG = pu::ui::Color(18, 60, 30, 50);
    static constexpr pu::ui::Color DIVIDER = pu::ui::Color(18, 60, 30, 55);

    static constexpr pu::ui::Color CHIP_BG = pu::ui::Color(18, 60, 30, 75);
    static constexpr pu::ui::Color CHIP_TEXT = pu::ui::Color(8, 20, 10, 255);

    static constexpr pu::i32 PAD = 48;
    static constexpr pu::i32 COL_GAP = 90;
    static constexpr pu::i32 ROW_GAP = 14;
    static constexpr pu::i32 LABEL_W = 210;
    static constexpr pu::i32 RIGHT_LABEL_W = 260;
    static constexpr pu::i32 RIGHT_VALUE_W = 120;

    static constexpr pu::i32 LEFT_COL_W = 560;
};

}  // namespace pksm::ui