#pragma once

#include <array>
#include <cstdint>
#include <list>
#include <optional>
#include <random>

namespace chess {
enum class Pieces : uint8_t { Pawn = 0, Knight, Bishop, Rook, Queen, King };
enum class Team : uint8_t { A = 0, B = 1 };
enum class RoundPhase : uint8_t { Playing = 0, Break = 1 };
enum class PlayerStatus : uint8_t { Waiting = 0, Alive = 1, Dead = 2 };

struct Position {
    int row = -1;
    int col = -1;
};
struct Player {
    uint32_t id = 0;
    Team team = Team::A;
    Pieces piece = Pieces::Knight; // free starting piece; players can travel in every direction
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
    bool alive = true;
    float cooldown = 1.0f;
};

// Pure gameplay state: no sockets, window, or graphics dependencies.
struct ChessLogic {
    std::list<Player> players; // stable addresses for server connections
    std::array<King, 2> kings;
    uint32_t next_player_id = 1;
    RoundPhase phase = RoundPhase::Playing;
    std::optional<Team> winner;
    bool selection_open = false;
    std::optional<Pieces> selected_piece; // applied at next round
    uint32_t capturer_id = 0; // 0 when an AI king wins the round
    float break_remaining = 0.0f;
    uint32_t round_number = 1;
    std::mt19937 rng{0x15466666};

    ChessLogic();
    static bool in_bounds(Position pos);
    bool occupied(Position pos) const;
    std::optional<Team> team_at(Position pos) const;
    bool legal_move(Pieces piece, Team team, Position from, Position to) const;
    bool move_player(uint32_t id, Position destination);
    static constexpr float MoveCooldown = 1.0f;
    static constexpr float RespawnDelay = 3.0f;
    static constexpr float RoundBreak = 5.0f;
    static uint32_t capture_value(Pieces piece);
    Team choose_team() const;
    bool try_spawn(Player &player);
    Player *spawn_player();
    void remove_player(Player *player);
    void update(float elapsed); // timers only; server runs kings after player requests
    void update_kings();
    void reset_round();
    bool select_piece(uint32_t player_id, uint32_t round, Pieces piece);
private:
    void capture_player(Player &victim);
    void finish_round(Team team, uint32_t capturer);
};
} // namespace chess
