#include "ChessLogic.hpp"

#include <limits>
#include <stdexcept>

namespace chess {
ChessLogic::ChessLogic() : kings{{{Team::A, {0, 4}}, {Team::B, {7, 4}}}} {}

bool ChessLogic::in_bounds(Position pos) {
    return pos.row >= 0 && pos.row < 8 && pos.col >= 0 && pos.col < 8;
}

bool ChessLogic::occupied(Position pos) const {
    auto same = [pos](Position other) { return pos.row == other.row && pos.col == other.col; };
    for (auto const &king : kings) if (same(king.pos)) return true;
    for (auto const &player : players)
        if (player.status == PlayerStatus::Alive && same(player.pos)) return true;
    return false;
}

Team ChessLogic::choose_team() const {
    size_t a = 0, b = 0;
    for (auto const &player : players) (player.team == Team::A ? a : b)++;
    return a <= b ? Team::A : Team::B;
}

bool ChessLogic::try_spawn(Player &player) {
    if (player.status != PlayerStatus::Waiting) return false;
    auto rows = player.team == Team::A ? std::array<int, 2>{1, 0} : std::array<int, 2>{6, 7};
    for (int row : rows) for (int col = 0; col < 8; ++col) {
        if (occupied({row, col})) continue;
        player.pos = {row, col};
        player.status = PlayerStatus::Alive;
        return true;
    }
    player.pos = {-1, -1};
    return false;
}

Player *ChessLogic::spawn_player() {
    if (players.size() >= std::numeric_limits<uint16_t>::max())
        throw std::runtime_error("Player capacity reached");
    if (next_player_id == 0) throw std::runtime_error("Player IDs exhausted");
    Team team = choose_team();
    players.emplace_back();
    auto &player = players.back();
    player.id = next_player_id++;
    player.team = team;
    try_spawn(player);
    return &player;
}

void ChessLogic::remove_player(Player *player) {
    for (auto it = players.begin(); it != players.end(); ++it) {
        if (&*it == player) { players.erase(it); return; }
    }
}

void ChessLogic::update(float) {
    for (auto &player : players) try_spawn(player);
}
} // namespace chess
