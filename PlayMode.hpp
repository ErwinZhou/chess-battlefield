#include "Mode.hpp"

#include "Connection.hpp"
#include "Game.hpp"
#include "GL.hpp"

#include <glm/glm.hpp>

#include <vector>
#include <deque>
#include <map>

struct PlayMode : Mode {
	PlayMode(Client &client);
	virtual ~PlayMode();

	//functions called by main loop:
	virtual bool handle_event(SDL_Event const &, glm::uvec2 const &window_size) override;
	virtual void update(float elapsed) override;
	virtual void draw(glm::uvec2 const &drawable_size) override;

	//----- game state -----

	// Set only after a complete authoritative snapshot.
	bool has_snapshot = false;

	//latest game state (from server):
	Game game;

	//last message from server:
	std::string server_message;
    std::string hud_info, hud_status;

	//connection to server:
	Client &client;

	// each piece keeps its own PNG texture
	std::map<std::string, GLuint> piece_textures;
	GLuint piece_vao = 0;
	GLuint piece_vbo = 0;

};
