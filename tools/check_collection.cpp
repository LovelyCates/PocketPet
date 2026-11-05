#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <fstream>
#include <iostream>
#include <set>
#include "../src/render.h"
#include "../third_party/picojson.h"
using namespace pet;
int wmain(int argc,wchar_t** argv) {
    if(argc!=3) return 2;
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    Gdiplus::GdiplusStartupInput input; ULONG_PTR token; Gdiplus::GdiplusStartup(&token,&input,nullptr);
    int failures=0;
    {
        fs::path output=argv[2]; fs::create_directories(output);
        auto manifests=discoverPackages({argv[1]});
        Image gallery; gallery.width=960; gallery.height=(int)((manifests.size()+3)/4)*260;
        gallery.pixels.resize(size_t(gallery.width)*gallery.height,0xff182126);
        Gdiplus::Bitmap canvas(gallery.width,gallery.height,gallery.width*4,PixelFormat32bppPARGB,(BYTE*)gallery.pixels.data());
        picojson::array reports;
        {
            Gdiplus::Graphics g(&canvas); g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            Gdiplus::FontFamily family(L"Microsoft YaHei UI"); Gdiplus::Font font(&family,17,Gdiplus::FontStyleRegular,Gdiplus::UnitPixel);
            Gdiplus::SolidBrush text(Gdiplus::Color(255,229,239,230)),card(Gdiplus::Color(255,34,47,53));
            int index=0;
            for(const auto& manifest:manifests) {
                try {
                    auto p=loadPackage(manifest); unsigned checked=0; std::set<uint64_t> signatures;
                    for(const auto& name:{"idle","running-right","running-left","waving","jumping","failed","waiting","running","review"}) {
                        auto it=p.tracks.find(name); if(it==p.tracks.end()) throw std::runtime_error("Missing standard track");
                        uint64_t hash=1469598103934665603ull;
                        for(int f:it->second.frames) {
                            unsigned occupied=0; int ox=f%p.columns*p.frameWidth,oy=f/p.columns*p.frameHeight;
                            for(int y=0;y<p.frameHeight;++y) for(int x=0;x<p.frameWidth;++x) {
                                auto pixel=p.image.pixels[size_t(oy+y)*p.image.width+ox+x];
                                occupied+=(pixel>>24)>24; hash=(hash^pixel)*1099511628211ull;
                            }
                            if(!occupied) throw std::runtime_error("Required animation frame is empty");
                            ++checked;
                        }
                        signatures.insert(hash);
                        Visual v; v.action=name; v.loopAction=true; v.actionTime=.25;
                        auto preview=renderPet(p,v);
                        if(preview.pixels.empty()) throw std::runtime_error("Rendered surface is empty");
                    }
                    Visual v; v.reducedMotion=true; auto thumb=renderPet(p,v);
                    Gdiplus::Bitmap sprite(thumb.width,thumb.height,thumb.width*4,PixelFormat32bppPARGB,(BYTE*)thumb.pixels.data());
                    int x=index%4*240,y=index/4*260;
                    g.FillRectangle(&card,x+7,y+7,226,246);
                    g.DrawImage(&sprite,Gdiplus::Rect(x+30,y+25,180,195),32,90,192,208,Gdiplus::UnitPixel);
                    Gdiplus::StringFormat format; format.SetAlignment(Gdiplus::StringAlignmentCenter);
                    g.DrawString(p.name.c_str(),-1,&font,Gdiplus::RectF((float)x+10,(float)y+222,220,28),&format,&text);
                    picojson::object report; report["id"]=picojson::value(narrow(manifest.parent_path().filename().wstring())); report["name"]=picojson::value(narrow(p.name));
                    report["width"]=picojson::value((double)p.image.width); report["height"]=picojson::value((double)p.image.height);
                    report["framesChecked"]=picojson::value((double)checked); report["distinctTrackSignatures"]=picojson::value((double)signatures.size());
                    report["valid"]=picojson::value(true); reports.emplace_back(report);
                    std::cout<<"PASS "<<narrow(manifest.parent_path().filename().wstring())<<" frames="<<checked<<" distinctTracks="<<signatures.size()<<'\n'; ++index;
                } catch(const std::exception& e) { ++failures; std::cout<<"FAIL "<<narrow(manifest.wstring())<<": "<<e.what()<<'\n'; }
            }
        }
        savePng(gallery,output/L"pet-gallery.png");
        std::ofstream report(output/L"validation.json",std::ios::binary); report<<picojson::value(reports).serialize(true);
        std::cout<<reports.size()<<" packages valid; "<<failures<<" failed\n";
    }
    Gdiplus::GdiplusShutdown(token); CoUninitialize(); return failures?1:0;
}
