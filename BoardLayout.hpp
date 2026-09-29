#pragma once
#include "ChessLogic.hpp"
#include <algorithm>
#include <cmath>
#include <optional>

// Drawing uses drawable pixels; SDL mouse events use window coordinates.
struct BoardLayout {
    int cell, side, left, bottom;
    int panel_left, panel_width;
    static constexpr float TextLineSpacing = 1.65f;
    float text_size(int height) const {
        return std::min(float(panel_width) / 12.0f, float(height) / 24.0f);
    }
    float menu_baseline(int row, int height) const {
        // Team/type/points, gap, winner/countdown, then the menu heading.
        return float(bottom + side) - text_size(height) * (2.0f + (6 + row) * TextLineSpacing);
    }
    static BoardLayout fit(int width, int height) {
        // Reserve the right side for readable status; hit testing uses this same rectangle.
        int board_region = width * 69 / 100;
        int cell = std::max(1, std::min(width * 65 / 100, height * 86 / 100) / 8);
        return {cell, cell * 8, (board_region - cell * 8) / 2, (height - cell * 8) / 2,
                width * 71 / 100, width * 26 / 100};
    }
    static std::optional<chess::Position> hit(float x, float y, int window_w, int window_h,
                                               int drawable_w, int drawable_h) {
        if (window_w <= 0 || window_h <= 0 || drawable_w <= 0 || drawable_h <= 0 ||
            !std::isfinite(x) || !std::isfinite(y)) return std::nullopt;
        auto board = fit(drawable_w, drawable_h);
        double px = double(x) * drawable_w / window_w - board.left;
        double py = double(y) * drawable_h / window_h - (drawable_h - board.bottom - board.side);
        if (px < 0 || py < 0 || px >= board.side || py >= board.side) return std::nullopt;
        return chess::Position{7 - int(py / board.cell), int(px / board.cell)};
    }
};
