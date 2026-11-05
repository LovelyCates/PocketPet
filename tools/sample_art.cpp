#include "sample_art.h"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <fstream>
#include <webp/encode.h>
using namespace Gdiplus;
namespace pet {
static void oval(Graphics& g, Color c, float x,float y,float w,float h) { SolidBrush b(c); g.FillEllipse(&b,x,y,w,h); }
static void line(Graphics& g, Color c, float width,float x,float y,float a,float b) { Pen p(c,width); p.SetStartCap(LineCapRound); p.SetEndCap(LineCapRound); g.DrawLine(&p,x,y,a,b); }
static void rounded(Graphics& g, Color c, float x,float y,float w,float h,float r) {
    GraphicsPath path; path.AddArc(x,y,r*2,r*2,180,90); path.AddArc(x+w-2*r,y,2*r,2*r,270,90);
    path.AddArc(x+w-2*r,y+h-2*r,2*r,2*r,0,90); path.AddArc(x,y+h-2*r,2*r,2*r,90,90); path.CloseFigure();
    SolidBrush brush(c); g.FillPath(&brush,&path);
}
static void nativePet(Graphics& g, int kind, const Visual& v) {
    bool fox = kind == 2;
    Color edge(255,53,68,69), fur = fox ? Color(255,222,143,99) : Color(255,248,243,229);
    Color light = fox ? Color(255,251,223,184) : Color(255,255,253,245);
    Color accent = fox ? Color(255,126,164,143) : Color(255,131,184,165);
    double phase = v.reducedMotion ? 0 : v.time;
    float breath = v.reducedMotion ? 0.f : float(std::sin(phase*2.2)*1.4);
    float gait = v.walking ? float(std::sin(phase*15)*4) : 0;
    float tail = float(std::sin(phase*1.7)*(v.happy ? 12 : 5));
    GraphicsState pose = g.Save();
    g.TranslateTransform(0,breath);
    Pen tailEdge(edge,18); tailEdge.SetStartCap(LineCapRound); tailEdge.SetEndCap(LineCapRound);
    Pen tailFill(fur,13); tailFill.SetStartCap(LineCapRound); tailFill.SetEndCap(LineCapRound);
    GraphicsPath tp; tp.AddBezier(135.f,165.f,179.f,177.f,180.f,125.f,162.f+tail,130.f);
    g.DrawPath(&tailEdge,&tp); g.DrawPath(&tailFill,&tp);
    if (fox) oval(g,light,155+tail*.3f,124,17,20);
    oval(g,edge,54,119,85,68); oval(g,fur,57,121,79,63);
    oval(g,light,76,135,43,45);
    oval(g,edge,53,174+gait,37,18); oval(g,fur,56,175+gait,31,13);
    oval(g,edge,103,174-gait,37,18); oval(g,fur,106,175-gait,31,13);
    line(g,Color(130,126,133,122),1.4f,68,180+gait,68,185+gait);
    line(g,Color(130,126,133,122),1.4f,118,180-gait,118,185-gait);
    GraphicsPath ears;
    ears.AddBezier(42.f,86.f,34.f,73.f,36.f,43.f,42.f,35.f);
    ears.AddBezier(42.f,35.f,53.f,34.f,69.f,54.f,77.f,64.f); ears.CloseFigure();
    ears.StartFigure(); ears.AddBezier(116.f,64.f,127.f,49.f,143.f,32.f,151.f,35.f);
    ears.AddBezier(151.f,35.f,158.f,46.f,159.f,74.f,151.f,88.f); ears.CloseFigure();
    SolidBrush fb(fur); Pen outline(edge,3); outline.SetLineJoin(LineJoinRound);
    g.FillPath(&fb,&ears); g.DrawPath(&outline,&ears);
    GraphicsPath inside; inside.AddPolygon(std::vector<PointF>{{44,47},{46,77},{66,64}}.data(),3);
    inside.StartFigure(); inside.AddPolygon(std::vector<PointF>{{147,47},{129,65},{149,78}}.data(),3);
    SolidBrush pink(Color(255,226,173,163)); g.FillPath(&pink,&inside);
    LinearGradientBrush head(Rect(32,61,130,103),light,fur,LinearGradientModeVertical);
    g.FillEllipse(&head,32,60,130,104); g.DrawEllipse(&outline,32,60,130,104);
    if (fox) {
        GraphicsPath muzzle; muzzle.AddBezier(37.f,114.f,62.f,111.f,73.f,126.f,96.f,132.f);
        muzzle.AddBezier(96.f,132.f,115.f,122.f,140.f,109.f,157.f,114.f);
        muzzle.AddBezier(157.f,114.f,152.f,148.f,128.f,160.f,97.f,161.f);
        muzzle.AddBezier(97.f,161.f,65.f,160.f,43.f,146.f,37.f,114.f); muzzle.CloseFigure();
        SolidBrush mb(light); g.FillPath(&mb,&muzzle);
    } else {
        line(g,Color(190,186,190,174),3,91,66,88,76); line(g,Color(190,186,190,174),3,103,65,101,76);
    }
    float lx = float(std::clamp(v.lookX,-1.,1.)*3), ly = float(std::clamp(v.lookY,-1.,1.)*2);
    bool blink = std::fmod(phase+1.2,5.7) < .15;
    if (v.sleeping || v.happy || blink) {
        Pen eyes(edge,3); eyes.SetStartCap(LineCapRound); eyes.SetEndCap(LineCapRound);
        g.DrawArc(&eyes,59,101,19,12,v.happy?190.f:10.f,160.f);
        g.DrawArc(&eyes,115,101,19,12,v.happy?190.f:10.f,160.f);
    } else {
        oval(g,edge,63+lx,99+ly,10,15); oval(g,edge,120+lx,99+ly,10,15);
        oval(g,Color(255,255,255,246),65+lx,101+ly,3,4); oval(g,Color(255,255,255,246),122+lx,101+ly,3,4);
    }
    oval(g,Color(115,231,148,139),45,119,19,9); oval(g,Color(115,231,148,139),132,119,19,9);
    oval(g,Color(255,123,94,88),91,120,10,7);
    Pen mouth(edge,1.8f); g.DrawArc(&mouth,83,123,13,11,0,160); g.DrawArc(&mouth,96,123,13,11,20,160);
    if (!fox) { line(g,Color(190,126,136,126),1.5,38,120,51,123); line(g,Color(190,126,136,126),1.5,143,123,157,119); }
    rounded(g,edge,64,153,66,13,6); rounded(g,accent,66,155,62,9,4);
    oval(g,edge,89,158,15,17); oval(g,Color(255,230,187,99),91,159,11,13);
    line(g,Color(255,143,109,60),1.4f,96.5f,166,96.5f,169);
    if (v.happy || v.action == "waving") {
        float wave = float(std::sin(phase*9)*8);
        oval(g,edge,137,128+wave,20,30); oval(g,fur,140,130+wave,14,24);
        oval(g,Color(255,220,166,156),144,138+wave,6,8);
    } else if (v.dragged) {
        oval(g,edge,47,151,19,30); oval(g,fur,50,153,13,24);
        oval(g,edge,129,151,19,30); oval(g,fur,132,153,13,24);
    }
    g.Restore(pose);
    if (v.sleeping) {
        FontFamily family(L"Segoe UI"); Font font(&family,14,FontStyleBold,UnitPixel); SolidBrush ink(Color(210,135,175,158));
        g.DrawString(L"z",-1,&font,PointF(153,50-float(std::fmod(phase,2)*4)),&ink);
    }
}
void exportDemoAtlas(const fs::path& folder, int builtin, int version, bool webp) {
    fs::create_directories(folder);
    Image image; image.width = 1536; image.height = version == 2 ? 2288 : 1872;
    image.pixels.resize(size_t(image.width)*image.height);
    Bitmap bitmap(image.width,image.height,image.width*4,PixelFormat32bppPARGB,(BYTE*)image.pixels.data());
    {
        Graphics g(&bitmap); g.SetSmoothingMode(SmoothingModeAntiAlias); g.Clear(Color(0,0,0,0));
        const int counts[] = {6,8,8,4,5,8,6,6,6,8,8};
        for (int row = 0; row < (version == 2 ? 11 : 9); ++row) for (int col = 0; col < counts[row]; ++col) {
            auto state = g.Save(); g.TranslateTransform(float(col*192),float(row*208));
            Visual v; v.time = col*.20; v.happy = row == 3; v.walking = row == 1 || row == 2; v.sleeping = row == 6;
            if (row >= 9) { double angle = (row-9)*3.141592653589793+col*3.141592653589793/8; v.lookX=std::sin(angle); v.lookY=-std::cos(angle); }
            if (row == 4) g.TranslateTransform(0,float(-std::sin(col*3.14159265/4)*20));
            if (row == 2) { g.TranslateTransform(192,0); g.ScaleTransform(-1,1); }
            nativePet(g,builtin,v); g.Restore(state);
        }
    }
    std::string sheet = webp ? "spritesheet.webp" : "spritesheet.png";
    if (webp) {
        auto pixels = image.pixels;
        for (auto& p : pixels) { unsigned a=p>>24; if (a) p=(a<<24)|(std::min(255u,((p>>16)&255)*255/a)<<16)|(std::min(255u,((p>>8)&255)*255/a)<<8)|std::min(255u,(p&255)*255/a); }
        uint8_t* encoded = nullptr;
        size_t length = WebPEncodeLosslessBGRA((const uint8_t*)pixels.data(),image.width,image.height,image.width*4,&encoded);
        if (!length) throw std::runtime_error("WebP demo encoding failed");
        std::ofstream file(folder/sheet,std::ios::binary); file.write((char*)encoded,length); WebPFree(encoded);
    } else savePng(image,folder/sheet);
    std::ofstream manifest(folder/L"pet.json",std::ios::binary);
    manifest << "{\n  \"id\": \"cloud-cat-demo-v" << version << "\",\n  \"displayName\": \"" << narrow(builtin == 1 ? L"糯米 · 云朵猫" : L"栗子 · 小赤狐") << " · " << (webp?"WebP":"PNG") << " v" << version << "\",\n  \"description\": \"PocketPet original sample. A portable sprite atlas.\",\n  \"spriteVersionNumber\": " << version << ",\n  \"spritesheetPath\": \"" << sheet << "\"\n}\n";
}

}
