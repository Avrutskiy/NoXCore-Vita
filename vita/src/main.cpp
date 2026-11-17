#include "viewer.hpp"
#include <SDL3/SDL_main.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <numeric>
#include <stdexcept>
#ifdef NOX_VITA
#include <psp2/ctrl.h>
#include <psp2/kernel/sysmem.h>
extern "C" { unsigned int _newlib_heap_size_user = 128 * 1024 * 1024; }
#endif

namespace {
using namespace vita_viewer;
constexpr const char* statusFormat="map=%s fps=%.1f work=%.2fms tex=%u decoded=%u segments=%u; %s";
struct Options {
#ifdef NOX_VITA
    std::filesystem::path gameDir="ux0:data/nox";
#else
    std::filesystem::path gameDir=".";
#endif
    std::filesystem::path map="maps/WAR01A/WAR01A.MAP", screenshot;
    int frames=1;
    float zoom=defaultZoom;
    bool tour=false;
};
Options arguments(int argc,char** argv) {
    Options o;
    for (int i=1;i<argc;++i) {
        std::string s=argv[i];
        auto value=[&]() -> std::string {if (++i>=argc) throw std::runtime_error("Missing argument for "+s); return argv[i];};
        if (s=="--game-dir") o.gameDir=value();
        else if (s=="--map") o.map=value();
        else if (s=="--headless") o.screenshot=value();
        else if (s=="--frames") o.frames=std::clamp(std::stoi(value()),1,10000);
        else if (s=="--zoom") {
            o.zoom=std::stof(value());
            if (std::abs(o.zoom-1.133f)<.00001f) o.zoom=defaultZoom;
            if (!std::isfinite(o.zoom) || o.zoom<.5f || o.zoom>2.f) throw std::runtime_error("Zoom must be between 0.5 and 2");
        }
        else if (s=="--tour") o.tour=true;
        else throw std::runtime_error("Unknown argument: "+s);
    }
    return o;
}
void check(bool ok) { if (!ok) throw std::runtime_error(SDL_GetError()); }
void text(SDL_Renderer* r,float x,float y,const std::string& value) {
    SDL_SetRenderDrawColor(r,235,235,225,255);
    SDL_RenderDebugText(r,x,y,value.c_str());
}
void panel(SDL_Renderer* r,float y,float h) {
    SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r,12,18,24,230);
    const SDL_FRect rect{0,y,960,h}; SDL_RenderFillRect(r,&rect);
}
void beginFrame(SDL_Renderer* r) {
    SDL_SetRenderDrawColor(r,12,15,18,255); SDL_RenderClear(r);
}
std::string memoryPools() {
#ifdef NOX_VITA
    SceKernelFreeMemorySizeInfo info{}; info.size=sizeof(info);
    if (sceKernelGetFreeMemorySize(&info)<0) return "Memory pool query failed";
    char buf[160];
    std::snprintf(buf,sizeof(buf),"Free OS pools: main %.1f MiB / CDRAM %.1f MiB / contiguous %.1f MiB",
        info.size_user/1048576.,info.size_cdram/1048576.,info.size_phycont/1048576.);
    return buf;
#else
    return "Desktop verification: timing is not Vita performance";
#endif
}
std::FILE* logFile=nullptr;
void logOutput(void*,int,SDL_LogPriority,const char* message) {
    std::fprintf(stderr,"%s\n",message);
    if (logFile) { std::fprintf(logFile,"%s\n",message); std::fflush(logFile); }
}
void verifyStatusFormatting() {
    // Run through SDL's actual target formatter before telemetry reaches the
    // render loop. A host libc test alone cannot detect Vita newlib limitations.
    char text[160];
    SDL_snprintf(text,sizeof(text),statusFormat,
        "TEST",60.0,1.25,2231808u,942928u,672341u,"TAIL");
    if (std::string(text)!="map=TEST fps=60.0 work=1.25ms tex=2231808 decoded=942928 segments=672341; TAIL")
        throw std::runtime_error("Runtime status formatting check failed");
    SDL_Log("Status formatting check passed");
}
}

int main(int argc,char** argv) {
    SDL_Window* window=nullptr;
    SDL_Surface* surface=nullptr;
    SDL_Renderer* renderer=nullptr;
    int result=0;
    try {
        const auto options=arguments(argc,argv);
#ifdef NOX_VITA
        std::error_code ec;
        std::filesystem::create_directories("ux0:data/nox-vita",ec);
        logFile=std::fopen("ux0:data/nox-vita/viewer.log","w");
        sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
#endif
        SDL_SetLogOutputFunction(logOutput,nullptr);
        SDL_SetAppMetadata("Nox Map Viewer","0.1.2","org.noxcore.vita.viewer");
        const bool headless=!options.screenshot.empty();
        check(SDL_Init(headless ? 0 : SDL_INIT_VIDEO));
        if (headless) {
            surface=SDL_CreateSurface(width,height,SDL_PIXELFORMAT_RGBA32);
            if (!surface) throw std::runtime_error(SDL_GetError());
            renderer=SDL_CreateSoftwareRenderer(surface);
        } else {
            window=SDL_CreateWindow("Nox Map Viewer",width,height,0);
            if (!window) throw std::runtime_error(SDL_GetError());
#ifdef NOX_VITA
            renderer=SDL_CreateRenderer(window,"VITA gxm");
#else
            renderer=SDL_CreateRenderer(window,nullptr);
#endif
        }
        if (!renderer) throw std::runtime_error(SDL_GetError());
        if (!headless) SDL_SetRenderVSync(renderer,1);
        SDL_Log("Nox Map Viewer 0.1.2; renderer=%s; stock assets only",SDL_GetRendererName(renderer));
        verifyStatusFormatting();
        beginFrame(renderer);
        text(renderer,24,235,"Nox Map Viewer - loading original game data...");
        text(renderer,24,260,"Expected: ux0:data/nox/thing.bin, video.bag, video.idx and maps/");
        SDL_RenderPresent(renderer);
        {
            Viewer viewer(renderer,options.gameDir);
            viewer.load(options.map.is_absolute() ? options.map : options.gameDir/options.map);
            std::size_t mapIndex=0;
            for (std::size_t i=0;i<viewer.maps().size();++i)
                if (viewer.maps()[i].stem()==options.map.stem()) mapIndex=i;
            viewer.camera.zoom=options.zoom;
            const auto initial=viewer.camera;
            auto switchMap=[&](int direction) {
                if (viewer.maps().empty()) return;
                const auto next=static_cast<std::size_t>((static_cast<int>(mapIndex)+direction+static_cast<int>(viewer.maps().size()))%static_cast<int>(viewer.maps().size()));
                beginFrame(renderer); text(renderer,24,250,"Loading "+viewer.maps()[next].stem().string()+"..."); SDL_RenderPresent(renderer);
                viewer.load(viewer.maps()[next]); mapIndex=next;
            };
            auto zoom=[&](int direction) {viewer.stepZoom(direction);};
            bool running=true,paused=false;
            unsigned frame=0;
            std::uint32_t previousButtons=0;
            (void)previousButtons;
            std::uint64_t previous=SDL_GetTicksNS(),windowStart=previous;
            unsigned windowFrames=0;
            double fps=0,workMs=0;
            std::string pools=memoryPools(),notice;
            std::vector<double> timings;
            while (running) {
                const auto started=SDL_GetTicksNS();
                const float dt=std::min(.05f,static_cast<float>(started-previous)/1e9f); previous=started;
                SDL_Event event;
                while (SDL_PollEvent(&event)) {
                    if (event.type==SDL_EVENT_QUIT) running=false;
                    if (event.type==SDL_EVENT_WILL_ENTER_BACKGROUND) paused=true;
                    if (event.type==SDL_EVENT_DID_ENTER_FOREGROUND) {paused=false;previous=SDL_GetTicksNS();}
                    if (event.type==SDL_EVENT_KEY_DOWN && !event.key.repeat) {
                        try {
                            switch(event.key.key) {
                            case SDLK_ESCAPE: running=false; break;
                            case SDLK_N: switchMap(1); break;
                            case SDLK_P: switchMap(-1); break;
                            case SDLK_EQUALS: zoom(1); break;
                            case SDLK_MINUS: zoom(-1); break;
                            case SDLK_R: viewer.resetCamera(); break;
                            case SDLK_C: viewer.clearCaches(); break;
                            default: break;
                            }
                        } catch(const std::exception& e) {notice=e.what(); SDL_Log("Map selection failed: %s",e.what());}
                    }
                }
                if (!running) break;
                if (paused) {SDL_Delay(30);continue;}
                float dx=0,dy=0;
#ifdef NOX_VITA
                SceCtrlData pad{};
                if (sceCtrlPeekBufferPositive(0,&pad,1)>0) {
                    const auto pressed=pad.buttons & ~previousButtons;
                    previousButtons=pad.buttons;
                    if ((pad.buttons & (SCE_CTRL_START|SCE_CTRL_SELECT))==(SCE_CTRL_START|SCE_CTRL_SELECT)) running=false;
                    auto axis=[](int v) {const float a=(v-128)/127.f;return std::abs(a)<.18f ? 0.f : a;};
                    dx=axis(pad.lx);dy=axis(pad.ly);
                    if (pad.buttons&SCE_CTRL_LEFT) dx=-1;
                    if (pad.buttons&SCE_CTRL_RIGHT) dx=1;
                    if (pad.buttons&SCE_CTRL_UP) dy=-1;
                    if (pad.buttons&SCE_CTRL_DOWN) dy=1;
                    try {
                        if (pressed&SCE_CTRL_CROSS) switchMap(1);
                        if (pressed&SCE_CTRL_CIRCLE) switchMap(-1);
                        if (pressed&SCE_CTRL_RTRIGGER) zoom(1);
                        if (pressed&SCE_CTRL_LTRIGGER) zoom(-1);
                        if (pressed&SCE_CTRL_SQUARE) viewer.resetCamera();
                        if (pressed&SCE_CTRL_TRIANGLE) viewer.clearCaches();
                    } catch(const std::exception& e) {notice=e.what();SDL_Log("Map selection failed: %s",e.what());}
                }
#else
                if (!headless) {
                    const bool* keys=SDL_GetKeyboardState(nullptr);
                    dx=static_cast<float>(keys[SDL_SCANCODE_RIGHT]||keys[SDL_SCANCODE_D])-static_cast<float>(keys[SDL_SCANCODE_LEFT]||keys[SDL_SCANCODE_A]);
                    dy=static_cast<float>(keys[SDL_SCANCODE_DOWN]||keys[SDL_SCANCODE_S])-static_cast<float>(keys[SDL_SCANCODE_UP]||keys[SDL_SCANCODE_W]);
                }
#endif
                viewer.camera.x+=dx*360*dt/viewer.camera.zoom;
                viewer.camera.y+=dy*360*dt/viewer.camera.zoom;
                if (headless && options.tour) {
                    const double phase=frame*.08;
                    viewer.camera.x=initial.x+static_cast<float>(std::sin(phase)*700);
                    viewer.camera.y=initial.y+static_cast<float>(std::cos(phase)*500);
                }
                beginFrame(renderer);
                viewer.draw();
                workMs=(SDL_GetTicksNS()-started)/1e6;
                if (headless) timings.push_back(workMs);
                panel(renderer,0,72);
                text(renderer,12,10,"NOX / VITA MAP VIEWER     "+viewer.mapName()+"     ORIGINAL ASSETS");
                char buf[220];
                std::snprintf(buf,sizeof(buf),"%.1f FPS  |  %.2f ms work  |  %.3fx  |  %u tiles / %u walls / %u objects  |  uploads %u",fps,workMs,viewer.camera.zoom,viewer.stats.tiles,viewer.stats.walls,viewer.stats.objects,viewer.stats.uploads);
                text(renderer,12,28,buf);
                std::snprintf(buf,sizeof(buf),"Texture cache %.2f MiB / 32  |  decoded %.2f / 8  |  segments %.2f / 4",
                    viewer.stats.textureBytes/1048576.,viewer.stats.decodedBytes/1048576.,viewer.stats.segmentBytes/1048576.);
                text(renderer,12,46,buf);
                panel(renderer,height-48,48);
                text(renderer,12,height-40,pools);
#ifdef NOX_VITA
                text(renderer,12,height-20,"Stick: pan  X/O: map  L/R: zoom  Square: reset  Triangle: cache  Start+Select: exit");
#else
                text(renderer,12,height-20,"WASD/arrows: pan  N/P: map  +/-: zoom  R: reset  C: clear cache  Esc: exit");
#endif
                if (!notice.empty()) {panel(renderer,height-74,24);text(renderer,12,height-68,notice.substr(0,110));}
                check(SDL_RenderPresent(renderer));
                ++frame; ++windowFrames;
                const auto now=SDL_GetTicksNS();
                if (now-windowStart>=1000000000ull) {
                    fps=windowFrames*1e9/static_cast<double>(now-windowStart); windowFrames=0;windowStart=now;pools=memoryPools();
                    if (!headless) SDL_Log(statusFormat,
                        viewer.mapName().c_str(),fps,workMs,static_cast<unsigned>(viewer.stats.textureBytes),static_cast<unsigned>(viewer.stats.decodedBytes),
                        static_cast<unsigned>(viewer.stats.segmentBytes),pools.c_str());
                }
                if (headless && frame>=static_cast<unsigned>(options.frames)) running=false;
            }
            if (headless) {
                check(SDL_SavePNG(surface,options.screenshot.string().c_str()));
                auto sorted=timings;std::sort(sorted.begin(),sorted.end());
                const double mean=timings.empty()?0:std::accumulate(timings.begin(),timings.end(),0.)/timings.size();
                const double p95=sorted.empty()?0:sorted[std::min(sorted.size()-1,static_cast<std::size_t>(sorted.size()*.95))];
                std::ofstream receipt(options.screenshot.string()+".json");
                receipt << "{\n  \"map\": \"" << viewer.mapName() << "\",\n  \"frames\": " << frame
                    << ",\n  \"map_tiles\": " << viewer.tileCount() << ",\n  \"map_walls\": " << viewer.wallCount()
                    << ",\n  \"zoom\": " << viewer.camera.zoom << ",\n  \"view_width\": " << width/viewer.camera.zoom
                    << ",\n  \"view_height\": " << height/viewer.camera.zoom << ",\n  \"map_objects\": " << viewer.objectCount()
                    << ",\n  \"visible_objects\": " << viewer.stats.objects
                    << ",\n  \"texture_bytes\": " << viewer.stats.textureBytes << ",\n  \"decoded_bytes\": " << viewer.stats.decodedBytes
                    << ",\n  \"segment_bytes\": " << viewer.stats.segmentBytes << ",\n  \"desktop_work_mean_ms\": " << mean
                    << ",\n  \"desktop_work_p95_ms\": " << p95 << ",\n  \"hardware_verified\": false\n}\n";
                if (!receipt) throw std::runtime_error("Could not write screenshot receipt");
                SDL_Log("Headless verification passed; frames=%u; textures=%u; mean=%.2fms p95=%.2fms (desktop)",frame,static_cast<unsigned>(viewer.stats.textures),mean,p95);
            }
        }
    } catch(const std::exception& e) {
        SDL_Log("ERROR: %s",e.what());result=1;
        if (renderer && window) {
            bool done=false;
            while (!done) {
                SDL_Event event;while(SDL_PollEvent(&event)) if(event.type==SDL_EVENT_QUIT || event.type==SDL_EVENT_KEY_DOWN) done=true;
#ifdef NOX_VITA
                SceCtrlData pad{};sceCtrlPeekBufferPositive(0,&pad,1);if(pad.buttons&SCE_CTRL_START) done=true;
#endif
                beginFrame(renderer);
                text(renderer,24,200,"Nox Map Viewer could not load the map:");
                const std::string message=e.what();
                for (std::size_t i=0;i<message.size() && i<440;i+=110) text(renderer,24,230+static_cast<float>(i/110)*18,message.substr(i,110));
                text(renderer,24,345,"Copy your original data to ux0:data/nox/ (no HD pack needed).");
                text(renderer,24,375,"See ux0:data/nox-vita/viewer.log. Press Start to exit.");
                SDL_RenderPresent(renderer);SDL_Delay(30);
            }
        }
    }
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    if (surface) SDL_DestroySurface(surface);
    SDL_Quit();
    if(logFile) std::fclose(logFile);
    return result;
}
