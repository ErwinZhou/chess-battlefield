#include "ChessLogic.hpp"

#include <limits>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace chess {
ChessLogic::ChessLogic() : kings{{{Team::A, {0, 4}}, {Team::B, {7, 4}}}} {}

bool ChessLogic::in_bounds(Position pos) {
    return pos.row >= 0 && pos.row < 8 && pos.col >= 0 && pos.col < 8;
}

std::optional<Team> ChessLogic::team_at(Position pos) const {
    auto same = [pos](Position other) { return pos.row == other.row && pos.col == other.col; };
    for (auto const &king : kings) if (same(king.pos)) return king.team;
    for (auto const &player : players)
        if (player.status == PlayerStatus::Alive && same(player.pos)) return player.team;
    return std::nullopt;
}

bool ChessLogic::occupied(Position pos) const { return team_at(pos).has_value(); }

bool ChessLogic::legal_move(Pieces piece, Team team, Position from, Position to) const {
    if (!in_bounds(from) || !in_bounds(to)) return false;
    int dr = to.row - from.row, dc = to.col - from.col;
    int ar = std::abs(dr), ac = std::abs(dc);
    if (ar == 0 && ac == 0) return false;
    auto target = team_at(to);
    if (target && *target == team) return false;
    switch (piece) {
        case Pieces::Pawn: {
            int forward = team == Team::A ? 1 : -1;
            return dr == forward && ((dc == 0 && !target) || (ac == 1 && target));
        }
        case Pieces::Knight: return (ar == 2 && ac == 1) || (ar == 1 && ac == 2);
        case Pieces::King: return std::max(ar, ac) == 1;
        case Pieces::Rook: if (dr != 0 && dc != 0) return false; break;
        case Pieces::Bishop: if (ar != ac) return false; break;
        case Pieces::Queen: if (dr != 0 && dc != 0 && ar != ac) return false; break;
        default: return false;
    }
    int sr = (dr > 0) - (dr < 0), sc = (dc > 0) - (dc < 0);
    for (Position p{from.row + sr, from.col + sc}; p.row != to.row || p.col != to.col;
         p.row += sr, p.col += sc) {
        if (occupied(p)) return false;
    }
    return true;
}

bool ChessLogic::move_player(uint32_t id, Position destination) {
    for (auto &player : players) {
        if (player.id != id) continue;
        if (player.status != PlayerStatus::Alive || player.cooldown > 0.0f) return false;
        if (!legal_move(player.piece, player.team, player.pos, destination)) return false;
        // King capture and round transitions belong to Stage 4.
        for (auto const &king : kings)
            if (king.pos.row == destination.row && king.pos.col == destination.col) return false;
        for (auto &victim : players) {
            if (victim.status != PlayerStatus::Alive || victim.pos.row != destination.row || victim.pos.col != destination.col) continue;
            uint32_t reward = capture_value(victim.piece);
            player.points += std::min(reward, std::numeric_limits<uint32_t>::max() - player.points);
            victim.status = PlayerStatus::Dead;
            victim.pos = {-1, -1};
            victim.cooldown = 0.0f;
            victim.respawn_remaining = RespawnDelay;
            ++victim.life;
            break;
        }
        player.pos = destination;
        player.cooldown = MoveCooldown;
        return true;
    }
    return false;
}

uint32_t ChessLogic::capture_value(Pieces piece) {
    switch (piece) {
        case Pieces::Pawn: return 1;
        case Pieces::Knight: case Pieces::Bishop: return 3;
        case Pieces::Rook: case Pieces::King: return 5;
        case Pieces::Queen: return 9;
    }
    return 0;
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
        ++player.life;
        player.cooldown = MoveCooldown;
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

void ChessLogic::update(float elapsed) {
    if (!std::isfinite(elapsed) || elapsed < 0.0f) return;
    for (auto &player : players) {
        player.cooldown = std::max(0.0f, player.cooldown - elapsed);
        if (player.status == PlayerStatus::Dead) {
            player.respawn_remaining = std::max(0.0f, player.respawn_remaining - elapsed);
            if (player.respawn_remaining == 0.0f) player.status = PlayerStatus::Waiting;
        }
        try_spawn(player);
    }
}
} // namespace chess
