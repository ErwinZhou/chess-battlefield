#pragma once
#include "ChessLogic.hpp"
#include <algorithm>
#include <cmath>
#include <optional>

// Drawing uses drawable pixels; SDL mouse events use window coordinates.
struct BoardLayout {
    int cell, side, left, bottom;
    static BoardLayout fit(int width, int height) {
        int cell = std::max(1, std::min(width, height) / 10);
        return {cell, cell * 8, (width - cell * 8) / 2, (height - cell * 8) / 2};
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
