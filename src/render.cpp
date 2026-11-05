#include "render.h"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <fstream>

using namespace Gdiplus;
namespace pet {
void cleanGreenFringe(Image& image) {
    // Opt-in display filter for green-screen contamination at the silhouette.
    // Work in premultiplied colour, and preserve opaque interior green details.
    const auto original=image.pixels;
    for(int y=0;y<image.height;++y) for(int x=0;x<image.width;++x) {
        auto& pixel=image.pixels[size_t(y)*image.width+x];
        unsigned a=pixel>>24,r=(pixel>>16)&255,g=(pixel>>8)&255,b=pixel&255;
        unsigned neutral=std::max(r,b);
        if(a==0 || g<=neutral*1.3+5*a/255.) continue;
        bool boundary=false;
        for(int dy=-2;dy<=2 && !boundary;++dy) for(int dx=-2;dx<=2;++dx) {
            int nx=x+dx,ny=y+dy;
            if(nx<0 || ny<0 || nx>=image.width || ny>=image.height || (original[size_t(ny)*image.width+nx]>>24)<16) { boundary=true; break; }
        }
        if(!boundary) continue;
        unsigned excess=g-neutral, newAlpha=a-excess;
        pixel=newAlpha<16?0:(newAlpha<<24)|(std::min(r,newAlpha)<<16)|(std::min(neutral,newAlpha)<<8)|std::min(b,newAlpha);
    }
}
static void rounded(Graphics& g, Color c, float x,float y,float w,float h,float r) {
    GraphicsPath path; path.AddArc(x,y,r*2,r*2,180,90); path.AddArc(x+w-2*r,y,2*r,2*r,270,90);
    path.AddArc(x+w-2*r,y+h-2*r,2*r,2*r,0,90); path.AddArc(x,y+h-2*r,2*r,2*r,90,90); path.CloseFigure();
    SolidBrush brush(c); g.FillPath(&brush,&path);
}
Image renderPet(const Package& p, const Visual& v) {
    Image out; out.width = std::max(1,(int)std::lround(256*v.scale)); out.height = std::max(1,(int)std::lround(300*v.scale));
    out.pixels.resize(size_t(out.width)*out.height);
    Bitmap bitmap(out.width,out.height,out.width*4,PixelFormat32bppPARGB,(BYTE*)out.pixels.data());
    Graphics g(&bitmap); g.SetSmoothingMode(SmoothingModeAntiAlias); g.SetPixelOffsetMode(PixelOffsetModeHalf);
    g.SetInterpolationMode(InterpolationModeHighQualityBicubic); g.Clear(Color(0,0,0,0));
    g.TranslateTransform(float(out.width*.5),float(out.height-12*v.scale)); g.ScaleTransform(float(v.scale),float(v.scale));
    if (!v.bubble.empty()) {
        auto state = g.Save();
        rounded(g,Color(242,38,50,54),-106,-261,212,39,13);
        PointF triangle[]={{-6,-223},{7,-223},{0,-217}}; SolidBrush bb(Color(242,38,50,54)); g.FillPolygon(&bb,triangle,3);
        FontFamily family(L"Microsoft YaHei UI"); Font font(&family,12,FontStyleRegular,UnitPixel);
        StringFormat fmt; fmt.SetAlignment(StringAlignmentCenter); fmt.SetLineAlignment(StringAlignmentCenter);
        SolidBrush text(Color(255,233,242,232)); g.DrawString(v.bubble.c_str(),-1,&font,RectF(-103,-260,206,37),&fmt,&text); g.Restore(state);
    }
    auto state = g.Save();
    float squash = v.reducedMotion ? 0.f : float(v.impact*.20);
    g.RotateTransform(float(v.tilt)); g.ScaleTransform(1+squash,1-squash);
    {
        if (v.sleeping) g.ScaleTransform(1.07f,.88f);
        std::string action=v.sleeping && p.tracks.count("sleeping")?"sleeping":v.action;
        auto track=p.tracks.find(action); if(track==p.tracks.end()) { action="idle"; track=p.tracks.find(action); }
        double timeline=v.sleeping?v.actionTime*.15:v.actionTime;
        if(v.loopAction && !v.sleeping && track->second.length()>0) timeline=std::fmod(timeline,track->second.length());
        int idx = v.reducedMotion ? track->second.frames.front() : frameAt(p.tracks,action,timeline);
        if (p.version == 2 && p.columns == 8 && p.rows == 11 && v.action == "idle" && std::hypot(v.lookX,v.lookY)>.2 && !v.sleeping && !v.reducedMotion)
            idx = 72+lookDirection(v.lookX,v.lookY);
        double size = std::min(152.0/p.visibleHeight,176.0/p.visibleWidth);
        g.ScaleTransform((float)size,(float)size); g.TranslateTransform((float)-p.anchorX,(float)-p.anchorY);
        // A source rectangle does not bound GDI+'s bicubic filter footprint.
        // Isolate this frame with transparent padding so rotation/downsampling
        // cannot pick up another atlas cell and draw a rectangular fringe.
        constexpr int padding=2;
        Image tile; tile.width=p.frameWidth+2*padding; tile.height=p.frameHeight+2*padding;
        tile.pixels.resize(size_t(tile.width)*tile.height);
        int sourceX=(idx%p.columns)*p.frameWidth, sourceY=(idx/p.columns)*p.frameHeight;
        for(int y=0;y<p.frameHeight;++y) {
            auto source=p.image.pixels.begin()+size_t(sourceY+y)*p.image.width+sourceX;
            auto destination=tile.pixels.begin()+size_t(y+padding)*tile.width+padding;
            std::copy_n(source,p.frameWidth,destination);
        }
        if(p.cleanGreenEdges) cleanGreenFringe(tile);
        Bitmap sprite(tile.width,tile.height,tile.width*4,PixelFormat32bppPARGB,(BYTE*)tile.pixels.data());
        ImageAttributes attr; attr.SetWrapMode(WrapModeTileFlipXY);
        RectF dest((float)-padding,(float)-padding,(float)tile.width,(float)tile.height);
        g.DrawImage(&sprite,dest,0.f,0.f,(float)tile.width,(float)tile.height,UnitPixel,&attr);
    }
    g.Restore(state);
    return out;
}

}
