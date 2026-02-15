#pragma once

#include <chrono>
#include <deque>
#include <pu/Plutonium>
#include <string>

#include "utils/NotificationManager.hpp"

namespace pksm::ui {

class NotificationCenter : public pu::ui::elm::Element {
private:
    struct Toast {
        std::string title;
        std::string body;
        std::chrono::steady_clock::time_point created;
    };

    pu::i32 x;
    pu::i32 y;
    pu::i32 width;

    std::deque<Toast> active;

public:
    static constexpr pu::i32 TOAST_HEIGHT = 104;
    static constexpr pu::i32 TOAST_SPACING = 12;
    static constexpr pu::i32 TOAST_PADDING_X = 18;
    static constexpr pu::i32 TOAST_PADDING_Y = 12;
    static constexpr pu::i32 TOAST_CORNER_RADIUS = 18;

    static constexpr pu::i32 TITLE_HEIGHT = 30;
    static constexpr pu::i32 TITLE_DIVIDER_OFFSET = 10;
    static constexpr pu::i32 DIVIDER_HEIGHT = 2;
    static constexpr pu::i32 BODY_BOX_PADDING = 14;
    static constexpr pu::i32 BODY_CORNER_RADIUS = 14;
    static constexpr pu::i32 BODY_LINE_SPACING = 6;

    static constexpr pu::i32 MIN_TOAST_HEIGHT = 104;
    static constexpr pu::i32 MAX_TOAST_HEIGHT = 600;

    static constexpr pu::i32 MARGIN_RIGHT = 36;
    static constexpr pu::i32 MARGIN_BOTTOM = 120;

    static constexpr int MAX_TOASTS = 3;

    static constexpr int SLIDE_MS = 220;
    static constexpr int DISPLAY_MS = 2400;
    static constexpr int FADE_MS = 260;

    NotificationCenter(const pu::i32 x, const pu::i32 y, const pu::i32 width);
    PU_SMART_CTOR(NotificationCenter)

    pu::i32 GetX() override;
    pu::i32 GetY() override;
    pu::i32 GetWidth() override;
    pu::i32 GetHeight() override;

    void OnRender(pu::ui::render::Renderer::Ref& drawer, const pu::i32 x, const pu::i32 y) override;
    void OnInput(const u64 keys_down, const u64 keys_up, const u64 keys_held, const pu::ui::TouchPoint touch_pos) override;

private:
    void PollNewToasts();
    void TrimExpired();
};

}  // namespace pksm::ui