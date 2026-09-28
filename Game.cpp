#include "Game.hpp"
#include "Connection.hpp"

#include <limits>
#include <cmath>
#include <stdexcept>
#include <unordered_set>
#include <utility>

void Game::send_state_message(Connection *connection, chess::Player const *recipient) const {
    if (logic.players.size() > std::numeric_limits<uint16_t>::max())
        throw std::runtime_error("Too many players for snapshot");
    auto write = [&](uint32_t value, unsigned bytes) {
        for (unsigned i = 0; i < bytes; ++i) connection->send(uint8_t(value >> (8 * i)));
    };
    write(uint8_t(Message::S2C_State), 1);
    write(10 + 11 * uint32_t(logic.players.size()), 3);
    write(recipient ? recipient->id : 0, 4);
    write(uint32_t(logic.players.size()), 2);
    for (auto const &player : logic.players) {
        write(player.id, 4);
        write(uint8_t(player.team), 1);
        write(uint8_t(player.piece), 1);
        write(uint8_t(player.status), 1);
        write(uint8_t(player.pos.row), 1);
        write(uint8_t(player.pos.col), 1);
        write(uint32_t(std::ceil(player.cooldown * 1000.0f)), 2);
    }
    for (auto const &king : logic.kings) {
        write(uint8_t(king.pos.row), 1);
        write(uint8_t(king.pos.col), 1);
    }
}

bool Game::recv_state_message(Connection *connection) {
    auto &buffer = connection->recv_buffer;
    if (buffer.size() < 4) return false;
    if (buffer[0] != uint8_t(Message::S2C_State)) return false;
    uint32_t size = uint32_t(buffer[1]) | (uint32_t(buffer[2]) << 8) | (uint32_t(buffer[3]) << 16);
    if (size < 10 || size > 10 + 11 * uint32_t(std::numeric_limits<uint16_t>::max()) || (size - 10) % 11)
        throw std::runtime_error("Invalid snapshot size");
    if (buffer.size() < 4 + size) return false;
    size_t at = 4;
    auto read = [&](unsigned bytes) {
        if (at + bytes > 4 + size) throw std::runtime_error("Truncated snapshot field");
        uint32_t value = 0;
        for (unsigned i = 0; i < bytes; ++i) value |= uint32_t(buffer[at++]) << (8 * i);
        return value;
    };
    uint32_t recipient = read(4);
    uint32_t count = read(2);
    if (size != 10 + 11 * count) throw std::runtime_error("Snapshot count/size mismatch");
    std::list<chess::Player> players;
    std::array<chess::King, 2> kings;
    std::unordered_set<uint32_t> ids;
    std::array<bool, 64> occupied{};
    auto claim = [&](chess::Position pos) {
        if (!chess::ChessLogic::in_bounds(pos)) throw std::runtime_error("Invalid board coordinate");
        auto &cell = occupied[pos.row * 8 + pos.col];
        if (cell) throw std::runtime_error("Overlapping snapshot pieces");
        cell = true;
    };
    for (uint32_t i = 0; i < count; ++i) {
        chess::Player player;
        player.id = read(4);
        auto team = read(1), piece = read(1), status = read(1);
        auto row = read(1), col = read(1);
        auto cooldown_ms = read(2);
        if (cooldown_ms > 1000) throw std::runtime_error("Invalid cooldown");
        player.cooldown = float(cooldown_ms) / 1000.0f;
        if (!player.id || !ids.insert(player.id).second || team > 1 || piece >= uint8_t(chess::Pieces::King) || status > 1)
            throw std::runtime_error("Invalid snapshot player");
        player.team = chess::Team(team);
        player.piece = chess::Pieces(piece);
        player.status = chess::PlayerStatus(status);
        if (player.status == chess::PlayerStatus::Alive) {
            player.pos = {int(row), int(col)};
            claim(player.pos);
        } else if (row != 255 || col != 255) {
            throw std::runtime_error("Invalid waiting position");
        }
        players.push_back(player);
    }
    for (size_t i = 0; i < kings.size(); ++i) {
        kings[i].team = chess::Team(i);
        int row = int(read(1)), col = int(read(1));
        kings[i].pos = {row, col};
        claim(kings[i].pos);
    }
    if (recipient && !ids.count(recipient)) throw std::runtime_error("Missing local player");
    // Commit only a fully validated frame. Never apply this on the live server.
    logic.players = std::move(players);
    logic.kings = kings;
    local_player_id = recipient;
    buffer.erase(buffer.begin(), buffer.begin() + 4 + size);
    return true;
}

void Game::send_move_message(Connection *connection, chess::Position destination) {
    if (!chess::ChessLogic::in_bounds(destination)) return;
    for (uint8_t byte : {uint8_t(Message::C2S_Move), uint8_t(2), uint8_t(0), uint8_t(0),
                         uint8_t(destination.row), uint8_t(destination.col)}) connection->send(byte);
}

bool Game::recv_move_message(Connection *connection, uint32_t player_id) {
    auto &buffer = connection->recv_buffer;
    if (buffer.empty()) return false;
    if (buffer[0] != uint8_t(Message::C2S_Move)) throw std::runtime_error("Unknown client message");
    if (buffer.size() < 4) return false;
    if (buffer[1] != 2 || buffer[2] != 0 || buffer[3] != 0) throw std::runtime_error("Invalid move size");
    if (buffer.size() < 6) return false;
    logic.move_player(player_id, {buffer[4], buffer[5]});
    buffer.erase(buffer.begin(), buffer.begin() + 6);
    return true; // consumed, even when the move is illegal
}
