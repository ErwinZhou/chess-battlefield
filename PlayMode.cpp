#include "PlayMode.hpp"

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

bool PlayMode::handle_event(SDL_Event const &evt, glm::uvec2 const &window_size) {

	if (evt.type == SDL_EVENT_KEY_DOWN) {
		if (evt.key.repeat) {
			//ignore repeats
		} else if (evt.key.key == SDLK_A) {
			controls.left.downs += 1;
			controls.left.pressed = true;
			return true;
		} else if (evt.key.key == SDLK_D) {
			controls.right.downs += 1;
			controls.right.pressed = true;
			return true;
		} else if (evt.key.key == SDLK_W) {
			controls.up.downs += 1;
			controls.up.pressed = true;
			return true;
		} else if (evt.key.key == SDLK_S) {
			controls.down.downs += 1;
			controls.down.pressed = true;
			return true;
		} else if (evt.key.key == SDLK_SPACE) {
			controls.jump.downs += 1;
			controls.jump.pressed = true;
			return true;
		}
	} else if (evt.type == SDL_EVENT_KEY_UP) {
		if (evt.key.key == SDLK_A) {
			controls.left.pressed = false;
			return true;
		} else if (evt.key.key == SDLK_D) {
			controls.right.pressed = false;
			return true;
		} else if (evt.key.key == SDLK_W) {
			controls.up.pressed = false;
			return true;
		} else if (evt.key.key == SDLK_S) {
			controls.down.pressed = false;
			return true;
		} else if (evt.key.key == SDLK_SPACE) {
			controls.jump.pressed = false;
			return true;
		}
	}

	return false;
}

void PlayMode::update(float elapsed) {

	//queue data for sending to server:
	controls.send_controls_message(&client.connection);

	//reset button press counters:
	controls.left.downs = 0;
	controls.right.downs = 0;
	controls.up.downs = 0;
	controls.down.downs = 0;
	controls.jump.downs = 0;

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
					if (game.recv_state_message(c)) handled_message = true;
				} while (handled_message);
			} catch (std::exception const &e) {
				std::cerr << "[" << c->socket << "] malformed message from server: " << e.what() << std::endl;
				//quit the game:
				throw e;
			}
		}
	}, 0.0);
}

void PlayMode::draw(glm::uvec2 const &drawable_size) {
	if (drawable_size.x == 0 || drawable_size.y == 0) return;
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_SCISSOR_TEST);
	glClearColor(0.07f, 0.08f, 0.10f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	// keep an 8x8 board square and centered when the window is resized
	int cell = std::max(1, int(std::min(drawable_size.x, drawable_size.y)) / 10);
	int side = cell * 8;
	int left = (int(drawable_size.x) - side) / 2;
	int bottom = (int(drawable_size.y) - side) / 2;
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

	// these are actual board positions with row 0 at the bottom and col 0 at the left
	// change only row and col below to move a preview piece to another square
	struct Placement { char const *image; int row, col; };
	static Placement const pieces[] = {
		{"pawn", 1, 3}, {"knight", 0, 1}, {"bishop", 2, 2},
		{"rook", 0, 7}, {"queen", 3, 4}, {"king", 6, 4},
	};

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
	for (auto const &piece : pieces) {
		// convert board coordinates to OpenGL's -1 to +1 range with a small margin
		float x0 = (piece.col + 0.1f) / 4.0f - 1.0f;
		float y0 = (piece.row + 0.1f) / 4.0f - 1.0f;
		float x1 = (piece.col + 0.9f) / 4.0f - 1.0f;
		float y1 = (piece.row + 0.9f) / 4.0f - 1.0f;
		// u/v always cover the entire individual PNG from 0 to 1
		float vertices[] = {
			x0, y0, 0, 0,  x1, y0, 1, 0,  x0, y1, 0, 1,
			x0, y1, 0, 1,  x1, y0, 1, 0,  x1, y1, 1, 1,
		};
		glBindTexture(GL_TEXTURE_2D, piece_textures.at(piece.image));
		glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STREAM_DRAW);
		glDrawArrays(GL_TRIANGLES, 0, 6);
	}
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindTexture(GL_TEXTURE_2D, 0);
	glUseProgram(0);
	glDisable(GL_BLEND);
	glViewport(0, 0, drawable_size.x, drawable_size.y);
	GL_ERRORS();
}
