#pragma once

#include <array>
#include <cstdint>
#include <list>
#include <optional>

namespace chess {
enum class Pieces : uint8_t { Pawn = 0, Knight, Bishop, Rook, Queen, King };
enum class Team : uint8_t { A = 0, B = 1 };
enum class PlayerStatus : uint8_t { Waiting = 0, Alive = 1, Dead = 2 };

struct Position {
    int row = -1;
    int col = -1;
};
struct Player {
    uint32_t id = 0;
    Team team = Team::A;
    Pieces piece = Pieces::Pawn;
    PlayerStatus status = PlayerStatus::Waiting;
    Position pos;
    float cooldown = 0.0f;
    uint32_t points = 0;
    float respawn_remaining = 0.0f;
    uint32_t life = 0; // changes on capture and spawn to invalidate stale requests
};
struct King {
    Team team = Team::A;
    Position pos;
};

// Pure gameplay state: no sockets, window, or graphics dependencies.
struct ChessLogic {
    std::list<Player> players; // stable addresses for server connections
    std::array<King, 2> kings;
    uint32_t next_player_id = 1;

    ChessLogic();
    static bool in_bounds(Position pos);
    bool occupied(Position pos) const;
    std::optional<Team> team_at(Position pos) const;
    bool legal_move(Pieces piece, Team team, Position from, Position to) const;
    bool move_player(uint32_t id, Position destination);
    static constexpr float MoveCooldown = 1.0f;
    static constexpr float RespawnDelay = 3.0f;
    static uint32_t capture_value(Pieces piece);
    Team choose_team() const;
    bool try_spawn(Player &player);
    Player *spawn_player();
    void remove_player(Player *player);
    void update(float elapsed);
};
} // namespace chess
