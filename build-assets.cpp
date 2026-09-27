#include "SpriteAtlas.hpp"
#include "load_save_png.hpp"
#include "read_write_chunk.hpp"

#include <fstream>
#include <iostream>

int main(int argc, char **argv) {
	try {
		// run from the project root; Maekfile.js does this automatically
		std::string output = argc > 1 ? argv[1] : "dist/chess.sprites";
		char const *names[] = {"pawn", "knight", "bishop", "rook", "queen", "king"};
		constexpr uint32_t side = 32;
		constexpr uint32_t count = uint32_t(PieceSprite::Count);
		static_assert(sizeof(names) / sizeof(names[0]) == count, "One PNG per piece");
		AtlasHeader header{1, count * side, side};
		std::vector<SpriteRecord> sprites;
		std::vector<uint8_t> pixels(header.width * header.height * 4);

		for (uint32_t i = 0; i < count; ++i) {
			glm::uvec2 size;
			std::vector<glm::u8vec4> image;
			std::string path = std::string("assets/") + names[i] + ".png";
			// libpng converts grayscale/palette images to RGBA too; flipping here
			// makes y=0 the bottom, matching the texture coordinates used by OpenGL
			load_png(path, &size, &image, LowerLeftOrigin);
			if (size.x != side || size.y != side)
				throw std::runtime_error(path + " must be 32x32");
			sprites.push_back({i, i * side, 0, side, side});
			for (uint32_t y = 0; y < side; ++y) {
				for (uint32_t x = 0; x < side; ++x) {
					size_t dst = (y * header.width + i * side + x) * 4;
					for (uint32_t c = 0; c < 4; ++c) pixels[dst + c] = image[y * side + x][c];
				}
			}
		}

		// each write_chunk adds an 8-byte header: four-character tag + byte count
		// all PNGs have been checked before opening the output file
		std::ofstream file(output, std::ios::binary);
		if (!file) throw std::runtime_error("Cannot write " + output);
		write_chunk("hdr0", std::vector<AtlasHeader>{header}, &file);
		write_chunk("spr0", sprites, &file);
		write_chunk("rgba", pixels, &file);
		file.close();
		if (!file) throw std::runtime_error("Failed to write " + output);

		// read back with the same loader used by the client to catch format drift
		SpriteAtlas check(output);
		if (check.pixels != pixels) throw std::runtime_error("Atlas round-trip failed");
		std::cout << "Wrote " << output << " (192x32, six sprites); read-back passed.\n";
	} catch (std::exception const &e) {
		std::cerr << "Assets: " << e.what() << '\n';
		return 1;
	}
}
