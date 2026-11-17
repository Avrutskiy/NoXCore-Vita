// Stock map geometry follows upstream noxsdl/map_view.cpp and map_thumbnail.cpp.
// Cache ownership and the platform front end are deliberately independent of the desktop client.
#include "viewer.hpp"
#include "noxcore/io/file.hpp"
#include "noxcore/map/objects.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>

namespace vita_viewer {
namespace {
constexpr std::size_t textureBudget = 32u << 20, decodeBudget = 8u << 20, segmentBudget = 4u << 20;
std::string tileKey(const nox::map::FloorTile& t) {
    std::string key = "t:" + std::to_string(t.image) + ":" + std::to_string(t.variant);
    for (const auto& e : t.edges)
        key += ":" + std::to_string(e.floor) + "," + std::to_string(e.variant) + "," + std::to_string(e.edge) + "," + std::to_string(e.direction);
    return key;
}
int wallKey(int x, int y) { return x * 256 + y; }
void check(bool ok) { if (!ok) throw std::runtime_error(SDL_GetError()); }
}

Viewer::Viewer(SDL_Renderer* renderer, const std::filesystem::path& dir)
    : renderer_(renderer), things_(nox::things::loadThingDb(nox::resolveNoCase(dir / "thing.bin"))),
      bag_(nox::resolveNoCase(dir / "video.bag"), nox::resolveNoCase(dir / "video.idx")) {
    const auto modifiers=nox::resolveNoCase(dir / "modifier.bin");
    if (std::filesystem::is_regular_file(modifiers)) modifiers_=nox::things::loadModifierDb(modifiers);
    else SDL_Log("modifier.bin missing: item and equipment colours use fallback colours");
    // Only the normal maps/<name>/<name>.map layout; no recursive scan of user storage.
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(nox::resolveNoCase(dir / "maps"), ec)) {
        if (entry.is_directory()) {
            const auto name = entry.path().filename().string();
            const auto p = nox::resolveNoCase(entry.path() / (name + ".map"));
            if (std::filesystem::is_regular_file(p)) maps_.push_back(p);
        }
    }
    std::sort(maps_.begin(), maps_.end());
}
Viewer::~Viewer() { clearCaches(); SDL_DestroyTexture(scene_); }
void Viewer::clearCaches() {
    for (auto& [key, texture] : textures_) SDL_DestroyTexture(texture.handle);
    textures_.clear(); decoded_.clear(); segments_.clear();
    stats.textureBytes = stats.decodedBytes = stats.segmentBytes = stats.textures = 0;
}
void Viewer::load(const std::filesystem::path& path) {
    // Parse completely before replacing the visible map; a bad selection leaves the old map usable.
    auto map = nox::map::loadMapFile(nox::resolveNoCase(path));
    auto floor = nox::map::decodeSection<nox::map::FloorMap>(map);
    auto walls = nox::map::decodeSection<nox::map::WallMap>(map).walls;
    std::set<int> windows, openSecret;
    if (map.find(nox::map::WindowWalls::kName)) {
        const auto win = nox::map::decodeSection<nox::map::WindowWalls>(map);
        for (const auto& w : win.walls) windows.insert(wallKey(w.x,w.y));
        if (win.version < 2) for (auto& w : walls) if (windows.contains(wallKey(w.x,w.y))) w.variant=0;
    }
    if (map.find(nox::map::SecretWalls::kName))
        for (const auto& w : nox::map::decodeSection<nox::map::SecretWalls>(map).walls)
            if (w.flags & 8) openSecret.insert(wallKey(w.pos.x,w.pos.y));
    std::vector<Tile> nextTiles;
    std::vector<Wall> nextWalls;
    float left = 1e9f, top = 1e9f, right = -1e9f, bottom = -1e9f;
    for (const auto& p : floor.readPairs()) {
        auto add = [&](float x, float y, const nox::map::FloorTile& t) {
            nextTiles.push_back({x,y,t,tileKey(t)});
            left=std::min(left,x); top=std::min(top,y); right=std::max(right,x+46); bottom=std::max(bottom,y+46);
        };
        if (p.odd) add(p.x*46.f+12,p.y*46.f-11,*p.odd);
        if (p.even) add(p.x*46.f-11,p.y*46.f+12,*p.even);
    }
    for (const auto& w : walls) {
        if (openSecret.contains(wallKey(w.x,w.y)) || w.material >= things_.walls.size()) continue;
        const auto& dirs = things_.walls[w.material].directions;
        std::size_t dir = w.direction & 0x7f, variant = w.variant;
        if (windows.contains(wallKey(w.x,w.y)) && dir<=1) dir+=11;
        if (dir>=dirs.size()) continue;
        if (dir>=11 && dir<=12 && variant>=dirs[dir].size()) variant=0;
        if (variant>=dirs[dir].size()) continue;
        const auto& image=dirs[dir][variant][0];
        nextWalls.push_back({w.x*23.f-50+image.x,w.y*23.f-72-image.y,w.y*23.f+11,image.image});
    }
    if (nextTiles.empty()) throw std::runtime_error("Map has no floor tiles");
    std::stable_sort(nextWalls.begin(),nextWalls.end(),[](const Wall& a,const Wall& b){return a.sortY<b.sortY;});
    // Start over a real tile nearest the map centre, not potentially empty space between rooms.
    const float cx=(left+right)/2,cy=(top+bottom)/2;
    const auto tile=std::min_element(nextTiles.begin(),nextTiles.end(),[&](const Tile& a,const Tile& b){
        return std::hypot(a.x+23-cx,a.y+23-cy)<std::hypot(b.x+23-cx,b.y+23-cy);
    });
    Camera nextInitial{tile->x+23,tile->y+23,defaultZoom};
    // Prefer the real entry point: geometric map centres often fall in unreachable scenery.
    if (map.find(nox::map::ObjectTOC::kName) && map.find(nox::map::ObjectData::kName)) {
        const auto toc=nox::map::decodeSection<nox::map::ObjectTOC>(map);
        std::set<std::uint16_t> starts;
        for (const auto& entry:toc.entries) if (entry.name=="PlayerStart") starts.insert(entry.id);
        for (const auto& record:nox::map::decodeSection<nox::map::ObjectData>(map).records) {
            if (!starts.contains(record.tocId)) continue;
            const auto spawn=nox::map::ObjectXfer::parse(record.data);
            if (std::isfinite(spawn.header.x) && std::isfinite(spawn.header.y)) {
                nextInitial={spawn.header.x,spawn.header.y,defaultZoom};break;
            }
        }
    }
    auto nextObjects=loadObjects(map);
    clearCaches();
    objects_=std::move(nextObjects);
    tiles_=std::move(nextTiles); walls_=std::move(nextWalls);
    initial_=nextInitial; camera=initial_;
    mapName_=path.stem().string();
    // Vita newlib's formatter does not consume %zu arguments. Use supported
    // conversions; otherwise a following %s can read a byte count as a pointer.
    SDL_Log("map=%s tiles=%u walls=%u",mapName_.c_str(),static_cast<unsigned>(tiles_.size()),static_cast<unsigned>(walls_.size()));
}
void Viewer::resetCamera() { camera=initial_; }
void Viewer::stepZoom(int direction) {
    if (direction>0) {
        for (const auto value:zoomLevels) if (value>camera.zoom+.0001f) {camera.zoom=value;break;}
    } else {
        for (auto it=zoomLevels.rbegin();it!=zoomLevels.rend();++it) if (*it<camera.zoom-.0001f) {camera.zoom=*it;break;}
    }
    SDL_Log("Zoom %.3fx; view %.1f x %.1f",camera.zoom,width/camera.zoom,height/camera.zoom);
}
std::int32_t Viewer::index(const nox::things::ImageRef& ref) {
    return ref.index>=0 ? ref.index : static_cast<std::int32_t>(bag_.findImage(ref.name));
}
std::shared_ptr<const nox::video::DecodedImage> Viewer::decode(std::int32_t id) {
    if (id<0 || static_cast<std::size_t>(id)>=bag_.images().size()) throw std::runtime_error("Invalid stock image reference");
    auto old=decoded_.find(id);
    if (old!=decoded_.end()) { old->second.used=++tick_; return old->second.image; }
    ++stats.cacheMisses;
    const auto& info=bag_.images()[id];
    auto seg=std::find_if(segments_.begin(),segments_.end(),[&](const auto& e){return e.first==info.segment;});
    if (seg==segments_.end()) {
        // Bound retained decompression staging before allocating the next segment.
        const auto size=bag_.segments().at(info.segment).size;
        while (!segments_.empty() && stats.segmentBytes+size>segmentBudget) {
            stats.segmentBytes-=segments_.back().second.size(); segments_.pop_back();
        }
        segments_.emplace_front(info.segment,bag_.readSegment(info.segment));
        stats.segmentBytes+=segments_.front().second.size();
    } else segments_.splice(segments_.begin(),segments_,seg);
    const auto& bytes=segments_.front().second;
    if (static_cast<std::size_t>(info.offset)+info.size>bytes.size()) throw std::runtime_error("Image outside bag segment");
    auto img=std::make_shared<nox::video::DecodedImage>(nox::video::decodeImage(std::span(bytes).subspan(info.offset,info.size),info.type));
    const auto size=img->rgba.size()+img->material.size()+img->edgeMask.size();
    while (!decoded_.empty() && stats.decodedBytes+size>decodeBudget) {
        auto victim=std::min_element(decoded_.begin(),decoded_.end(),[](const auto& a,const auto& b){return a.second.used<b.second.used;});
        stats.decodedBytes-=victim->second.bytes; decoded_.erase(victim);
    }
    if (size<=decodeBudget) { decoded_[id]={img,++tick_,size}; stats.decodedBytes+=size; }
    // An exceptional large segment may be decoded, but is never retained beyond the request.
    if (stats.segmentBytes>segmentBudget) { segments_.clear(); stats.segmentBytes=0; }
    return img;
}
std::shared_ptr<const nox::video::DecodedImage> Viewer::floorImage(std::uint8_t floor,std::int16_t variant) {
    if (floor>=things_.floors.size() || variant<0 || static_cast<std::size_t>(variant)>=things_.floors[floor].images.size())
        throw std::runtime_error("Invalid floor variant");
    auto image=decode(index(things_.floors[floor].images[variant]));
    if (image->width!=46 || image->height!=46) throw std::runtime_error("Nonstandard floor image");
    return image;
}
Viewer::Texture& Viewer::upload(const std::string& key,const nox::video::DecodedImage& image) {
    // Some stock wall variants deliberately reference an empty image.
    if (image.width==0 && image.height==0 && image.rgba.empty()) {
        auto& empty=textures_[key];empty.used=++tick_;return empty;
    }
    // Vita GXM aligns RGBA texture rows to 8 pixels. Track payload, not the driver's reserved pool.
    const std::size_t bytes=static_cast<std::size_t>((image.width+7)&~7)*image.height*4;
    if (image.width<=0 || image.height<=0 || bytes>textureBudget)
        throw std::runtime_error("Invalid or oversized image "+key+" ("+std::to_string(image.width)+"x"+std::to_string(image.height)+")");
    while (!textures_.empty() && stats.textureBytes+bytes>textureBudget) {
        auto victim=std::min_element(textures_.begin(),textures_.end(),[](const auto& a,const auto& b){return a.second.used<b.second.used;});
        SDL_DestroyTexture(victim->second.handle); stats.textureBytes-=victim->second.bytes; textures_.erase(victim);
    }
    SDL_Texture* handle=SDL_CreateTexture(renderer_,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STATIC,image.width,image.height);
    if (!handle) throw std::runtime_error(SDL_GetError());
    try {
        check(SDL_UpdateTexture(handle,nullptr,image.rgba.data(),image.width*4));
        check(SDL_SetTextureBlendMode(handle,SDL_BLENDMODE_BLEND));
        check(SDL_SetTextureScaleMode(handle,SDL_SCALEMODE_NEAREST));
    } catch (...) { SDL_DestroyTexture(handle); throw; }
    auto& texture=textures_[key];
    texture={handle,image.width,image.height,image.offsetX,image.offsetY,bytes,++tick_};
    stats.textureBytes+=bytes; ++stats.uploads;
    return texture;
}
Viewer::Texture& Viewer::tileTexture(const Tile& tile) {
    auto old=textures_.find(tile.key);
    if (old!=textures_.end()) { old->second.used=++tick_; return old->second; }
    auto base=floorImage(tile.tile.image,tile.tile.variant);
    nox::video::DecodedImage image=*base;
    for (const auto& e:tile.tile.edges) {
        if (e.edge>=things_.edges.size() || e.direction>=things_.edges[e.edge].images.size()) continue;
        auto mask=decode(index(things_.edges[e.edge].images[e.direction]));
        auto blend=floorImage(e.floor,e.variant);
        if (mask->edgeMask.size()!=46u*46u) throw std::runtime_error("Invalid floor edge mask");
        for (std::size_t i=0;i<mask->edgeMask.size();++i) {
            const auto* src=mask->edgeMask[i]==1 ? mask->rgba.data()+i*4 : mask->edgeMask[i]==2 ? blend->rgba.data()+i*4 : nullptr;
            if (src) std::copy_n(src,4,image.rgba.data()+i*4);
        }
    }
    return upload(tile.key,image);
}
Viewer::Texture& Viewer::wallTexture(const Wall& wall) {
    const auto id=index(wall.image);
    const std::string key="w:"+std::to_string(id);
    auto old=textures_.find(key);
    if (old!=textures_.end()) { old->second.used=++tick_; return old->second; }
    return upload(key,*decode(id));
}
void Viewer::drawTexture(Texture& t,float x,float y) {
    if (!t.handle) return;
    const SDL_FRect dst{std::round(x+t.ox-originX_),std::round(y+t.oy-originY_),static_cast<float>(t.w),static_cast<float>(t.h)};
    check(SDL_RenderTexture(renderer_,t.handle,nullptr,&dst));
}
void Viewer::draw() {
    stats.tiles=stats.walls=stats.objects=stats.uploads=stats.cacheMisses=0;
    const float viewW=width/camera.zoom,viewH=height/camera.zoom;
    const int sw=static_cast<int>(std::ceil(viewW)),sh=static_cast<int>(std::ceil(viewH));
    if (!scene_ || sw!=sceneW_ || sh!=sceneH_) {
        auto* next=SDL_CreateTexture(renderer_,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_TARGET,sw,sh);
        if (!next) throw std::runtime_error(SDL_GetError());
        SDL_SetTextureBlendMode(next,SDL_BLENDMODE_NONE);
        SDL_SetTextureScaleMode(next,SDL_SCALEMODE_NEAREST);
        SDL_DestroyTexture(scene_);scene_=next;sceneW_=sw;sceneH_=sh;
    }
    // Compose at native asset resolution, then scale once: independently rounding
    // fractional-size floor diamonds leaves seams between adjacent tiles.
    auto* previous=SDL_GetRenderTarget(renderer_);
    check(SDL_SetRenderTarget(renderer_,scene_));
    originX_=std::floor(camera.x-viewW/2);originY_=std::floor(camera.y-viewH/2);
    const float left=originX_,right=left+sw,top=originY_,bottom=top+sh;
    try {
        check(SDL_SetRenderDrawColor(renderer_,12,15,18,255));check(SDL_RenderClear(renderer_));
        for (const auto& t:tiles_) {
            if (t.x+46<left || t.x>right || t.y+46<top || t.y>bottom) continue;
            auto& texture=tileTexture(t);drawTexture(texture,t.x,t.y);++stats.tiles;
        }
        auto visible=[&](float x,float y,const Texture& t) {
            return t.handle && x+t.ox+t.w>=left && x+t.ox<=right && y+t.oy+t.h>=top && y+t.oy<=bottom;
        };
        auto drawObject=[&](const Object& o) {
            if (o.x+512<left || o.x-512>right || o.y+512<top || o.y-512>bottom) return;
            bool drawn=false;
            for (const auto& layer:o.layers) {
                auto& t=objectTexture(layer);
                if (visible(o.x,o.y,t)) {drawTexture(t,o.x,o.y);drawn=true;}
            }
            if (drawn) ++stats.objects;
        };
        auto object=objects_.begin();
        for (const auto& w:walls_) {
            while (object!=objects_.end() && object->sortY<=w.sortY) drawObject(*object++);
            if (w.x+512<left || w.x-512>right || w.y+512<top || w.y-512>bottom) continue;
            auto& t=wallTexture(w);
            if (visible(w.x,w.y,t)) {drawTexture(t,w.x,w.y);++stats.walls;}
        }
        while (object!=objects_.end()) drawObject(*object++);
    } catch (...) {SDL_SetRenderTarget(renderer_,previous);throw;}
    check(SDL_SetRenderTarget(renderer_,previous));
    const SDL_FRect source{0,0,viewW,viewH},dest{0,0,static_cast<float>(width),static_cast<float>(height)};
    check(SDL_RenderTexture(renderer_,scene_,&source,&dest));
    stats.textures=textures_.size();
}
}
