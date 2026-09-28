#pragma once
#include "ChessLogic.hpp"

struct Connection;
enum class Message : uint8_t { S2C_State = 's' };

// Network adapter. Server advances logic; clients only install snapshots.
struct Game {
    chess::ChessLogic logic;
    uint32_t local_player_id = 0;
    static constexpr float Tick = 1.0f / 30.0f;

    chess::Player *spawn_player() { return logic.spawn_player(); }
    void remove_player(chess::Player *player) { logic.remove_player(player); }
    void update(float elapsed) { logic.update(elapsed); }

    // False means incomplete input or a different message type; no state changes.
    bool recv_state_message(Connection *connection);
    void send_state_message(Connection *connection, chess::Player const *recipient = nullptr) const;
};
