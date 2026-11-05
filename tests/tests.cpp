#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <fstream>
#include <iostream>
#include <functional>
#include "../src/assets.h"
#include "../src/render.h"
#include "../tools/sample_art.h"
#include "../src/companion.h"
#include <set>
using namespace pet;
static int passed=0, failed=0;
static void require(bool value,const char* what) { if (!value) throw std::runtime_error(what); }
static void test(const char* name,const std::function<void()>& fn) {
    try { fn(); ++passed; std::cout << "PASS " << name << '\n'; }
    catch (const std::exception& e) { ++failed; std::cout << "FAIL " << name << ": " << e.what() << '\n'; }
}
static void rejects(const std::function<void()>& fn) { bool rejected=false; try { fn(); } catch (const std::exception&) { rejected=true; } require(rejected,"Expected rejection"); }
int main() {
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    Gdiplus::GdiplusStartupInput input; ULONG_PTR token; Gdiplus::GdiplusStartup(&token,&input,nullptr);
    auto root=fs::current_path(); auto fixtures=root/L"build"/L"fixtures"; fs::create_directories(fixtures);
    test("throw velocity uses recent movement and clamps speed",[] {
        ThrowHistory h; h.add({0,0},0); h.add({100,20},.1); auto v=h.velocity(.1); require(std::abs(v.x-1000)<.1,"Wrong velocity");
        h.add({1000,1000},.11); v=h.velocity(.11); require(std::hypot(v.x,v.y)<=2400.001,"Throw speed not clamped");
        v=h.velocity(.5); require(v.x==0 && v.y==0,"Stationary drag should not throw");
    });
    test("gravity bounces, loses energy, then settles",[] {
        Body b; b.p={500,500}; b.launch({600,100}); Bounds area{0,0,1000,900}; double previous=1e9; unsigned seen=0;
        for (int i=0;i<2400;++i) { b.step(1./240,area); if (b.bounces!=seen) { require(std::abs(b.v.y)<previous,"Bounce gained energy"); previous=std::abs(b.v.y); seen=b.bounces; } }
        require(b.grounded && b.p.y==900 && b.v.x==0,"Pet did not settle"); require(b.bounces>=2 && b.bounces<=6,"Unexpected bounce count");
    });
    test("physics is stable at 30 and 144 fps",[] {
        Body a,b; a.p=b.p={500,450}; a.launch({500,-300}); b.launch({500,-300}); Bounds area{0,0,1200,1000};
        for(int i=0;i<90;++i) a.step(1./30,area); for(int i=0;i<432;++i) b.step(1./144,area);
        require(std::abs(a.p.x-b.p.x)<8 && std::abs(a.p.y-b.p.y)<8,"Frame rate changed trajectory too much");
    });
    test("negative monitor coordinates and wall containment",[] {
        Body b; b.p={-1800,400}; b.launch({-2200,-200}); Bounds area{-1920,-200,0,880};
        for(int i=0;i<1000;++i) { b.step(.01,area,1.5); require(b.p.x>=area.left+b.width/2 && b.p.x<=area.right-b.width/2,"Escaped side"); require(b.p.y<=880 && b.p.y>=-200+b.height,"Escaped vertical boundary"); }
    });
    test("animation fallback, loop and directional look",[] {
        std::map<std::string,Track> ts{{"idle",{{0,1},{.5,.5},true,"idle"}},{"wave",{{24,25},{.1,.1},false,"idle"}}};
        require(frameAt(ts,"wave",.15)==25,"Wave wrong"); require(frameAt(ts,"wave",.8)==1,"Fallback wrong"); require(frameAt(ts,"idle",1.6)==1,"Loop wrong");
        require(lookDirection(0,-1)==0 && lookDirection(1,0)==4 && lookDirection(0,1)==8 && lookDirection(-1,0)==12,"Look order wrong");
    });
    test("generate portable PNG v1 and WebP v2 samples",[&] {
        exportDemoAtlas(fixtures/L"sample-pets"/L"cloud-cat-v1",1,1,false);
        exportDemoAtlas(fixtures/L"sample-pets"/L"fox-v2",2,2,true);
        auto a=loadPackage(fixtures/L"sample-pets"/L"cloud-cat-v1"); auto b=loadPackage(fixtures/L"sample-pets"/L"fox-v2");
        require(a.rows==9 && a.image.width==1536,"PNG v1 failed"); require(b.rows==11 && b.version==2 && b.image.height==2288,"WebP v2 failed");
        require(a.anchorY>175 && a.anchorY<205 && b.visibleHeight>100,"Ground anchor wrong");
        require(discoverPackages({fixtures/L"sample-pets"}).size()>=2,"Sample discovery failed");
    });
    test("transparent renderer uses external PNG and WebP packages",[&] {
        int index=0;
        for(auto p : {loadPackage(root/L"pets"/L"cloud-cat-v1"),loadPackage(root/L"pets"/L"fox-v2")}) {
            Visual v; v.happy=true; v.bubble=L"你好呀"; auto image=renderPet(p,v);
            size_t opaque=0; for(auto px:image.pixels) opaque+=(px>>24)>0;
            require(opaque>3000 && opaque<image.pixels.size()/2,"Surface is blank or opaque");
            for(auto px:image.pixels) { unsigned a=px>>24; require(((px>>16)&255)<=a && ((px>>8)&255)<=a && (px&255)<=a,"Pixels are not premultiplied"); }
            savePng(image,fixtures/(L"external-preview-"+std::to_wstring(index++)+L".png"));
        }
    });
    auto bad=fixtures/L"坏的角色包"; fs::create_directories(bad);
    auto write=[&](const std::string& text) { std::ofstream f(bad/L"pet.json",std::ios::binary); f<<text; };
    test("malformed package rejected without replacing active pet",[&] {
        auto active=loadPackage(root/L"pets"/L"cloud-cat-v1"); auto original=active.manifest;
        write("{broken"); rejects([&] { active=loadPackage(bad); }); require(active.manifest==original,"Active pet was lost");
    });
    test("manifest traversal and absolute paths rejected",[&] {
        write("{\"spritesheetPath\":\"../outside.png\"}"); rejects([&] { loadPackage(bad); });
        write("{\"spritesheetPath\":\"C:/outside.png\"}"); rejects([&] { loadPackage(bad); });
    });
    test("custom grid and animation metadata validated",[&] {
        Image image; image.width=32; image.height=16; image.pixels.resize(512,0xff88bb99); savePng(image,bad/L"sheet.png");
        write("{\"displayName\":\"test\",\"spritesheetPath\":\"sheet.png\",\"frame\":{\"width\":16,\"height\":16,\"columns\":2,\"rows\":1},\"animations\":{\"idle\":{\"frames\":[0,1],\"fps\":4,\"loop\":true}}}");
        auto p=loadPackage(bad); require(p.columns==2 && p.tracks.at("idle").frames.size()==2,"Custom grid failed");
        write("{\"spritesheetPath\":\"sheet.png\",\"frame\":{\"width\":16,\"height\":16,\"columns\":2,\"rows\":1},\"animations\":{\"idle\":{\"frames\":[3]}}}"); rejects([&] { loadPackage(bad); });
        write("{\"spritesheetPath\":\"sheet.png\"}"); rejects([&] { loadPackage(bad); });
    });
    test("blank idle rejected",[&] {
        Image image; image.width=1536; image.height=1872; image.pixels.resize(size_t(image.width)*image.height); savePng(image,bad/L"empty.png");
        write("{\"spritesheetPath\":\"empty.png\"}"); rejects([&] { loadPackage(bad); });
    });
    auto local=fixtures/(L"external-only-"+std::to_wstring(GetTickCount64()))/L"pets";
    test("missing pets folder has no embedded fallback",[&] {
        rejects([&] { loadInitialPackage(local); });
        fs::create_directories(local); rejects([&] { loadInitialPackage(local); });
    });
    test("import copies an external role into local pets without modifying source",[&] {
        auto source=root/L"pets"/L"fox-v2"/L"pet.json"; auto original=readText(source);
        auto installed=storePackage(source,local); auto role=loadPackage(installed);
        require(installed.parent_path().parent_path()==fs::weakly_canonical(local),"Import is outside local pets");
        require(role.version==2 && role.image.height==2288,"Imported role failed to load");
        require(readText(source)==original,"Source manifest was changed");
        require(storePackage(installed,local)==installed,"Already-local role duplicated");
        auto again=storePackage(source,local); require(again!=installed && fs::exists(installed),"Import overwrote existing role");
    });
    test("startup restores local choice and falls back if selection was removed",[&] {
        auto roles=discoverPackages({local}); require(roles.size()==2,"Expected two installed packages");
        require(loadInitialPackage(local,roles[1]).manifest==roles[1],"Preferred role not restored");
        require(loadInitialPackage(local,local/L"missing"/L"pet.json").manifest==roles[0],"Missing selection did not fall back locally");
    });
    test("replacing an external image changes the role without changing any executable",[&] {
        auto folder=local/L"editable"; fs::create_directories(folder);
        { std::ofstream f(folder/L"pet.json"); f<<"{\"spritesheetPath\":\"sheet.png\",\"frame\":{\"width\":16,\"height\":16,\"columns\":1,\"rows\":1}}"; }
        Image image; image.width=image.height=16; image.pixels.resize(256,0xff88bb99); savePng(image,folder/L"sheet.png");
        auto a=loadPackage(folder); image.pixels.assign(256,0xffcc6677); savePng(image,folder/L"sheet.png");
        auto b=loadPackage(folder); require(a.image.pixels!=b.image.pixels,"New external artwork did not take effect");
    });
    test("scaled sprite frames never sample neighboring atlas cells",[&] {
        Package p; p.columns=p.rows=3; p.frameWidth=p.frameHeight=32;
        p.anchorX=16; p.anchorY=32; p.visibleWidth=p.visibleHeight=32;
        p.image.width=p.image.height=96; p.image.pixels.resize(96*96,0xff00ff00);
        // Transparent current cell, small red sprite, solid green neighboring cells.
        // Any green in the output is an atlas filtering leak, not character artwork.
        for(int y=32;y<64;++y) for(int x=32;x<64;++x) p.image.pixels[y*96+x]=0;
        for(int y=42;y<54;++y) for(int x=42;x<54;++x) p.image.pixels[y*96+x]=0xffff0000;
        p.tracks["idle"]=Track{{4},{1},true,"idle"};
        for(double scale : {.75,1.,1.3}) for(double tilt : {0.,11.}) {
            Visual v; v.scale=scale; v.tilt=tilt;
            auto image=renderPet(p,v); unsigned leaked=0;
            for(auto pixel:image.pixels) leaked+=((pixel>>8)&255)>0;
            savePng(image,fixtures/(L"atlas-border-"+std::to_wstring(scale)+L"-"+std::to_wstring(tilt)+L".png"));
            require(leaked==0,"Neighboring cell pixels leaked into the transparent frame border");
        }
    });
    test("automatic companion uses multiple poses without showing failure randomly",[] {
        Companion c(42); std::set<std::string> seen;
        for(double t=0;t<1800;t+=.25) { auto i=c.update(t,true,true); seen.insert(i.action); require(i.action!="failed","Random failure reaction"); }
        for(auto name:{"idle","running","review","waving","running-left","running-right"}) require(seen.count(name)>0,"Automatic pose never played");
    });
    test("automatic rest wakes up and touch interrupts it",[] {
        Companion c(7); require(c.update(90,true,false).resting,"Did not rest after inactivity");
        require(c.update(100,true,false).resting,"Rest was too short");
        auto awake=c.update(125,true,false); require(!awake.resting && awake.action=="waving","Never woke from rest");
        c.interact(200); require(!c.update(201,true,false).resting,"Touch did not wake companion");
        require(c.update(290,true,false).resting,"Next rest was not rescheduled");
    });
    test("interaction and disabled walking take precedence over autonomous poses",[] {
        Companion c(1); c.interact(100);
        require(c.update(111,true,false).action=="idle","Reacted too soon after touch");
        auto before=c.update(112,true,false); require(before.direction==0,"Walked despite disabled roaming");
        auto blocked=c.update(200,false,false); require(blocked.action==before.action && blocked.resting==before.resting,"Autonomy advanced during flight or manual action");
    });
    test("green fringe cleanup removes stray edges and preserves character interiors",[] {
        Image image; image.width=image.height=32; image.pixels.resize(1024);
        for(int y=7;y<25;++y) for(int x=7;x<25;++x) image.pixels[y*32+x]=0xff00ff00;
        for(int y=8;y<24;++y) for(int x=8;x<24;++x) image.pixels[y*32+x]=0xffffaabb;
        image.pixels[16*32+16]=0xff00ff00; // An intentional green detail well inside the body.
        image.pixels[2*32+16]=0xff00ff00;  // Detached green noise above the character.
        auto before=image; cleanGreenFringe(image);
        require(image.pixels[7*32+16]==0 && image.pixels[2*32+16]==0,"Green fringe remained");
        require(image.pixels[16*32+16]==before.pixels[16*32+16],"Interior green detail was changed");
        require(image.pixels[12*32+12]==before.pixels[12*32+12],"Character colour was changed");
        for(auto p:image.pixels) { unsigned a=p>>24; require(((p>>16)&255)<=a && ((p>>8)&255)<=a && (p&255)<=a,"Cleanup broke premultiplied alpha"); }
    });
    test("manual form remains selected through time and interaction until resumed",[] {
        Companion c(42); c.hold("running",10);
        for(double t:{10.,100.,1000.}) { auto i=c.update(t,true,true); require(i.action=="running" && !i.resting && !i.direction,"Automatic behaviour interrupted selected form"); }
        c.interact(2000); require(c.update(2020,true,false).action=="running","Touch forgot selected form");
        c.resume(2100); require(!c.holding() && c.update(2101,true,true).action=="idle","Cannot resume automatic mode");
    });
    test("held one-shot animation loops instead of falling back to idle",[] {
        Package p; p.columns=2; p.rows=1; p.frameWidth=p.frameHeight=32;
        p.anchorX=16; p.anchorY=32; p.visibleWidth=p.visibleHeight=32;
        p.image.width=64; p.image.height=32; p.image.pixels.resize(2048);
        for(int y=0;y<32;++y) for(int x=0;x<64;++x) p.image.pixels[y*64+x]=x<32?0xffff0000:0xff0000ff;
        p.tracks["idle"]=Track{{0},{1},true,"idle"}; p.tracks["waving"]=Track{{1},{1},false,"idle"};
        Visual v; v.action="waving"; v.actionTime=.3; v.loopAction=true; auto first=renderPet(p,v);
        v.actionTime=20.3; auto held=renderPet(p,v); require(first.pixels==held.pixels,"Held pose reverted or changed");
        v.loopAction=false; auto settled=renderPet(p,v); require(settled.pixels!=held.pixels,"Automatic one-shot fallback stopped working");
    });
    test("an unavailable action falls back to the declared idle frame",[] {
        Package p; p.columns=2; p.rows=1; p.frameWidth=p.frameHeight=16;
        p.anchorX=8;p.anchorY=16;p.visibleWidth=p.visibleHeight=16;
        p.image.width=32;p.image.height=16;p.image.pixels.resize(512,0xffaacc99);
        p.tracks["idle"]=Track{{1},{1},true,"idle"};
        Visual v; auto idle=renderPet(p,v); v.action="missing"; auto fallback=renderPet(p,v);
        require(idle.pixels==fallback.pixels,"Missing action did not use the idle track");
    });
    test("a bare atlas already under pets becomes a discoverable package",[&] {
        auto loose=local/L"loose-atlas.png";
        fs::copy_file(root/L"pets"/L"cloud-cat-v1"/L"spritesheet.png",loose);
        auto source=readText(loose,20*1024*1024);
        auto installed=storePackage(loose,local);
        require(installed.filename()==L"pet.json" && fs::exists(installed),"Import left atlas without manifest");
        auto found=discoverPackages({local}); require(std::find(found.begin(),found.end(),installed)!=found.end(),"Imported atlas is absent from menu discovery");
        auto p=loadPackage(installed); require(p.version==1 && p.rows==9,"Automatic atlas layout detection failed");
        require(readText(loose,20*1024*1024)==source,"Source atlas was altered");
    });
    std::cout<<passed<<" passed, "<<failed<<" failed\n";
    Gdiplus::GdiplusShutdown(token); CoUninitialize(); return failed?1:0;
}
