#include "PlayMode.hpp"
#include "BoardLayout.hpp"
#include "DrawLines.hpp"
#include <cmath>

#include "ColorTextureProgram.hpp"
#include "load_save_png.hpp"
#include "gl_errors.hpp"
#include "data_path.hpp"
#include "hex_dump.hpp"

#include <glm/gtc/type_ptr.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/string_cast.hpp>

#include <algorithm>

PlayMode::PlayMode(Client &client_) : client(client_) {
	// read each PNG separately and upload its colors to its own texture
	for (auto name : {"pawn", "knight", "bishop", "rook", "queen", "king"}) {
		glm::uvec2 size;
		std::vector<glm::u8vec4> pixels;
		load_png(data_path(std::string(name) + ".png"), &size, &pixels, LowerLeftOrigin);
		GLuint texture;
		glGenTextures(1, &texture);
		glBindTexture(GL_TEXTURE_2D, texture);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, size.x, size.y, 0,
			GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		piece_textures[name] = texture;
	}
	glBindTexture(GL_TEXTURE_2D, 0);

	// each vertex contains screen position x/y followed by image coordinates u/v
	glGenVertexArrays(1, &piece_vao);
	glGenBuffers(1, &piece_vbo);
	glBindVertexArray(piece_vao);
	glBindBuffer(GL_ARRAY_BUFFER, piece_vbo);
	auto const &shader = *color_texture_program;
	glVertexAttribPointer(shader.Position_vec4, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
	glEnableVertexAttribArray(shader.Position_vec4);
	glVertexAttribPointer(shader.TexCoord_vec2, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
		reinterpret_cast<void *>(2 * sizeof(float)));
	glEnableVertexAttribArray(shader.TexCoord_vec2);
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

PlayMode::~PlayMode() {
	for (auto const &piece : piece_textures) glDeleteTextures(1, &piece.second);
	glDeleteBuffers(1, &piece_vbo);
	glDeleteVertexArrays(1, &piece_vao);
}

bool PlayMode::handle_event(SDL_Event const &event, glm::uvec2 const &window_size) {
    bool click = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT;
    bool key = event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat;
    if (!click && !key) return false;
    if (!has_snapshot) return click;
    auto local = std::find_if(game.logic.players.begin(), game.logic.players.end(),
        [&](auto const &p) { return p.id == game.local_player_id; });
    if (local == game.logic.players.end()) return click;
    int width, height;
    SDL_GetWindowSizeInPixels(Mode::window, &width, &height);
    if (game.logic.phase == chess::RoundPhase::Break) {
        if (!game.logic.selection_open || game.logic.capturer_id != local->id) return click;
        int choice = -1;
        if (key) {
            if (event.key.key >= SDLK_1 && event.key.key <= SDLK_5) choice = int(event.key.key - SDLK_1);
            if (event.key.key == SDLK_0) choice = 5;
        } else if (window_size.x && window_size.y) {
            auto layout = BoardLayout::fit(width,height);
            float size = std::min(float(layout.panel_width)/12.0f, float(height)/24.0f);
            float x = event.button.x * width / window_size.x;
            float y = height - event.button.y * height / window_size.y;
            float first = float(layout.bottom+layout.side) - size*11.9f;
            if (x >= layout.panel_left && x < layout.panel_left+layout.panel_width)
                for (int i=0; i<6; ++i) {
                    float baseline = first - i*size*1.65f;
                    if (y >= baseline-size*.25f && y < baseline+size*1.2f) choice=i;
                }
        }
        if (choice >= 0) {
            auto type = choice == 5 ? local->piece : chess::Pieces(choice);
            if (type == local->piece || local->points >= chess::ChessLogic::capture_value(type))
                Game::send_selection_message(&client.connection, game.logic.round_number, type);
            return true;
        }
        return click;
    }
    if (!click) return false;
    if (local->status != chess::PlayerStatus::Alive || local->cooldown > 0) return true;
    auto destination = BoardLayout::hit(event.button.x, event.button.y, window_size.x, window_size.y, width, height);
    if (destination) Game::send_move_message(&client.connection, *destination, local->life);
    return true;
}

void PlayMode::update(float) {
	//send/receive data:
	client.poll([this](Connection *c, Connection::Event event){
		if (event == Connection::OnOpen) {
			std::cout << "[" << c->socket << "] opened" << std::endl;
		} else if (event == Connection::OnClose) {
			std::cout << "[" << c->socket << "] closed (!)" << std::endl;
			throw std::runtime_error("Lost connection to server!");
		} else { assert(event == Connection::OnRecv);
			//std::cout << "[" << c->socket << "] recv'd data. Current buffer:\n" << hex_dump(c->recv_buffer); std::cout.flush(); //DEBUG
			bool handled_message;
			try {
				do {
					handled_message = false;
					if (!c->recv_buffer.empty() && c->recv_buffer.front() != uint8_t(Message::S2C_State))
                        throw std::runtime_error("Unknown server message");
                    if (game.recv_state_message(c)) {
                        handled_message = true;
                        has_snapshot = true;
                    }
				} while (handled_message);
			} catch (std::exception const &e) {
				std::cerr << "[" << c->socket << "] malformed message from server: " << e.what() << std::endl;
				//quit the game:
				throw e;
			}
		}
	}, 0.0);
    std::string title = "Chess - connecting";
    if (has_snapshot) for (auto const &player : game.logic.players) {
        if (player.id != game.local_player_id) continue;
        static char const *names[] = {"Pawn", "Knight", "Bishop", "Rook", "Queen"};
        hud_info = std::string(player.team == chess::Team::A ? "Team A | " : "Team B | ")
                 + names[uint8_t(player.piece)] + " | Points: " + std::to_string(player.points);
        if (game.logic.phase == chess::RoundPhase::Break)
            hud_status = std::string(*game.logic.winner == chess::Team::A ? "Team A wins! " : "Team B wins! ")
                       + "Next round in " + std::to_string(int(std::ceil(game.logic.break_remaining)));
        else if (player.status == chess::PlayerStatus::Dead)
            hud_status = "Captured - respawn in " + std::to_string(int(std::ceil(player.respawn_remaining)));
        else if (player.status == chess::PlayerStatus::Waiting)
            hud_status = "Waiting for a free home square";
        else hud_status.clear();
        title = "Chess - " + hud_info;
        if (!hud_status.empty()) title += " - " + hud_status;
    }
    if (title != server_message) {
        SDL_SetWindowTitle(Mode::window, title.c_str());
        server_message = title;
    }
}

void PlayMode::draw(glm::uvec2 const &drawable_size) {
	if (drawable_size.x == 0 || drawable_size.y == 0) return;
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_SCISSOR_TEST);
	glClearColor(0.07f, 0.08f, 0.10f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	// keep an 8x8 board square and centered when the window is resized
    auto board = BoardLayout::fit(drawable_size.x, drawable_size.y);
    int cell = board.cell, side = board.side, left = board.left, bottom = board.bottom;
	glEnable(GL_SCISSOR_TEST);
	for (int row = 0; row < 8; ++row) {
		for (int col = 0; col < 8; ++col) {
			glScissor(left + col * cell, bottom + row * cell, cell, cell);
			if ((row + col) % 2 == 0) glClearColor(0.20f, 0.29f, 0.26f, 1.0f);
			else glClearColor(0.72f, 0.76f, 0.64f, 1.0f);
			glClear(GL_COLOR_BUFFER_BIT);
		}
	}
	glDisable(GL_SCISSOR_TEST);

    if (!has_snapshot) return;
    auto marker = [&](chess::Position pos, chess::Team team, bool local) {
        int x = left + pos.col * cell, y = bottom + pos.row * cell;
        int width = std::max(1, cell / 20);
        glEnable(GL_SCISSOR_TEST);
        auto rect = [&](int rx, int ry, int w, int h) {
            glScissor(rx, ry, w, h);
            glClear(GL_COLOR_BUFFER_BIT);
        };
        if (team == chess::Team::A) glClearColor(0.1f, 0.45f, 1.0f, 1.0f);
        else glClearColor(1.0f, 0.35f, 0.05f, 1.0f);
        rect(x, y, cell, std::max(1, cell / 10));
        if (local) {
            glClearColor(1.0f, 0.9f, 0.05f, 1.0f);
            rect(x, y, width, cell);
            rect(x + cell - width, y, width, cell);
            rect(x, y, cell, width);
            rect(x, y + cell - width, cell, width);
        }
        glDisable(GL_SCISSOR_TEST);
    };
    for (auto const &king : game.logic.kings) if (king.alive) marker(king.pos, king.team, false);
    for (auto const &player : game.logic.players)
        if (player.status == chess::PlayerStatus::Alive)
            marker(player.pos, player.team, player.id == game.local_player_id);

    // Small bar below the board: green means ready, amber drains with cooldown.
    for (auto const &player : game.logic.players) {
        if (game.logic.phase != chess::RoundPhase::Playing || player.id != game.local_player_id || player.status != chess::PlayerStatus::Alive) continue;
        int height = std::max(2, cell / 10);
        int y = std::max(0, bottom - height * 2);
        glEnable(GL_SCISSOR_TEST);
        glScissor(left, y, side, height);
        glClearColor(0.15f, 0.17f, 0.20f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        bool ready = player.cooldown <= 0;
        glScissor(left, y, ready ? side : int(side * player.cooldown / chess::ChessLogic::MoveCooldown), height);
        if (ready) glClearColor(0.15f, 0.85f, 0.4f, 1.0f);
        else glClearColor(1.0f, 0.6f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glDisable(GL_SCISSOR_TEST);
    }

	glViewport(left, bottom, side, side);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	auto const &shader = *color_texture_program;
	glUseProgram(shader.program);
	glm::mat4 identity(1.0f);
	glUniformMatrix4fv(shader.CLIP_FROM_OBJECT_mat4, 1, GL_FALSE, glm::value_ptr(identity));
	glActiveTexture(GL_TEXTURE0);
	glBindVertexArray(piece_vao);
	glBindBuffer(GL_ARRAY_BUFFER, piece_vbo);
	glVertexAttrib4f(shader.Color_vec4, 1.0f, 1.0f, 1.0f, 1.0f);
	auto draw_piece = [&](chess::Pieces type, chess::Position pos) {
        char const *name = nullptr;
        switch (type) {
            case chess::Pieces::Pawn: name = "pawn"; break;
            case chess::Pieces::Knight: name = "knight"; break;
            case chess::Pieces::Bishop: name = "bishop"; break;
            case chess::Pieces::Rook: name = "rook"; break;
            case chess::Pieces::Queen: name = "queen"; break;
            case chess::Pieces::King: name = "king"; break;
            default: throw std::runtime_error("Invalid piece type");
        }
		// convert board coordinates to OpenGL's -1 to +1 range with a small margin
		float x0 = (pos.col + 0.1f) / 4.0f - 1.0f;
		float y0 = (pos.row + 0.1f) / 4.0f - 1.0f;
		float x1 = (pos.col + 0.9f) / 4.0f - 1.0f;
		float y1 = (pos.row + 0.9f) / 4.0f - 1.0f;
		// u/v always cover the entire individual PNG from 0 to 1
		float vertices[] = {
			x0, y0, 0, 0,  x1, y0, 1, 0,  x0, y1, 0, 1,
			x0, y1, 0, 1,  x1, y0, 1, 0,  x1, y1, 1, 1,
		};
		glBindTexture(GL_TEXTURE_2D, piece_textures.at(name));
		glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STREAM_DRAW);
		glDrawArrays(GL_TRIANGLES, 0, 6);
    };
    for (auto const &king : game.logic.kings) if (king.alive) draw_piece(chess::Pieces::King, king.pos);
    for (auto const &player : game.logic.players)
        if (player.status == chess::PlayerStatus::Alive) draw_piece(player.piece, player.pos);
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindTexture(GL_TEXTURE_2D, 0);
	glUseProgram(0);
	glDisable(GL_BLEND);
	glViewport(0, 0, drawable_size.x, drawable_size.y);
    // Right-side status panel. Text scales with drawable size, including high DPI.
    {
        float w = float(drawable_size.x), h = float(drawable_size.y);
        float size = std::min(float(board.panel_width) / 12.0f, h / 24.0f);
        float x = float(board.panel_left);
        float y = float(bottom + side) - size;
        glm::mat4 pixels(1.0f);
        pixels[0][0] = 2.0f / w; pixels[1][1] = 2.0f / h;
        pixels[3][0] = -1.0f; pixels[3][1] = -1.0f;
        DrawLines text(pixels);
        auto line = [&](std::string const &label, float scale = 1.0f,
                        glm::u8vec4 color = glm::u8vec4(255)) {
            float font = size * scale;
            text.draw_text(label, {x,y,0}, {font,0,0}, {0,font,0}, color);
            y -= font * 1.65f;
        };
        for (auto const &player : game.logic.players) {
            if (player.id != game.local_player_id) continue;
            static char const *names[] = {"Pawn", "Knight", "Bishop", "Rook", "Queen"};
            line(player.team == chess::Team::A ? "TEAM A" : "TEAM B", 1.0f,
                 player.team == chess::Team::A ? glm::u8vec4(100,180,255,255) : glm::u8vec4(255,170,80,255));
            line(names[uint8_t(player.piece)]);
            line("Points: " + std::to_string(player.points));
            y -= size;
            if (game.logic.phase == chess::RoundPhase::Break) {
                line(*game.logic.winner == chess::Team::A ? "Team A wins!" : "Team B wins!");
                line("Next round in " + std::to_string(int(std::ceil(game.logic.break_remaining))));
                if (game.logic.capturer_id == player.id) {
                    if (game.logic.selection_open) {
                        line("Choose piece");
                        for (int i=0; i<5; ++i) {
                            auto type = chess::Pieces(i);
                            bool current = type == player.piece;
                            uint32_t cost = current ? 0 : chess::ChessLogic::capture_value(type);
                            std::string label = std::to_string(i+1) + " " + names[i] + (current ? " (keep)" : " -" + std::to_string(cost));
                            line(label, 1.0f, player.points >= cost ? glm::u8vec4(255) : glm::u8vec4(125,125,125,255));
                        }
                        line("0 Keep - free");
                    } else if (game.logic.selected_piece) {
                        line(std::string("Next: ") + names[uint8_t(*game.logic.selected_piece)]);
                    }
                }
            } else if (player.status == chess::PlayerStatus::Dead) {
                line("Captured");
                line("Respawn in " + std::to_string(int(std::ceil(player.respawn_remaining))));
            } else if (player.status == chess::PlayerStatus::Waiting) {
                line("Waiting for"); line("a free home"); line("square");
            }
            break;
        }
    }
	GL_ERRORS();
}
