#pragma once
#include "noxcore/map/sections.hpp"
#include "noxcore/things/thing_db.hpp"
#include "noxcore/video/video_bag.hpp"
#include "noxcore/video/sprite.hpp"
#include <SDL3/SDL.h>
#include <filesystem>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace vita_viewer {
constexpr int width = 960, height = 544;
struct Tile { float x, y; nox::map::FloorTile tile; std::string key; };
struct Wall { float x, y, sortY; nox::things::ImageRef image; };
struct Camera { float x = 0, y = 0, zoom = 1; };
struct Stats {
    std::size_t textures = 0, textureBytes = 0, decodedBytes = 0, segmentBytes = 0;
    unsigned tiles = 0, walls = 0, uploads = 0, cacheMisses = 0;
};

class Viewer {
public:
    Viewer(SDL_Renderer* renderer, const std::filesystem::path& gameDir);
    ~Viewer();
    void load(const std::filesystem::path& map);
    void draw();
    void resetCamera();
    void clearCaches();
    Camera camera;
    Stats stats;
    const std::vector<std::filesystem::path>& maps() const { return maps_; }
    const std::string& mapName() const { return mapName_; }
    std::size_t tileCount() const { return tiles_.size(); }
    std::size_t wallCount() const { return walls_.size(); }
private:
    struct Texture {
        SDL_Texture* handle = nullptr;
        int w = 0, h = 0, ox = 0, oy = 0;
        std::size_t bytes = 0;
        std::uint64_t used = 0;
    };
    struct Decoded {
        std::shared_ptr<const nox::video::DecodedImage> image;
        std::uint64_t used = 0;
        std::size_t bytes = 0;
    };
    std::int32_t index(const nox::things::ImageRef& ref);
    std::shared_ptr<const nox::video::DecodedImage> decode(std::int32_t index);
    std::shared_ptr<const nox::video::DecodedImage> floorImage(std::uint8_t floor, std::int16_t variant);
    Texture& upload(const std::string& key, const nox::video::DecodedImage& image);
    Texture& tileTexture(const Tile& tile);
    Texture& wallTexture(const Wall& wall);
    void drawTexture(Texture& texture, float x, float y);
    SDL_Renderer* renderer_;
    nox::things::ThingDb things_;
    nox::video::VideoBag bag_;
    std::vector<Tile> tiles_;
    std::vector<Wall> walls_;
    std::vector<std::filesystem::path> maps_;
    std::map<std::string, Texture> textures_;
    std::map<std::int32_t, Decoded> decoded_;
    std::list<std::pair<std::uint32_t, std::vector<std::uint8_t>>> segments_;
    std::string mapName_;
    Camera initial_;
    std::uint64_t tick_ = 0;
};
}
