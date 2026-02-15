#include "gui/shared/components/PokemonSummaryOverlay.hpp"

#include "gui/shared/UIConstants.hpp"
#include "pksmcore/utils/i18n.hpp"
#include "utils/Logger.hpp"

namespace pksm::ui {

PokemonSummaryOverlay::PokemonSummaryOverlay(const pu::i32 x, const pu::i32 y, const pu::i32 width, const pu::i32 height)
  : pu::ui::Overlay(x, y, width, height, OVERLAY_BG) {
    this->SetRadius(0);
    this->SetMaxFadeAlpha(200);
    this->SetFadeAlphaVariation(18);
}

void PokemonSummaryOverlay::SetPokemon(std::unique_ptr<pksm::PKX> pk) {
    this->pk = std::move(pk);
    this->Rebuild();
}

static std::string GenderSymbol(const pksm::Gender g) {
    if (g == pksm::Gender::Male) {
        return "\xE2\x99\x82"; // ♂
    }
    if (g == pksm::Gender::Female) {
        return "\xE2\x99\x80"; // ♀
    }
    return "-";
}

static std::string LocalizedOrFallback(const std::string &s, const std::string &fallback) {
    if (s.empty()) {
        return fallback;
    }
    return s;
}

static pu::ui::Color TypeColor(const pksm::Type t) {
    using C = pu::ui::Color;
    switch (static_cast<pksm::Type::EnumType>(t)) {
        case pksm::Type::EnumType::Normal: return C(168, 168, 120, 255);
        case pksm::Type::EnumType::Fire: return C(240, 128, 48, 255);
        case pksm::Type::EnumType::Water: return C(104, 144, 240, 255);
        case pksm::Type::EnumType::Electric: return C(248, 208, 48, 255);
        case pksm::Type::EnumType::Grass: return C(120, 200, 80, 255);
        case pksm::Type::EnumType::Ice: return C(152, 216, 216, 255);
        case pksm::Type::EnumType::Fighting: return C(192, 48, 40, 255);
        case pksm::Type::EnumType::Poison: return C(160, 64, 160, 255);
        case pksm::Type::EnumType::Ground: return C(224, 192, 104, 255);
        case pksm::Type::EnumType::Flying: return C(168, 144, 240, 255);
        case pksm::Type::EnumType::Psychic: return C(248, 88, 136, 255);
        case pksm::Type::EnumType::Bug: return C(168, 184, 32, 255);
        case pksm::Type::EnumType::Rock: return C(184, 160, 56, 255);
        case pksm::Type::EnumType::Ghost: return C(112, 88, 152, 255);
        case pksm::Type::EnumType::Dragon: return C(112, 56, 248, 255);
        case pksm::Type::EnumType::Dark: return C(112, 88, 72, 255);
        case pksm::Type::EnumType::Steel: return C(184, 184, 208, 255);
        case pksm::Type::EnumType::Fairy: return C(238, 153, 172, 255);
        default: return C(120, 120, 120, 255);
    }
}

static std::string LocalizeMove(const pksm::Language lang, const pksm::Move mv) {
    if (mv == pksm::Move::None) {
        return "--";
    }
    const auto &s = i18n::move(lang, mv);
    if (s.empty()) {
        return "Move #" + std::to_string(static_cast<int>(static_cast<u16>(mv)));
    }
    return s;
}

static std::string LocalizeAbility(const pksm::Language lang, const pksm::Ability ab) {
    const auto &s = i18n::ability(lang, ab);
    if (s.empty()) {
        return "Ability #" + std::to_string(static_cast<int>(static_cast<u16>(ab)));
    }
    return s;
}

static std::string LocalizeNature(const pksm::Language lang, const pksm::Nature nat) {
    const auto &s = i18n::nature(lang, nat);
    if (s.empty()) {
        return "Nature #" + std::to_string(static_cast<int>(static_cast<u16>(nat)));
    }
    return s;
}

static std::string LocalizeType(const pksm::Language lang, const pksm::Type t) {
    const auto &s = i18n::type(lang, t);
    if (s.empty()) {
        return "Type";
    }
    return s;
}

static std::string LocalizeSpecies(const pksm::Language lang, const pksm::Species sp) {
    const auto &s = i18n::species(lang, sp);
    if (s.empty()) {
        return "Species";
    }
    return s;
}

static std::string LocalizeItem(const pksm::Language lang, const u16 item_id) {
    if (item_id == 0) {
        return "None";
    }
    const auto &s = i18n::item(lang, item_id);
    if (s.empty()) {
        return "Item #" + std::to_string(item_id);
    }
    return s;
}

void PokemonSummaryOverlay::Rebuild() {
    this->Clear();

    if (!this->pk) {
        return;
    }

    const auto lang = pksm::Language::ENG;

    // ensure i18n tables are initialized
    i18n::init(lang);

    auto shadow = pu::ui::elm::Rectangle::New(PANEL_X + 10, PANEL_Y + 14, PANEL_W, PANEL_H, SHADOW, PANEL_RADIUS);
    this->Add(shadow);

    auto panelBorder = pu::ui::elm::Rectangle::New(PANEL_X, PANEL_Y, PANEL_W, PANEL_H, PANEL_BORDER, PANEL_RADIUS);
    auto panelBg = pu::ui::elm::Rectangle::New(PANEL_X + 6, PANEL_Y + 6, PANEL_W - 12, PANEL_H - 12, PANEL_BG, PANEL_RADIUS - 6);
    this->Add(panelBorder);
    this->Add(panelBg);

    const pu::ui::Color titleBarColor = pu::ui::Color(22, 74, 36, 255);
    auto titleBar = pu::ui::elm::Rectangle::New(PANEL_X + 6, PANEL_Y + 6, PANEL_W - 12, 72, titleBarColor, PANEL_RADIUS - 6);
    this->Add(titleBar);

    auto headerDivider = pu::ui::elm::Rectangle::New(PANEL_X + 24, PANEL_Y + 84, PANEL_W - 48, 3, DIVIDER, 2);
    this->Add(headerDivider);

    // pokéball placeholder asset soon
    auto ballPlaceholder = pu::ui::elm::Rectangle::New(PANEL_X + 20, PANEL_Y + 18, 36, 36, pu::ui::Color(255, 255, 255, 40), 18);
    this->Add(ballPlaceholder);

    const pu::i32 startX = PANEL_X + PAD;
    const pu::i32 startY = PANEL_Y + PAD;

    const auto speciesName = LocalizeSpecies(lang, this->pk->species());
    const auto genderSym = GenderSymbol(this->pk->gender());
    const auto titleLeft = speciesName;
    const auto titleMid = genderSym;
    const auto titleRight = "Lvl. " + std::to_string(this->pk->level());

    auto titleLeftText = pu::ui::elm::TextBlock::New(PANEL_X + 70, PANEL_Y + 16, titleLeft);
    titleLeftText->SetColor(pu::ui::Color(255, 255, 255, 255));
    titleLeftText->SetFont(pksm::ui::global::MakeHeavyFontName(pksm::ui::global::FONT_SIZE_BUTTON));
    this->Add(titleLeftText);

    auto titleMidText = pu::ui::elm::TextBlock::New(0, 0, titleMid);
    titleMidText->SetColor(pu::ui::Color(255, 255, 255, 255));
    titleMidText->SetFont(pksm::ui::global::MakeHeavyFontName(pksm::ui::global::FONT_SIZE_BUTTON));
    titleMidText->SetX(PANEL_X + 420);
    titleMidText->SetY(PANEL_Y + 16);
    this->Add(titleMidText);

    auto titleRightText = pu::ui::elm::TextBlock::New(0, 0, titleRight);
    titleRightText->SetColor(pu::ui::Color(255, 255, 255, 255));
    titleRightText->SetFont(pksm::ui::global::MakeHeavyFontName(pksm::ui::global::FONT_SIZE_BUTTON));
    const pu::i32 titleRightX = PANEL_X + PANEL_W - PAD - titleRightText->GetWidth();
    titleRightText->SetX(titleRightX);
    titleRightText->SetY(PANEL_Y + 16);
    this->Add(titleRightText);

    const pu::ui::Color cardBg = pu::ui::Color(255, 255, 255, 150);
    const pu::ui::Color cardBorder = pu::ui::Color(18, 60, 30, 55);

    auto addPair = [&](const pu::i32 x, pu::i32 &y, const std::string &label, const std::string &value) {
        auto lbl = pu::ui::elm::TextBlock::New(x, y, label);
        lbl->SetColor(TEXT_DARK);
        lbl->SetFont(pksm::ui::global::MakeHeavyFontName(pksm::ui::global::FONT_SIZE_BUTTON));
        this->Add(lbl);

        const pu::i32 valX = x + LABEL_W;
        auto val = pu::ui::elm::TextBlock::New(valX, y, value);
        val->SetColor(TEXT_DARK);
        val->SetFont(pksm::ui::global::MakeMediumFontName(pksm::ui::global::FONT_SIZE_BUTTON));
        this->Add(val);

        y += std::max(lbl->GetHeight(), val->GetHeight()) + ROW_GAP;
    };

    const pu::i32 leftX = startX;
    const pu::i32 rightX = startX + LEFT_COL_W + COL_GAP;

    const pu::i32 contentTop = PANEL_Y + 110;

    const pu::i32 leftCardX = leftX - 18;
    const pu::i32 leftCardY = contentTop;
    const pu::i32 leftCardW = LEFT_COL_W + 36;
    const pu::i32 leftCardH = PANEL_Y + PANEL_H - PAD - leftCardY;
    auto leftCardBorder = pu::ui::elm::Rectangle::New(leftCardX, leftCardY, leftCardW, leftCardH, cardBorder, 26);
    auto leftCard = pu::ui::elm::Rectangle::New(leftCardX + 4, leftCardY + 4, leftCardW - 8, leftCardH - 8, cardBg, 24);
    this->Add(leftCardBorder);
    this->Add(leftCard);

    const pu::i32 rightCardX = rightX - 18;
    const pu::i32 rightCardY = contentTop;
    const pu::i32 rightCardW = PANEL_X + PANEL_W - PAD - rightX + 36;
    const pu::i32 rightCardH = PANEL_Y + PANEL_H - PAD - rightCardY;
    auto rightCardBorder = pu::ui::elm::Rectangle::New(rightCardX, rightCardY, rightCardW, rightCardH, cardBorder, 26);
    auto rightCard = pu::ui::elm::Rectangle::New(rightCardX + 4, rightCardY + 4, rightCardW - 8, rightCardH - 8, cardBg, 24);
    this->Add(rightCardBorder);
    this->Add(rightCard);

    pu::i32 leftY = contentTop + 20;
    pu::i32 rightY = contentTop + 20;

    const auto t1 = this->pk->type1();
    const auto t2 = this->pk->type2();

    {
        auto lbl = pu::ui::elm::TextBlock::New(leftX, leftY, "Type");
        lbl->SetColor(TEXT_DARK);
        lbl->SetFont(pksm::ui::global::MakeHeavyFontName(pksm::ui::global::FONT_SIZE_BUTTON));
        this->Add(lbl);

        auto makeChip = [&](const pu::i32 x, const pu::i32 y, const std::string &text, const pu::ui::Color &bgColor) {
            auto tb = pu::ui::elm::TextBlock::New(0, 0, text);
            tb->SetColor(pu::ui::Color(255, 255, 255, 255));
            tb->SetFont(pksm::ui::global::MakeHeavyFontName(pksm::ui::global::FONT_SIZE_BUTTON));
            const pu::i32 w = tb->GetWidth() + 28;
            const pu::i32 h = tb->GetHeight() + 10;
            auto bg = pu::ui::elm::Rectangle::New(x, y, w, h, bgColor, h / 2);
            this->Add(bg);
            tb->SetX(x + 14);
            tb->SetY(y + 5);
            this->Add(tb);
            return w;
        };

        const pu::i32 chipY = leftY - 2;
        pu::i32 chipX = leftX + LABEL_W;
        chipX += makeChip(chipX, chipY, LocalizeType(lang, t1), TypeColor(t1)) + 12;
        if (t2 != t1) {
            makeChip(chipX, chipY, LocalizeType(lang, t2), TypeColor(t2));
        }

        leftY += lbl->GetHeight() + ROW_GAP;
    }

    addPair(leftX, leftY, "Nickname", LocalizedOrFallback(this->pk->nickname(), ""));
    addPair(leftX, leftY, "OT", LocalizedOrFallback(this->pk->otName(), ""));
    addPair(leftX, leftY, "Nature", LocalizeNature(lang, this->pk->nature()));
    addPair(leftX, leftY, "Ability", LocalizeAbility(lang, this->pk->ability()));
    addPair(leftX, leftY, "Item", LocalizeItem(lang, this->pk->heldItem()));
    addPair(leftX, leftY, "PSV/TSV", std::to_string(this->pk->PSV()) + "/" + std::to_string(this->pk->TSV()));
    addPair(leftX, leftY, "TID/SID", std::to_string(this->pk->TID()) + "/" + std::to_string(this->pk->SID()));
    addPair(leftX, leftY, "Friendship", std::to_string(this->pk->otFriendship()) + "/" + std::to_string(this->pk->htFriendship()));
    addPair(leftX, leftY, "Hidden Power", LocalizeType(lang, this->pk->hpType()));

    const pu::i32 statsBoxX = rightX;
    const pu::i32 statsBoxY = rightY;
    const pu::i32 statsBoxW = PANEL_X + PANEL_W - PAD - rightX;
    const pu::i32 statsBoxH = 262;

    auto statsHeader = pu::ui::elm::TextBlock::New(statsBoxX + 6, statsBoxY, "Stats (IV / EV / Stat)");
    statsHeader->SetColor(TEXT_DARK);
    statsHeader->SetFont(pksm::ui::global::MakeHeavyFontName(pksm::ui::global::FONT_SIZE_BUTTON));
    this->Add(statsHeader);

    const pu::i32 colIV = statsBoxX + 250;
    const pu::i32 colEV = statsBoxX + 330;
    const pu::i32 colST = statsBoxX + 430;
    auto addStatRow = [&](pu::i32 &y, const std::string &label, const pu::ui::Color &labelColor, const pksm::Stat st) {
        auto lbl = pu::ui::elm::TextBlock::New(statsBoxX + 16, y, label);
        lbl->SetColor(labelColor);
        lbl->SetFont(pksm::ui::global::MakeHeavyFontName(pksm::ui::global::FONT_SIZE_BUTTON));
        this->Add(lbl);

        auto ivT = pu::ui::elm::TextBlock::New(colIV, y, std::to_string(this->pk->iv(st)));
        ivT->SetColor(TEXT_DARK);
        ivT->SetFont(pksm::ui::global::MakeHeavyFontName(pksm::ui::global::FONT_SIZE_BUTTON));
        this->Add(ivT);

        auto evT = pu::ui::elm::TextBlock::New(colEV, y, std::to_string(this->pk->ev(st)));
        evT->SetColor(TEXT_DARK);
        evT->SetFont(pksm::ui::global::MakeHeavyFontName(pksm::ui::global::FONT_SIZE_BUTTON));
        this->Add(evT);

        auto stT = pu::ui::elm::TextBlock::New(colST, y, std::to_string(this->pk->stat(st)));
        stT->SetColor(TEXT_DARK);
        stT->SetFont(pksm::ui::global::MakeHeavyFontName(pksm::ui::global::FONT_SIZE_BUTTON));
        this->Add(stT);

        y += std::max({lbl->GetHeight(), ivT->GetHeight(), evT->GetHeight(), stT->GetHeight()}) + 10;
    };

    pu::i32 statY = statsBoxY + 38;
    addStatRow(statY, "HP", TEXT_DARK, pksm::Stat::HP);
    addStatRow(statY, "Attack", TEXT_DARK, pksm::Stat::ATK);
    addStatRow(statY, "Defense", TEXT_DARK, pksm::Stat::DEF);
    addStatRow(statY, "Sp. Atk", pu::ui::Color(40, 92, 220, 255), pksm::Stat::SPATK);
    addStatRow(statY, "Sp. Def", pu::ui::Color(220, 70, 70, 255), pksm::Stat::SPDEF);
    addStatRow(statY, "Speed", TEXT_DARK, pksm::Stat::SPD);

    const pu::i32 movesBoxX = rightX;
    const pu::i32 movesBoxY = statsBoxY + statsBoxH + 20;
    auto movesHeader = pu::ui::elm::TextBlock::New(movesBoxX + 6, movesBoxY, "Moves");
    movesHeader->SetColor(TEXT_DARK);
    movesHeader->SetFont(pksm::ui::global::MakeHeavyFontName(pksm::ui::global::FONT_SIZE_BUTTON));
    this->Add(movesHeader);

    pu::i32 mvY = movesBoxY + 36;
    for (int i = 0; i < 4; i++) {
        const auto mv = this->pk->move(static_cast<u8>(i));
        auto bullet = pu::ui::elm::TextBlock::New(movesBoxX + 6, mvY, "-");
        bullet->SetColor(TEXT_DARK);
        bullet->SetFont(pksm::ui::global::MakeHeavyFontName(pksm::ui::global::FONT_SIZE_BUTTON));
        this->Add(bullet);

        auto mvText = pu::ui::elm::TextBlock::New(movesBoxX + 30, mvY, LocalizeMove(lang, mv));
        mvText->SetColor(TEXT_DARK);
        mvText->SetFont(pksm::ui::global::MakeHeavyFontName(pksm::ui::global::FONT_SIZE_BUTTON));
        this->Add(mvText);
        mvY += mvText->GetHeight() + 12;
    }

    auto hint = pu::ui::elm::TextBlock::New(PANEL_X + PANEL_W - 260, PANEL_Y + PANEL_H - 54, "B  Back");
    hint->SetColor(TEXT_DARK);
    hint->SetFont(pksm::ui::global::MakeSwitchButtonFontName(pksm::ui::global::FONT_SIZE_BUTTON));
    this->Add(hint);
}

}  // namespace pksm::ui