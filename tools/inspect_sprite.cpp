#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <iostream>
#include <iomanip>
#include "../src/render.h"
using namespace pet;
int wmain(int argc,wchar_t** argv) {
    if(argc!=3) return 2;
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    Gdiplus::GdiplusStartupInput input; ULONG_PTR token; Gdiplus::GdiplusStartup(&token,&input,nullptr);
    int result=0;
    try {
        auto p=loadPackage(argv[1]); fs::path folder=argv[2]; fs::create_directories(folder);
        savePng(p.image,folder/L"decoded-source.png");
        unsigned totalGreen=0,totalEdge=0;
        for(int index=0;index<p.columns*p.rows;++index) {
            int ox=index%p.columns*p.frameWidth,oy=index/p.columns*p.frameHeight;
            unsigned green=0,edge=0; int maxRow=0,maxRowY=0;
            for(int y=0;y<p.frameHeight;++y) {
                int rowCount=0;
                for(int x=0;x<p.frameWidth;++x) {
                    uint32_t pixel=p.image.pixels[size_t(oy+y)*p.image.width+ox+x];
                    unsigned a=pixel>>24,r=(pixel>>16)&255,g=(pixel>>8)&255,b=pixel&255;
                    bool fringe=a>10 && g>r*1.3+5 && g>b*1.3+5;
                    green+=fringe;
                    if(a>10) {
                        ++rowCount;
                        if(x<2 || y<2 || x>=p.frameWidth-2 || y>=p.frameHeight-2) ++edge;
                    }
                }
                if(rowCount>maxRow) { maxRow=rowCount; maxRowY=y; }
            }
            if(green || edge) std::cout<<"frame "<<index<<" green="<<green<<" frame-edge="<<edge<<" widest-row="<<maxRow<<" at y="<<maxRowY<<'\n';
            totalGreen+=green; totalEdge+=edge;
            if(index==0 || index==1 || index==24 || index==48) {
                auto isolated=p; isolated.tracks["idle"]=Track{{index},{1},true,"idle"};
                Visual visual; visual.scale=2; isolated.cleanGreenEdges=false;
                savePng(renderPet(isolated,visual),folder/(L"frame-"+std::to_wstring(index)+L".png"));
                isolated.cleanGreenEdges=true;
                savePng(renderPet(isolated,visual),folder/(L"frame-"+std::to_wstring(index)+L"-clean.png"));
            }
        }
        for(const auto& action:{"idle","running","review","waiting","failed"}) {
            Visual v; v.action=action; v.scale=2; v.actionTime=.3;
            p.cleanGreenEdges=true;
            savePng(renderPet(p,v),folder/(widen(action)+L"-state.png"));
        }
        std::cout<<"TOTAL green="<<totalGreen<<" frame-edge="<<totalEdge<<'\n';
    } catch(const std::exception& e) { std::cout<<e.what()<<'\n'; result=1; }
    Gdiplus::GdiplusShutdown(token); CoUninitialize(); return result;
}
