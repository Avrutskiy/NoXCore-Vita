#include "viewer.hpp"
#include <iostream>
#include <stdexcept>

using namespace vita_viewer;
void require(bool ok,const char* why) {if (!ok) throw std::runtime_error(why);}
void mathChecks() {
    require(stickPan(128,128).x==0 && stickPan(140,140).y==0,"dead zone drift");
    require(stickPan(151,128).x>0 && stickPan(151,128).x<.01f,"dead zone speed jump");
    require(stickPan(0,128).x==-1 && stickPan(255,128).x==1,"asymmetric stick travel");
    float last=0;
    for (int x=128;x<=255;++x) {auto p=stickPan(x,128);require(p.x>=last,"non-monotonic stick speed");last=p.x;}
    for (int rate:{20,30,60,120}) {
        Camera a,b;
        for (int i=0;i<rate;++i) {panCamera(a,{1,0},1.f/rate);panCamera(b,{1,1},1.f/rate);}
        require(std::abs(a.x-240)<.01f,"pan speed depends on frame rate");
        require(std::abs(std::hypot(b.x,b.y)-a.x)<.01f,"diagonal speed boost");
    }
    for (const float zoom:zoomLevels) {
        Camera c{2000.25f,1500.75f,zoom};SceneWindow window;
        for (int i=0;i<2000;++i) {
            c.x+=.125f;c.y-=.0625f;
            if (!window.contains(c)) {window.place(c);window.valid=true;}
            require(window.contains(c),"cache does not cover viewport and filter border");
            const float u=c.x-width/(2*zoom)-window.x;
            const float screen=((2000.f-window.x)-u)*zoom;
            const float expected=(2000.f-c.x)*zoom+width/2;
            require(std::abs(screen-expected)<.001f,"camera phase changed at scene rebase");
        }
    }
}

std::vector<unsigned char> capture(SDL_Renderer* renderer) {
    auto* raw=SDL_RenderReadPixels(renderer,nullptr);
    require(raw!=nullptr,SDL_GetError());
    auto* pixels=SDL_ConvertSurface(raw,SDL_PIXELFORMAT_RGBA32);SDL_DestroySurface(raw);
    require(pixels!=nullptr,SDL_GetError());
    std::vector<unsigned char> result(width*height*4);
    for (int y=0;y<height;++y)
        std::copy_n(static_cast<unsigned char*>(pixels->pixels)+y*pixels->pitch,width*4,result.data()+y*width*4);
    SDL_DestroySurface(pixels);return result;
}
double difference(const std::vector<unsigned char>& a,const std::vector<unsigned char>& b) {
    double total=0;
    for (std::size_t i=0;i<a.size();++i) total+=std::abs(int(a[i])-int(b[i]));
    return total/a.size();
}
int main(int argc,char** argv) {
    try {
        mathChecks();
        require(argc>=2,"usage: nox_scroll_test GAME_DIR [--gpu]");
        const bool gpu=argc>2 && std::string(argv[2])=="--gpu";
        require(SDL_Init(gpu ? SDL_INIT_VIDEO : 0),SDL_GetError());
        auto* window=gpu ? SDL_CreateWindow("Scrolling regression",width,height,SDL_WINDOW_HIDDEN) : nullptr;
        auto* surface=gpu ? nullptr : SDL_CreateSurface(width,height,SDL_PIXELFORMAT_RGBA32);
        auto* renderer=gpu ? SDL_CreateRenderer(window,nullptr) : SDL_CreateSoftwareRenderer(surface);
        require(renderer!=nullptr,SDL_GetError());
        if (gpu) require(std::string(SDL_GetRendererName(renderer))!="software","GPU checks require accelerated sampling");
        std::cout<<"renderer="<<SDL_GetRendererName(renderer)<<'\n';
        {
            Viewer viewer(renderer,argv[1]);
            viewer.load(std::filesystem::path(argv[1])/"maps/WAR01A/WAR01A.MAP");
            const auto start=viewer.camera;
            for (const float zoom:zoomLevels) {
                viewer.camera=start;viewer.camera.zoom=zoom;
                viewer.clearCaches();viewer.draw();auto pixels=capture(renderer);
                const auto built=viewer.stats.sceneRebuilds;
                for (int i=0;i<8;++i) {
                    viewer.camera.x+=.125f;viewer.draw();auto next=capture(renderer);
                    if (gpu) require(difference(pixels,next)>.001,"subpixel camera movement froze");
                    require(viewer.stats.sceneRebuilds==built,"subpixel pan redrew the scene");
                    pixels=std::move(next);
                }
                // Rebuild at exactly the same world view, after one whole unit of pan.
                // Cached and freshly composed pictures must agree across an origin change.
                viewer.clearCaches();viewer.draw();auto rebuilt=capture(renderer);
                const auto error=difference(pixels,rebuilt);
                require(error<.05,"cache rebase moved or changed scene pixels");
                std::cout<<"zoom="<<zoom<<" phase/rebase PASS; mean pixel error="<<error<<'\n';
            }
            viewer.camera=start;
            const auto before=viewer.stats.sceneRebuilds;
            for (int i=0;i<180;++i) {panCamera(viewer.camera,{1,0},1.f/60);viewer.draw();}
            const auto rebuilds=viewer.stats.sceneRebuilds-before;
            require(rebuilds<15,"scene cache ineffective during sustained scrolling");
            require(viewer.stats.textureBytes<=32u<<20,"texture cache exceeded budget");
            viewer.resetCamera();require(viewer.camera.zoom==1.5f,"wrong reset zoom");
            viewer.load(std::filesystem::path(argv[1])/"maps/CON01A/CON01A.MAP");
            const auto mapBefore=viewer.stats.sceneRebuilds;viewer.draw();
            require(viewer.stats.sceneRebuilds==mapBefore+1,"map switch reused stale scene");
            std::cout<<"180-frame pan: "<<rebuilds<<" scene rebuilds; reset/map invalidation PASS\n";
        }
        SDL_DestroyRenderer(renderer);SDL_DestroySurface(surface);SDL_DestroyWindow(window);SDL_Quit();
        std::cout<<"Scrolling regression checks passed\n";return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
