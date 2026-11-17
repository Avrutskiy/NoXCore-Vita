#pragma once
#include <algorithm>
#include <array>
#include <cmath>

namespace vita_viewer {
constexpr int width = 960, height = 544;
constexpr float classicHeightZoom = 544.f / 480.f;
constexpr float defaultZoom = 1.5f;
inline constexpr std::array zoomLevels{.5f, 1.f, classicHeightZoom, 1.2f, 1.4f, 1.5f, 2.f};
struct Camera { float x = 0, y = 0, zoom = defaultZoom; };
struct PanInput { float x = 0, y = 0; };

inline PanInput limitPan(PanInput p) {
    const float length=std::hypot(p.x,p.y);
    if (length>1) {p.x/=length;p.y/=length;}
    return p;
}
inline PanInput stickPan(int x,int y) {
    // Rescale the radial dead zone to zero, preserving direction and full travel.
    const float dx=(x-128)/(x<128 ? 128.f : 127.f);
    const float dy=(y-128)/(y<128 ? 128.f : 127.f);
    const float length=std::hypot(dx,dy);
    constexpr float deadZone=.18f;
    if (length<=deadZone) return {};
    const float travel=(std::min(length,1.f)-deadZone)/(1-deadZone);
    const float speed=travel*(.4f+.6f*travel); // fine control near centre
    return {dx*speed/length,dy*speed/length};
}
inline void panCamera(Camera& camera,PanInput input,float seconds) {
    input=limitPan(input); // diagonals travel at the same speed as axial motion
    const float distance=360.f*std::clamp(seconds,0.f,.1f)/camera.zoom;
    camera.x+=input.x*distance;camera.y+=input.y*distance;
}

// Native-resolution scene cache. The camera remains continuous; only the cache's
// world origin is integral. One texel beyond the view protects linear sampling.
struct SceneWindow {
    static constexpr int padding=64;
    int w=0,h=0;
    float x=0,y=0;
    bool valid=false;
    static int extent(float view) {return static_cast<int>(std::ceil(view))+2*padding+2;}
    bool contains(const Camera& c) const {
        const float left=c.x-width/(2*c.zoom),top=c.y-height/(2*c.zoom);
        return valid && w==extent(width/c.zoom) && h==extent(height/c.zoom) &&
            left>=x+1 && top>=y+1 && left+width/c.zoom<=x+w-1 && top+height/c.zoom<=y+h-1;
    }
    void place(const Camera& c) {
        w=extent(width/c.zoom);h=extent(height/c.zoom);
        x=std::floor(c.x-width/(2*c.zoom))-padding-1;
        y=std::floor(c.y-height/(2*c.zoom))-padding-1;
        valid=false; // only a successfully rendered scene becomes reusable
    }
};
}
