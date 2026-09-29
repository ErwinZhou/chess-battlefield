#pragma once
#include "ChessLogic.hpp"

struct Connection;
enum class Message : uint8_t { C2S_Move = 'm', S2C_State = 's' };

// Network adapter. Server advances logic; clients only install snapshots.
struct Game {
    chess::ChessLogic logic;
    uint32_t local_player_id = 0;
    static constexpr float Tick = 1.0f / 30.0f;

    chess::Player *spawn_player() { return logic.spawn_player(); }
    void remove_player(chess::Player *player) { logic.remove_player(player); }
    void update(float elapsed) { logic.update(elapsed); }

    // False means incomplete input or a different message type; no state changes.
    static void send_move_message(Connection *connection, chess::Position destination, uint32_t life);
    bool recv_move_message(Connection *connection, uint32_t player_id);
    bool recv_state_message(Connection *connection);
    void send_state_message(Connection *connection, chess::Player const *recipient = nullptr) const;
};
