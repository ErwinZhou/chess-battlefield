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
    write(26 + 21 * uint32_t(logic.players.size()), 3);
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
        write(player.points, 4);
        write(uint32_t(std::ceil(player.respawn_remaining * 1000.0f)), 2);
        write(player.life, 4);
    }
    for (auto const &king : logic.kings) {
        write(uint8_t(king.pos.row), 1);
        write(uint8_t(king.pos.col), 1);
        write(king.alive ? 1 : 0, 1);
    }
    write(uint8_t(logic.phase), 1);
    write(logic.winner ? uint8_t(*logic.winner) : 255, 1);
    write(logic.capturer_id, 4);
    write(uint32_t(std::ceil(logic.break_remaining * 1000.0f)), 2);
    write(logic.round_number, 4);
    write(logic.selection_open ? 1 : 0, 1);
    write(logic.selected_piece ? uint8_t(*logic.selected_piece) : 255, 1);
}

bool Game::recv_state_message(Connection *connection) {
    auto &buffer = connection->recv_buffer;
    if (buffer.size() < 4) return false;
    if (buffer[0] != uint8_t(Message::S2C_State)) return false;
    uint32_t size = uint32_t(buffer[1]) | (uint32_t(buffer[2]) << 8) | (uint32_t(buffer[3]) << 16);
    if (size < 26 || size > 26 + 21 * uint32_t(std::numeric_limits<uint16_t>::max()) || (size - 26) % 21)
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
    if (size != 26 + 21 * count) throw std::runtime_error("Snapshot count/size mismatch");
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
        player.points = read(4);
        auto respawn_ms = read(2);
        player.respawn_remaining = float(respawn_ms) / 1000.0f;
        player.life = read(4);
        if (cooldown_ms > 1000) throw std::runtime_error("Invalid cooldown");
        player.cooldown = float(cooldown_ms) / 1000.0f;
        if (!player.id || !ids.insert(player.id).second || team > 1 || piece >= uint8_t(chess::Pieces::King) || status > 2)
            throw std::runtime_error("Invalid snapshot player");
        if (respawn_ms > 3000 || (status != uint8_t(chess::PlayerStatus::Dead) && respawn_ms != 0) ||
            (status == uint8_t(chess::PlayerStatus::Dead) && respawn_ms == 0) ||
            (status != uint8_t(chess::PlayerStatus::Alive) && cooldown_ms != 0))
            throw std::runtime_error("Invalid lifecycle timers");
        player.team = chess::Team(team);
        player.piece = chess::Pieces(piece);
        player.status = chess::PlayerStatus(status);
        if (player.status == chess::PlayerStatus::Alive) {
            player.pos = {int(row), int(col)};
            claim(player.pos);
        } else if (row != 255 || col != 255) {
            throw std::runtime_error("Invalid off-board position");
        }
        players.push_back(player);
    }
    for (size_t i = 0; i < kings.size(); ++i) {
        kings[i].team = chess::Team(i);
        int row = int(read(1)), col = int(read(1));
        auto alive = read(1);
        if (alive > 1) throw std::runtime_error("Invalid king state");
        kings[i].alive = alive != 0;
        if (kings[i].alive) {
            kings[i].pos = {row, col};
            claim(kings[i].pos);
        } else {
            if (row != 255 || col != 255) throw std::runtime_error("Invalid captured king position");
            kings[i].pos = {-1,-1};
        }
    }
    auto phase = read(1), winner = read(1), capturer = read(4), break_ms = read(2), round = read(4);
    if (phase > 1 || (winner > 1 && winner != 255) || break_ms > 5000 || round == 0)
        throw std::runtime_error("Invalid round state");
    if (phase == uint8_t(chess::RoundPhase::Playing)) {
        if (winner != 255 || capturer != 0 || break_ms != 0 || !kings[0].alive || !kings[1].alive)
            throw std::runtime_error("Invalid playing round");
    } else {
        if (winner > 1 || break_ms == 0 || !kings[winner].alive || kings[1-winner].alive)
            throw std::runtime_error("Invalid round result");
        // The capturer may have disconnected during the break.
        for (auto const &player : players)
            if (player.id == capturer && uint8_t(player.team) != winner)
                throw std::runtime_error("Invalid capturer team");
    }
    auto selection_open = read(1), selected_piece = read(1);
    if (selection_open > 1 || (selected_piece >= uint8_t(chess::Pieces::King) && selected_piece != 255) ||
        (selection_open && selected_piece != 255) ||
        ((selection_open || selected_piece != 255) && (phase != uint8_t(chess::RoundPhase::Break) || !capturer || !ids.count(capturer))))
        throw std::runtime_error("Invalid selection state");
    if (recipient && !ids.count(recipient)) throw std::runtime_error("Missing local player");
    // Commit only a fully validated frame. Never apply this on the live server.
    logic.players = std::move(players);
    logic.kings = kings;
    local_player_id = recipient;
    logic.phase = chess::RoundPhase(phase);
    logic.winner = winner == 255 ? std::nullopt : std::optional<chess::Team>(chess::Team(winner));
    logic.capturer_id = capturer;
    logic.break_remaining = float(break_ms) / 1000.0f;
    logic.round_number = round;
    logic.selection_open = selection_open != 0;
    logic.selected_piece = selected_piece == 255 ? std::nullopt : std::optional<chess::Pieces>(chess::Pieces(selected_piece));
    buffer.erase(buffer.begin(), buffer.begin() + 4 + size);
    return true;
}

void Game::send_move_message(Connection *connection, chess::Position destination, uint32_t life) {
    if (!chess::ChessLogic::in_bounds(destination)) return;
    for (uint8_t byte : {uint8_t(Message::C2S_Move), uint8_t(6), uint8_t(0), uint8_t(0),
                         uint8_t(destination.row), uint8_t(destination.col)}) connection->send(byte);
    for (unsigned i = 0; i < 4; ++i) connection->send(uint8_t(life >> (8 * i)));
}

bool Game::recv_move_message(Connection *connection, uint32_t player_id) {
    auto &buffer = connection->recv_buffer;
    if (buffer.empty()) return false;
    if (buffer[0] != uint8_t(Message::C2S_Move)) throw std::runtime_error("Unknown client message");
    if (buffer.size() < 4) return false;
    if (buffer[1] != 6 || buffer[2] != 0 || buffer[3] != 0) throw std::runtime_error("Invalid move size");
    if (buffer.size() < 10) return false;
    uint32_t life = 0;
    for (unsigned i = 0; i < 4; ++i) life |= uint32_t(buffer[6 + i]) << (8 * i);
    for (auto const &player : logic.players) {
        if (player.id == player_id && player.life == life) {
            logic.move_player(player_id, {buffer[4], buffer[5]});
            break;
        }
    }
    buffer.erase(buffer.begin(), buffer.begin() + 10);
    return true; // consumed, even when the move is illegal
}

void Game::send_selection_message(Connection *connection, uint32_t round, chess::Pieces piece) {
    for (uint8_t byte : {uint8_t(Message::C2S_Select), uint8_t(5), uint8_t(0), uint8_t(0)}) connection->send(byte);
    for (unsigned i=0; i<4; ++i) connection->send(uint8_t(round >> (8*i)));
    connection->send(uint8_t(piece));
}

bool Game::recv_client_message(Connection *connection, uint32_t player_id) {
    auto &buffer = connection->recv_buffer;
    if (buffer.empty()) return false;
    if (buffer[0] != uint8_t(Message::C2S_Select)) return recv_move_message(connection, player_id);
    if (buffer.size() < 4) return false;
    if (buffer[1] != 5 || buffer[2] || buffer[3]) throw std::runtime_error("Invalid selection size");
    if (buffer.size() < 9) return false;
    uint32_t round = 0;
    for (unsigned i=0; i<4; ++i) round |= uint32_t(buffer[4+i]) << (8*i);
    logic.select_piece(player_id, round, chess::Pieces(buffer[8]));
    buffer.erase(buffer.begin(), buffer.begin()+9);
    return true;
}
