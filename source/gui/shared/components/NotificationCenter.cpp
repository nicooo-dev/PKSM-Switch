#include "gui/shared/components/NotificationCenter.hpp"

#include <algorithm>
#include <sstream>

#include "gui/shared/UIConstants.hpp"

namespace pksm::ui {

NotificationCenter::NotificationCenter(const pu::i32 x, const pu::i32 y, const pu::i32 width)
  : Element(), x(x), y(y), width(width) {}

pu::i32 NotificationCenter::GetX() {
    return x;
}

pu::i32 NotificationCenter::GetY() {
    return y;
}

pu::i32 NotificationCenter::GetWidth() {
    return width;
}

pu::i32 NotificationCenter::GetHeight() {
    const pu::i32 count = static_cast<pu::i32>(active.size());
    if (count <= 0) {
        return 0;
    }
    return (count * TOAST_HEIGHT) + ((count - 1) * TOAST_SPACING);
}

void NotificationCenter::OnInput(
    const u64 keys_down,
    const u64 keys_up,
    const u64 keys_held,
    const pu::ui::TouchPoint touch_pos
) {
    (void)keys_down;
    (void)keys_up;
    (void)keys_held;
    (void)touch_pos;
}

static int ClampInt(const int v, const int lo, const int hi) {
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

static std::vector<std::string> WrapTextToWidth(
    const std::string &text,
    const std::string &font,
    const pu::ui::Color clr,
    const pu::i32 max_width
) {
    std::vector<std::string> lines;
    if (text.empty()) {
        return lines;
    }

    std::istringstream iss(text);
    std::string word;
    std::string current;

    auto measureWidth = [&](const std::string &s) -> pu::i32 {
        auto tb = pu::ui::elm::TextBlock::New(0, 0, s);
        tb->SetFont(font);
        tb->SetColor(clr);
        return tb->GetWidth();
    };

    while (iss >> word) {
        const std::string candidate = current.empty() ? word : (current + " " + word);
        if (measureWidth(candidate) <= max_width) {
            current = candidate;
        }
        else {
            if (!current.empty()) {
                lines.push_back(current);
            }
            // if a word is too long, force it into its own line.
            current = word;
        }
    }

    if (!current.empty()) {
        lines.push_back(current);
    }

    return lines;
}

void NotificationCenter::PollNewToasts() {
    auto newToasts = utils::NotificationManager::ConsumeAll();
    if (newToasts.empty()) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    for (const auto &t : newToasts) {
        if (t.title.empty() && t.body.empty()) {
            continue;
        }
        active.push_front(Toast{t.title, t.body, now});
    }

    while (static_cast<int>(active.size()) > MAX_TOASTS) {
        active.pop_back();
    }
}

void NotificationCenter::TrimExpired() {
    const auto now = std::chrono::steady_clock::now();

    auto isExpired = [&](const Toast &t) {
        const auto age_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - t.created).count();
        return age_ms > (SLIDE_MS + DISPLAY_MS + FADE_MS);
    };

    while (!active.empty() && isExpired(active.back())) {
        active.pop_back();
    }
}

void NotificationCenter::OnRender(pu::ui::render::Renderer::Ref& drawer, const pu::i32 base_x, const pu::i32 base_y) {
    PollNewToasts();
    TrimExpired();

    if (active.empty()) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();

    const pu::i32 screenW = static_cast<pu::i32>(pu::ui::render::ScreenWidth);
    const pu::i32 screenH = static_cast<pu::i32>(pu::ui::render::ScreenHeight);

    const pu::i32 toastW = width;
    const pu::i32 originX = screenW - MARGIN_RIGHT - toastW;

    const pu::ui::Color baseTextColor = pksm::ui::global::TEXT_WHITE;
    const std::string titleFont = pksm::ui::global::MakeHeavyFontName(pksm::ui::global::FONT_SIZE_BUTTON);
    const std::string bodyFont = pksm::ui::global::MakeMediumFontName(pksm::ui::global::FONT_SIZE_BUTTON);
    const pu::i32 bodyMaxWidth = toastW - (TOAST_PADDING_X * 2) - (BODY_BOX_PADDING * 2);
    const pu::i32 lineHeight = 26;

    struct ToastRenderInfo {
        std::vector<std::string> wrapped;
        pu::i32 height;
    };

    std::vector<ToastRenderInfo> infos;
    infos.reserve(active.size());
    pu::i32 stackHeight = 0;
    for (const auto &toast : active) {
        const pu::ui::Color measureColor = pu::ui::Color(baseTextColor.r, baseTextColor.g, baseTextColor.b, 0xFF);
        const auto wrapped = WrapTextToWidth(toast.body, bodyFont, measureColor, bodyMaxWidth);
        const pu::i32 wrappedH = wrapped.empty() ? 0 : (static_cast<pu::i32>(wrapped.size()) * lineHeight) + ((static_cast<pu::i32>(wrapped.size()) - 1) * BODY_LINE_SPACING);
        const pu::i32 baseContentH = TOAST_PADDING_Y + TITLE_HEIGHT + TITLE_DIVIDER_OFFSET + DIVIDER_HEIGHT + BODY_BOX_PADDING + BODY_BOX_PADDING + TOAST_PADDING_Y;
        const pu::i32 desiredToastH = std::min<pu::i32>(MAX_TOAST_HEIGHT, std::max<pu::i32>(MIN_TOAST_HEIGHT, baseContentH + wrappedH));
        infos.push_back(ToastRenderInfo{wrapped, desiredToastH});
        stackHeight += desiredToastH;
    }
    if (!infos.empty()) {
        stackHeight += static_cast<pu::i32>(infos.size() - 1) * TOAST_SPACING;
    }

    const pu::i32 originY = screenH - MARGIN_BOTTOM - stackHeight;

    pu::i32 idx = 0;
    pu::i32 yCursor = originY;
    for (const auto &toast : active) {
        const auto age_ms_ll = std::chrono::duration_cast<std::chrono::milliseconds>(now - toast.created).count();
        const int age_ms = ClampInt(static_cast<int>(age_ms_ll), 0, 60'000);

        float slide_t = 1.0f;
        if (age_ms < SLIDE_MS) {
            slide_t = static_cast<float>(age_ms) / static_cast<float>(SLIDE_MS);
        }

        float fade_t = 1.0f;
        const int fade_start = SLIDE_MS + DISPLAY_MS;
        if (age_ms >= fade_start) {
            const int fade_age = age_ms - fade_start;
            fade_t = 1.0f - (static_cast<float>(ClampInt(fade_age, 0, FADE_MS)) / static_cast<float>(FADE_MS));
        }

        const u8 alpha = static_cast<u8>(ClampInt(static_cast<int>(255.0f * fade_t), 0, 255));

        const auto &info = infos.at(static_cast<size_t>(idx));
        const pu::i32 desiredToastH = info.height;
        const pu::i32 targetY = yCursor;

        const pu::i32 slideOffset = static_cast<pu::i32>((1.0f - slide_t) * 44.0f);
        const pu::i32 drawX = originX + slideOffset;
        const pu::i32 drawY = targetY;

        const pu::ui::Color bg = pu::ui::Color(20, 20, 28, alpha);
        const pu::ui::Color shadow = pu::ui::Color(0, 0, 0, static_cast<u8>(alpha / 3));
        const pu::ui::Color divider = pu::ui::Color(255, 255, 255, static_cast<u8>(alpha / 4));
        const pu::ui::Color bodyBg = pu::ui::Color(255, 255, 255, static_cast<u8>(alpha / 14));

        const pu::ui::Color textColor = pu::ui::Color(baseTextColor.r, baseTextColor.g, baseTextColor.b, alpha);

        drawer->RenderRoundedRectangleFill(
            shadow,
            base_x + drawX + 4,
            base_y + drawY + 6,
            toastW,
            desiredToastH,
            TOAST_CORNER_RADIUS
        );
        drawer->RenderRoundedRectangleFill(
            bg,
            base_x + drawX,
            base_y + drawY,
            toastW,
            desiredToastH,
            TOAST_CORNER_RADIUS
        );

        auto titleText = pu::ui::elm::TextBlock::New(0, 0, toast.title.empty() ? std::string("Notification") : toast.title);
        titleText->SetColor(textColor);
        titleText->SetFont(titleFont);
        titleText->SetX(base_x + drawX + TOAST_PADDING_X);
        titleText->SetY(base_y + drawY + TOAST_PADDING_Y);
        titleText->OnRender(drawer, titleText->GetProcessedX(), titleText->GetProcessedY());

        const pu::i32 dividerY = base_y + drawY + TOAST_PADDING_Y + TITLE_HEIGHT + TITLE_DIVIDER_OFFSET;
        drawer->RenderRectangleFill(divider, base_x + drawX + TOAST_PADDING_X, dividerY, toastW - (TOAST_PADDING_X * 2), DIVIDER_HEIGHT);

        const pu::i32 bodyBoxY = dividerY + DIVIDER_HEIGHT + BODY_BOX_PADDING;
        const pu::i32 bodyBoxH = desiredToastH - (bodyBoxY - (base_y + drawY)) - TOAST_PADDING_Y;
        drawer->RenderRoundedRectangleFill(
            bodyBg,
            base_x + drawX + TOAST_PADDING_X,
            bodyBoxY,
            toastW - (TOAST_PADDING_X * 2),
            bodyBoxH,
            BODY_CORNER_RADIUS
        );

        if (!info.wrapped.empty()) {
            pu::i32 lineY = bodyBoxY + BODY_BOX_PADDING;
            for (const auto &line : info.wrapped) {
                auto bodyText = pu::ui::elm::TextBlock::New(0, 0, line);
                bodyText->SetColor(textColor);
                bodyText->SetFont(bodyFont);
                bodyText->SetX(base_x + drawX + TOAST_PADDING_X + BODY_BOX_PADDING);
                bodyText->SetY(lineY);
                bodyText->OnRender(drawer, bodyText->GetProcessedX(), bodyText->GetProcessedY());
                lineY += lineHeight + BODY_LINE_SPACING;
            }
        }

        yCursor += desiredToastH + TOAST_SPACING;
        idx++;
    }
}

}  // namespace pksm::ui