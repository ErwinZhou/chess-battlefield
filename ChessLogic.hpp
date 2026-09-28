#pragma once

#include <array>
#include <cstdint>
#include <list>

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
    Team choose_team() const;
    bool try_spawn(Player &player);
    Player *spawn_player();
    void remove_player(Player *player);
    void update(float elapsed);
};
} // namespace chess
