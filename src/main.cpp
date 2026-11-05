#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <commdlg.h>
#include <shlobj.h>
#include <gdiplus.h>
#include <fstream>
#include <random>
#include <chrono>
#include <sstream>
#include "assets.h"
#include "render.h"
#include "companion.h"
#include "../third_party/picojson.h"

namespace {
using namespace pet;
constexpr UINT TrayMessage = WM_APP+1, ShowMessage = WM_APP+2;
constexpr wchar_t ClassName[] = L"PocketPet.Desktop.v1";
enum Command { Import=100, Folder, Refresh, Jump, Wave, Sleep, Roam, Physics, Small, Medium, Large, Topmost, Quiet, Fullscreen, Hide, Reset, Exit, Help, AutoCompanion, CleanEdges, ResumeAuto, PoseFirst=300, Imported=1000 };
struct Pose { const char* action; const wchar_t* label; };
constexpr Pose poses[]={{"idle",L"发呆待机"},{"running-right",L"向右散步"},{"running-left",L"向左散步"},{"waving",L"挥手打招呼"},{"jumping",L"开心跳一跳"},{"failed",L"失落一小会"},{"waiting",L"期待地等你"},{"running",L"专注思考"},{"review",L"观察与检查"}};
double now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
fs::path exeFolder() { wchar_t path[32768]; GetModuleFileNameW(nullptr,path,32768); return fs::path(path).parent_path(); }
fs::path environment(const wchar_t* name) { wchar_t value[32768]; DWORD n = GetEnvironmentVariableW(name,value,32768); return n && n<32768 ? fs::path(value) : fs::path{}; }
struct Settings {
    double x = -1e9, y = -1e9, size = 1;
    bool roaming = true, physics = true, topmost = true, quiet = false, hideFullscreen = false;
    bool autoCompanion=true;
    std::map<std::string,bool> edgeOverrides;
    fs::path package;
};
struct App {
    HWND window = nullptr;
    HICON icon = nullptr;
    HDC memoryDC = nullptr;
    HBITMAP dib = nullptr, previousBitmap = nullptr;
    void* dibPixels = nullptr;
    int surfaceW = 0, surfaceH = 0;
    Package package;
    Settings settings;
    fs::path config, diagnostics;
    Body body; ThrowHistory history;
    Image frame;
    double dpi = 1, started = now(), last = now(), rendered = 0, lastInteraction = now(), lastSave = 0, lastSystemCheck = 0;
    double roamUntil = 0, actionStart = now(), happyUntil = 0, bubbleUntil = 0, manualUntil=0;
    std::string action = "idle"; std::wstring bubble;
    Vec grab, downPoint;
    bool dragging = false, moved = false, sleeping = false, manualHidden = false, fullscreenHidden = false, menuOpen = false, closing = false;
    bool forceRender = true, manualRest=false; double autoExit = 0;
    Companion companion{std::random_device{}()};
    std::vector<fs::path> discovered;
    std::vector<std::wstring> discoveredNames;
    std::vector<std::wstring> discoveredGroups;
    UINT taskbarCreated = 0;

    double scale() const { return dpi*settings.size; }
    void log(const std::string& event) {
        if (diagnostics.empty()) return;
        std::ofstream f(diagnostics,std::ios::app);
        f << "{\"event\":\"" << event << "\",\"seconds\":" << now()-started << ",\"x\":" << body.p.x << ",\"y\":" << body.p.y << ",\"vx\":" << body.v.x << ",\"vy\":" << body.v.y << ",\"grounded\":" << (body.grounded?"true":"false") << ",\"bounces\":" << body.bounces << ",\"pet\":" << picojson::value(narrow(package.name)).serialize() << ",\"manifest\":" << picojson::value(narrow(package.manifest.wstring())).serialize() << ",\"action\":" << picojson::value(action).serialize() << ",\"resting\":" << (sleeping?"true":"false") << ",\"cleanGreenEdges\":" << (package.cleanGreenEdges?"true":"false") << "}\n";
    }
    Bounds workArea(bool updateDpi = false) {
        POINT point{(LONG)body.p.x,(LONG)body.p.y};
        if (body.p.x < -1e8) GetCursorPos(&point);
        HMONITOR monitor = MonitorFromPoint(point,MONITOR_DEFAULTTONEAREST);
        MONITORINFO info{sizeof(info)}; GetMonitorInfoW(monitor,&info);
        if (updateDpi && window) dpi = GetDpiForWindow(window)/96.;
        return {(double)info.rcWork.left,(double)info.rcWork.top,(double)info.rcWork.right,(double)info.rcWork.bottom};
    }
    void dimensions() { body.width = 172*scale(); body.height = 168*scale(); }
    void loadSettings() {
        try {
            if (!fs::exists(config)) return;
            picojson::value root; auto error = picojson::parse(root,readText(config));
            if (!error.empty() || !root.is<picojson::object>()) return;
            auto number = [&](const char* key,double fallback,double min,double max) { auto& v=root.get(key); return v.is<double>() && std::isfinite(v.get<double>()) ? std::clamp(v.get<double>(),min,max) : fallback; };
            auto boolean = [&](const char* key,bool fallback) { auto& v=root.get(key); return v.is<bool>() ? v.get<bool>() : fallback; };
            settings.x=number("x",-1e9,-1e9,1e6); settings.y=number("y",-1e9,-1e9,1e6); settings.size=number("size",1,.65,1.5);
            settings.roaming=boolean("roaming",true); settings.physics=boolean("physics",true); settings.topmost=boolean("topmost",true);
            settings.quiet=boolean("quiet",false); settings.hideFullscreen=boolean("hideFullscreen",false);
            settings.autoCompanion=boolean("autoCompanion",true);
            if(root.get("edgeOverrides").is<picojson::object>()) for(auto& [key,value]:root.get("edgeOverrides").get<picojson::object>()) if(value.is<bool>()) settings.edgeOverrides[key]=value.get<bool>();
            if (root.get("package").is<std::string>()) settings.package=widen(root.get("package").get<std::string>());
        } catch (...) { /* Corrupt preferences should never prevent a pet from starting. */ }
    }
    void saveSettings() {
        if (closing) return;
        try {
            picojson::object root;
            root["x"]=picojson::value(body.p.x); root["y"]=picojson::value(body.p.y); root["size"]=picojson::value(settings.size);
            root["roaming"]=picojson::value(settings.roaming); root["physics"]=picojson::value(settings.physics); root["topmost"]=picojson::value(settings.topmost);
            root["quiet"]=picojson::value(settings.quiet); root["hideFullscreen"]=picojson::value(settings.hideFullscreen);
            root["package"]=picojson::value(narrow(settings.package.wstring()));
            root["autoCompanion"]=picojson::value(settings.autoCompanion);
            picojson::object overrides; for(auto& [key,value]:settings.edgeOverrides) overrides[key]=picojson::value(value);
            root["edgeOverrides"]=picojson::value(overrides);
            fs::create_directories(config.parent_path()); fs::path temp=config; temp+=L".tmp";
            { std::ofstream f(temp,std::ios::binary); f << picojson::value(root).serialize(true); if (!f) return; }
            MoveFileExW(temp.c_str(),config.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
        } catch (...) {}
    }
    void setAction(std::string name) { if (action != name) { action=std::move(name); actionStart=now(); forceRender=true; } }
    void say(const std::wstring& text,double seconds=3) { if (settings.quiet) return; bubble=text; bubbleUntil=now()+seconds; forceRender=true; }
    void wake() { sleeping=false; manualRest=false; lastInteraction=now(); roamUntil=0; manualUntil=now()+.8; companion.interact(now()-started); }
    void applyRenderOptions() {
        auto it=settings.edgeOverrides.find(narrow(settings.package.wstring()));
        if(it!=settings.edgeOverrides.end()) package.cleanGreenEdges=it->second;
    }
    void jump() { wake(); body.launch({body.v.x,-750*scale()}); setAction("jumping"); say(L"起飞！",1.5); log("jump"); }
    void select(Package next) {
        // Decode/validate before this point. Only swap assets; physics remains intact.
        log("switch_begin");
        package=std::move(next);
        settings.package=fs::relative(package.manifest.empty()?package.sheet:package.manifest,exeFolder());
        applyRenderOptions(); wake();
        if(companion.holding() && !package.tracks.count(companion.heldAction())) companion.resume(now()-started);
        setAction(companion.holding()?companion.heldAction():"idle"); actionStart=now(); forceRender=true;
        SetWindowTextW(window,(L"PocketPet · "+package.name).c_str());
        say(L"换好啦，我还在这里。",3);
        saveSettings(); log("switch");
    }
    void import(const fs::path& path) {
        try { auto next=loadPackage(storePackage(path,exeFolder()/L"pets")); select(std::move(next)); refreshPackages(); }
        catch (const std::exception& e) {
            log("import_failed");
            MessageBoxW(window,(L"这个角色包没有加载成功，原来的宠物还在。\n\n"+widen(e.what())).c_str(),L"PocketPet · 导入角色",MB_OK|MB_ICONINFORMATION|MB_SETFOREGROUND);
        }
    }
    void pickFile() {
        wchar_t path[32768]{};
        OPENFILENAMEW dialog{sizeof(dialog)}; dialog.hwndOwner=window; dialog.lpstrFile=path; dialog.nMaxFile=32768;
        dialog.lpstrFilter=L"宠物包 / 精灵图\0pet.json;avatar.json;*.png;*.webp\0JSON\0*.json\0PNG / WebP\0*.png;*.webp\0\0";
        dialog.lpstrTitle=L"选择 pet.json 或 PNG / WebP 精灵图"; dialog.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
        if (GetOpenFileNameW(&dialog)) import(path);
    }
    std::vector<fs::path> roots() {
        return {exeFolder()/L"pets"};
    }
    void refreshPackages() {
        discovered=discoverPackages(roots()); discoveredNames.clear(); discoveredGroups.clear();
        for (auto& path : discovered) {
            try {
                picojson::value doc; auto error=picojson::parse(doc,readText(path));
                if(!error.empty() || !doc.is<picojson::object>()) throw std::runtime_error("Invalid manifest");
                const auto& name=doc.get("displayName"); const auto& group=doc.get("collection");
                auto title=name.is<std::string>()?widen(name.get<std::string>()):path.parent_path().filename().wstring();
                auto collection=group.is<std::string>()?widen(group.get<std::string>()):L"我的伙伴";
                discoveredNames.push_back(std::move(title)); discoveredGroups.push_back(std::move(collection));
            }
            catch (...) { discoveredNames.push_back(path.parent_path().filename().wstring()+L"（待检查）"); discoveredGroups.push_back(L"我的伙伴"); }
        }
    }
    void tray(bool add) {
        NOTIFYICONDATAW data{sizeof(data)}; data.hWnd=window; data.uID=1; data.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP;
        data.uCallbackMessage=TrayMessage; data.hIcon=icon;
        wcscpy_s(data.szTip,L"PocketPet · 右键打开小窝\n拖动甩出去 · 双击跳跃 · 滚轮缩放");
        Shell_NotifyIconW(add?NIM_ADD:NIM_DELETE,&data);
        if (add) { data.uVersion=NOTIFYICON_VERSION_4; Shell_NotifyIconW(NIM_SETVERSION,&data); }
    }
    void releaseSurface() {
        if (memoryDC && previousBitmap) SelectObject(memoryDC,previousBitmap);
        if (dib) DeleteObject(dib); if (memoryDC) DeleteDC(memoryDC);
        memoryDC=nullptr; dib=nullptr; previousBitmap=nullptr; dibPixels=nullptr; surfaceW=surfaceH=0;
    }
    void draw(double t) {
        POINT cursor{}; GetCursorPos(&cursor);
        Visual visual; visual.time=t-started; visual.scale=scale(); visual.actionTime=t-actionStart;
        visual.impact=body.impact; visual.dragged=dragging; visual.airborne=!body.grounded;
        visual.sleeping=sleeping; visual.happy=t<happyUntil; visual.walking=roamUntil>t && body.grounded;
        visual.direction=body.v.x<0?-1:1; visual.reducedMotion=settings.quiet;
        double dx=cursor.x-body.p.x, dy=cursor.y-(body.p.y-100*scale());
        double distance=std::hypot(dx,dy);
        if (distance>25*scale() && distance<650*scale()) { visual.lookX=dx/std::max(distance,1.); visual.lookY=dy/std::max(distance,1.); }
        visual.tilt=settings.quiet ? 0 : dragging ? std::clamp((body.p.x-cursor.x)*.13,-12.,12.) : !body.grounded ? std::clamp(body.v.x/(75*scale()),-15.,15.) : 0;
        visual.action=action; visual.bubble=t<bubbleUntil?bubble:L"";
        visual.loopAction=companion.holding() && action==companion.heldAction();
        frame=renderPet(package,visual);
        if (frame.width!=surfaceW || frame.height!=surfaceH) {
            releaseSurface(); memoryDC=CreateCompatibleDC(nullptr);
            BITMAPINFO info{}; info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth=frame.width; info.bmiHeader.biHeight=-frame.height;
            info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32; info.bmiHeader.biCompression=BI_RGB;
            dib=CreateDIBSection(memoryDC,&info,DIB_RGB_COLORS,&dibPixels,nullptr,0);
            if (!dib || !memoryDC) throw std::runtime_error("Cannot allocate transparent window surface");
            previousBitmap=(HBITMAP)SelectObject(memoryDC,dib); surfaceW=frame.width; surfaceH=frame.height;
        }
        memcpy(dibPixels,frame.pixels.data(),frame.pixels.size()*4);
        POINT destination{(LONG)std::lround(body.p.x-frame.width*.5),(LONG)std::lround(body.p.y-frame.height+12*scale())};
        POINT source{}; SIZE size{frame.width,frame.height}; BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
        UpdateLayeredWindow(window,nullptr,&destination,&size,memoryDC,&source,0,&blend,ULW_ALPHA);
        rendered=t; forceRender=false;
    }
    bool fullscreen() {
        HWND fg=GetForegroundWindow();
        if (!fg || fg==window || IsIconic(fg)) return false;
        // Maximized work windows can fill a monitor with an auto-hidden taskbar.
        // Their normal resize frame distinguishes them from exclusive/borderless full screen.
        if (IsZoomed(fg) && (GetWindowLongPtrW(fg,GWL_STYLE)&WS_THICKFRAME)) return false;
        wchar_t name[128]{}; GetClassNameW(fg,name,128);
        if (wcscmp(name,L"Progman")==0 || wcscmp(name,L"WorkerW")==0 || wcscmp(name,L"Shell_TrayWnd")==0) return false;
        RECT rect{}; GetWindowRect(fg,&rect); MONITORINFO info{sizeof(info)};
        GetMonitorInfoW(MonitorFromWindow(fg,MONITOR_DEFAULTTONEAREST),&info);
        return rect.left<=info.rcMonitor.left && rect.top<=info.rcMonitor.top && rect.right>=info.rcMonitor.right && rect.bottom>=info.rcMonitor.bottom;
    }
    void tick() {
        double t=now(), dt=std::clamp(t-last,0.,.10); last=t;
        if (autoExit>0 && t-started>=autoExit) { DestroyWindow(window); return; }
        if (menuOpen) return;
        if (t-lastSystemCheck>1) {
            lastSystemCheck=t; bool hidden=settings.hideFullscreen && fullscreen();
            if (hidden!=fullscreenHidden) { fullscreenHidden=hidden; ShowWindow(window,hidden || manualHidden?SW_HIDE:SW_SHOWNOACTIVATE); forceRender=true; log(hidden?"fullscreen_hide":"fullscreen_show"); }
        }
        if (!dragging && !manualHidden && !fullscreenHidden) {
            auto bounds=workArea();
            bool oldGround=body.grounded; unsigned oldBounces=body.bounces;
            if (!settings.physics) { body.v={}; body.grounded=true; body.constrain(bounds); }
            else body.step(dt,bounds,scale());
            if (body.bounces!=oldBounces) { log("bounce"); forceRender=true; }
            if (!oldGround && body.grounded) { setAction("idle"); log("landed"); }
            bool available=body.grounded && t>happyUntil && t>manualUntil && !manualRest;
            if(available) {
                bool wasResting=sleeping;
                auto intent=companion.update(t-started,(settings.autoCompanion || companion.holding()) && !settings.quiet,settings.roaming && settings.physics);
                if((!settings.autoCompanion || settings.quiet) && !companion.holding()) intent={};
                sleeping=intent.resting;
                if(sleeping!=wasResting) { forceRender=true; log(sleeping?"rest_begin":"rest_end"); }
                if(intent.direction && settings.physics) {
                    if(action!="running-left" && action!="running-right") body.v.x=intent.direction*65*scale();
                    roamUntil=t+.1;
                } else roamUntil=0;
                if(action!=intent.action) { setAction(intent.action); log("auto_action"); }
            }
            if (roamUntil>t && settings.physics && body.grounded) {
                if (body.p.x<=bounds.left+body.width*.5+4) body.v.x=65*scale();
                if (body.p.x>=bounds.right-body.width*.5-4) body.v.x=-65*scale();
                body.v.x=(body.v.x<0?-1:1)*65*scale(); setAction(body.v.x<0?"running-left":"running-right");
            } else if (body.grounded && t>manualUntil && (action=="running-left" || action=="running-right")) setAction("idle");
        }
        double interval=dragging || !body.grounded || roamUntil>t ? .015 : sleeping || settings.quiet ? .25 : .09;
        if (!manualHidden && !fullscreenHidden && (forceRender || t-rendered>=interval)) draw(t);
        if (t-lastSave>5 && !dragging) { lastSave=t; saveSettings(); }
    }
    void reset() {
        auto area=workArea(); dimensions(); body.p={area.right-220*scale(),area.bottom}; body.v={}; body.grounded=true;
        body.constrain(area); manualHidden=false; wake();
        ShowWindow(window,SW_SHOWNOACTIVATE); forceRender=true; say(L"我回来啦。",2); log("reset");
    }
    void resize(double size) {
        settings.size=std::clamp(size,.65,1.5); dimensions(); body.constrain(workArea()); forceRender=true; saveSettings(); log("resize");
    }
    void command(UINT id) {
        wake();
        if (id>=Imported && id<Imported+discovered.size()) { import(discovered[id-Imported]); return; }
        if(id>=PoseFirst && id<PoseFirst+sizeof(poses)/sizeof(poses[0])) {
            auto name=std::string(poses[id-PoseFirst].action);
            if(name=="jumping") jump();
            else {
                setAction(name); actionStart=now(); manualUntil=now()+(name=="running"?15:8);
                if(name!="running-left" && name!="running-right") companion.hold(name,now()-started);
                if(name=="running-left" || name=="running-right") { manualUntil=now()+3; roamUntil=manualUntil; body.v.x=(name=="running-left"?-1:1)*65*scale(); }
                say(poses[id-PoseFirst].label,2); log("manual_action");
            }
            return;
        }
        switch(id) {
        case Import: pickFile(); break;
        case Folder: { auto folder=exeFolder()/L"pets"; fs::create_directories(folder); ShellExecuteW(nullptr,L"open",folder.c_str(),nullptr,nullptr,SW_SHOWNORMAL); break; }
        case Refresh: refreshPackages(); say(L"角色列表已更新。",2); break;
        case Jump: jump(); break;
        case Wave: happyUntil=now()+3; manualUntil=happyUntil; setAction("waving"); say(L"摸摸收到啦 ♡"); break;
        case Sleep: sleeping=true; manualRest=true; body.v.x=0; setAction("idle"); say(L"我先眯一会儿。摸摸我就醒。",2); break;
        case ResumeAuto: settings.autoCompanion=true; companion.resume(now()-started); setAction("idle"); say(L"恢复自动形态切换啦。",2); break;
        case AutoCompanion: if(companion.holding()) { settings.autoCompanion=true; companion.resume(now()-started); } else settings.autoCompanion=!settings.autoCompanion; say(settings.autoCompanion?L"我会自己找点事做。":L"我就在这里安静陪你。",2); break;
        case CleanEdges: package.cleanGreenEdges=!package.cleanGreenEdges; settings.edgeOverrides[narrow(settings.package.wstring())]=package.cleanGreenEdges; say(package.cleanGreenEdges?L"已开启这位伙伴的杂边净化。":L"已恢复素材原来的边缘。",2); break;
        case Roam: settings.roaming=!settings.roaming; if (!settings.roaming) { roamUntil=0; body.v.x=0; } break;
        case Physics: settings.physics=!settings.physics; break;
        case Small: resize(.75); break; case Medium: resize(1); break; case Large: resize(1.3); break;
        case Topmost: settings.topmost=!settings.topmost; SetWindowPos(window,settings.topmost?HWND_TOPMOST:HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE); break;
        case Quiet: settings.quiet=!settings.quiet; roamUntil=0; break;
        case Fullscreen: settings.hideFullscreen=!settings.hideFullscreen; lastSystemCheck=0; break;
        case Hide: manualHidden=true; ShowWindow(window,SW_HIDE); break;
        case Reset: reset(); break;
        case Exit: DestroyWindow(window); return;
        case Help: MessageBoxW(window,L"拖住身体：拎起 / 甩出去\n单击：摸摸\n双击：跳跃\n滚轮：调整大小\n右键：打开小窝\n\n把 pet.json、宠物文件夹或 PNG / WebP 精灵图拖到宠物上，即可换角色。\n角色统一保存在 EXE 同级的 pets 目录；导入会复制到这里。\n\n隐藏后可单击托盘图标找回。\n设置仅保存在本机，不联网。",L"PocketPet · 陪你摸会儿鱼",MB_OK|MB_SETFOREGROUND); break;
        }
        forceRender=true; saveSettings();
    }
    void menu(POINT point) {
        refreshPackages();
        HMENU root=CreatePopupMenu(), characters=CreatePopupMenu(), sizes=CreatePopupMenu(), behaviors=CreatePopupMenu(), actions=CreatePopupMenu();
        auto item=[](HMENU m,UINT id,const wchar_t* text,bool checked=false) { AppendMenuW(m,MF_STRING|(checked?MF_CHECKED:0),id,text); };
        std::map<std::wstring,HMENU> groups;
        for(size_t i=0;i<discovered.size() && i<100;++i) {
            auto label=discoveredGroups[i].empty()?L"我的伙伴":discoveredGroups[i];
            if(!groups.count(label)) groups[label]=CreatePopupMenu();
            item(groups[label],Imported+(UINT)i,discoveredNames[i].c_str(),package.manifest==discovered[i]);
        }
        for(auto& [label,submenu]:groups) AppendMenuW(characters,MF_POPUP,(UINT_PTR)submenu,label.c_str());
        AppendMenuW(characters,MF_SEPARATOR,0,nullptr); item(characters,Import,L"导入角色包…"); item(characters,Folder,L"打开角色文件夹"); item(characters,Refresh,L"刷新角色列表");
        item(sizes,Small,L"小小只 · 75%",settings.size<.85); item(sizes,Medium,L"刚刚好 · 100%",settings.size>=.85 && settings.size<1.15); item(sizes,Large,L"大一点 · 130%",settings.size>=1.15);
        item(behaviors,Roam,L"偶尔散步",settings.roaming); item(behaviors,Physics,L"重力与弹跳",settings.physics); item(behaviors,Topmost,L"保持置顶",settings.topmost);
        item(behaviors,Quiet,L"安静模式 · 减少动作和气泡",settings.quiet); item(behaviors,Fullscreen,L"全屏时自动躲起来",settings.hideFullscreen);
        item(behaviors,AutoCompanion,L"自动陪伴 · 活动、养神与醒来",settings.autoCompanion && !companion.holding());
        item(behaviors,CleanEdges,L"清理当前角色的绿色杂边",package.cleanGreenEdges);
        item(actions,ResumeAuto,L"恢复自动形态切换",!companion.holding() && settings.autoCompanion && !manualRest);
        AppendMenuW(actions,MF_SEPARATOR,0,nullptr);
        for(UINT i=0;i<sizeof(poses)/sizeof(poses[0]);++i) {
            item(actions,PoseFirst+i,poses[i].label,action==poses[i].action && !sleeping);
            if(!package.tracks.count(poses[i].action)) EnableMenuItem(actions,PoseFirst+i,MF_BYCOMMAND|MF_GRAYED);
        }
        AppendMenuW(actions,MF_SEPARATOR,0,nullptr); item(actions,Sleep,L"养神 / 小憩",sleeping);
        item(root,0,(L"  "+package.name).c_str()); EnableMenuItem(root,0,MF_BYCOMMAND|MF_GRAYED);
        AppendMenuW(root,MF_SEPARATOR,0,nullptr); AppendMenuW(root,MF_POPUP,(UINT_PTR)characters,L"换个伙伴");
        item(root,Wave,L"摸摸脑袋"); item(root,Jump,L"跳一下"); item(root,Sleep,L"睡个小觉");
        AppendMenuW(root,MF_POPUP,(UINT_PTR)actions,L"动作与形态");
        AppendMenuW(root,MF_POPUP,(UINT_PTR)sizes,L"宠物大小"); AppendMenuW(root,MF_POPUP,(UINT_PTR)behaviors,L"陪伴设置");
        AppendMenuW(root,MF_SEPARATOR,0,nullptr); item(root,Reset,L"回到屏幕边缘"); item(root,Hide,L"暂时藏起来"); item(root,Help,L"怎么玩"); item(root,Exit,L"退出 PocketPet");
        HWND previousForeground=GetForegroundWindow();
        menuOpen=true; SetForegroundWindow(window);
        UINT id=TrackPopupMenu(root,TPM_RETURNCMD|TPM_RIGHTBUTTON|TPM_NONOTIFY,point.x,point.y,0,window,nullptr);
        PostMessageW(window,WM_NULL,0,0); DestroyMenu(root); menuOpen=false; last=now();
        if (previousForeground && previousForeground!=window && IsWindow(previousForeground)) SetForegroundWindow(previousForeground);
        if (id) command(id);
    }
    void shutdown() { saveSettings(); closing=true; tray(false); releaseSurface(); if (icon) DestroyIcon(icon); log("exit"); }
};

LRESULT CALLBACK procedure(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    App* app=(App*)GetWindowLongPtrW(hwnd,GWLP_USERDATA);
    if (msg==WM_NCCREATE) { app=(App*)((CREATESTRUCTW*)lp)->lpCreateParams; app->window=hwnd; SetWindowLongPtrW(hwnd,GWLP_USERDATA,(LONG_PTR)app); }
    if (!app) return DefWindowProcW(hwnd,msg,wp,lp);
    if (app->taskbarCreated && msg==app->taskbarCreated) { app->tray(true); return 0; }
    switch(msg) {
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(hwnd,&ps); EndPaint(hwnd,&ps); return 0; }
    case WM_NCHITTEST: {
        POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)}; ScreenToClient(hwnd,&p);
        if (p.x>=0 && p.y>=0 && p.x<app->frame.width && p.y<app->frame.height && (app->frame.pixels[size_t(p.y)*app->frame.width+p.x]>>24)>20) return HTCLIENT;
        return HTTRANSPARENT;
    }
    case WM_TIMER: try { app->tick(); } catch (...) { KillTimer(hwnd,1); MessageBoxW(hwnd,L"绘制遇到了问题，请重新打开 PocketPet。",L"PocketPet",MB_OK); DestroyWindow(hwnd); } return 0;
    case WM_LBUTTONDOWN: {
        app->wake(); POINT p; GetCursorPos(&p); app->dragging=true; app->moved=false; app->downPoint={(double)p.x,(double)p.y};
        app->grab={p.x-app->body.p.x,p.y-app->body.p.y}; app->body.v={}; app->body.grounded=false;
        app->history.clear(); app->history.add({(double)p.x,(double)p.y},now()); SetCapture(hwnd); app->setAction("waiting"); app->forceRender=true; app->log("drag_begin"); return 0;
    }
    case WM_MOUSEMOVE: if (app->dragging) {
        POINT p; GetCursorPos(&p); app->history.add({(double)p.x,(double)p.y},now());
        if (std::hypot(p.x-app->downPoint.x,p.y-app->downPoint.y)>4*app->scale()) app->moved=true;
        app->body.p={p.x-app->grab.x,p.y-app->grab.y}; app->forceRender=true;
    } return 0;
    case WM_LBUTTONUP: if (app->dragging) {
        POINT p; GetCursorPos(&p); double t=now(); app->history.add({(double)p.x,(double)p.y},t);
        app->dragging=false; ReleaseCapture();
        if (app->moved) { app->body.launch(app->settings.physics?app->history.velocity(t,2400*app->scale()):Vec{}); app->setAction("jumping"); app->log("throw"); }
        else { app->body.grounded=std::abs(app->body.p.y-app->workArea().bottom)<.5; app->happyUntil=t+2.8; app->setAction("waving"); app->say(L"摸摸收到啦 ♡"); app->log("pet"); }
        app->body.constrain(app->workArea()); app->forceRender=true;
    } return 0;
    case WM_CAPTURECHANGED: if (app->dragging) { app->dragging=false; app->body.launch({}); app->setAction("idle"); app->log("drag_cancel"); } return 0;
    case WM_LBUTTONDBLCLK: app->dragging=false; ReleaseCapture(); app->jump(); return 0;
    case WM_RBUTTONUP: { POINT p; GetCursorPos(&p); app->menu(p); return 0; }
    case WM_CONTEXTMENU: { POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)}; if (p.x==-1 && p.y==-1) GetCursorPos(&p); app->menu(p); return 0; }
    case WM_MOUSEWHEEL: app->resize(app->settings.size+(GET_WHEEL_DELTA_WPARAM(wp)>0?.1:-.1)); return 0;
    case WM_DPICHANGED: app->dpi=HIWORD(wp)/96.; app->dimensions(); app->body.constrain(app->workArea()); app->forceRender=true; return 0;
    case WM_DISPLAYCHANGE: case WM_SETTINGCHANGE: app->body.constrain(app->workArea()); app->forceRender=true; return 0;
    case WM_DROPFILES: {
        HDROP drop=(HDROP)wp; wchar_t path[32768]; if (DragQueryFileW(drop,0,path,32768)) app->import(path); DragFinish(drop); return 0;
    }
    case WM_COPYDATA: {
        auto data=(COPYDATASTRUCT*)lp;
        if (data->dwData==1 && data->cbData>=sizeof(wchar_t) && data->cbData<65536 && data->cbData%sizeof(wchar_t)==0) {
            std::wstring path((wchar_t*)data->lpData,data->cbData/sizeof(wchar_t)-1);
            if (!path.empty()) app->import(path); else app->reset(); return TRUE;
        } return FALSE;
    }
    case ShowMessage: app->reset(); return 0;
    case TrayMessage: {
        UINT event=LOWORD(lp);
        if (event==WM_CONTEXTMENU || event==WM_RBUTTONUP) { POINT p; GetCursorPos(&p); app->menu(p); }
        else if (event==NIN_SELECT || event==NIN_KEYSELECT || event==WM_LBUTTONUP) { app->manualHidden=false; ShowWindow(hwnd,SW_SHOWNOACTIVATE); app->wake(); app->forceRender=true; }
        return 0;
    }
    case WM_COMMAND: app->command(LOWORD(wp)); return 0;
    case WM_CLOSE: DestroyWindow(hwnd); return 0;
    case WM_DESTROY: KillTimer(hwnd,1); app->shutdown(); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}
HICON makeIcon() {
    BITMAPV5HEADER header{}; header.bV5Size=sizeof(header); header.bV5Width=32; header.bV5Height=-32; header.bV5Planes=1; header.bV5BitCount=32; header.bV5Compression=BI_BITFIELDS;
    header.bV5RedMask=0x00ff0000; header.bV5GreenMask=0x0000ff00; header.bV5BlueMask=0x000000ff; header.bV5AlphaMask=0xff000000;
    void* pixels; HBITMAP color=CreateDIBSection(nullptr,(BITMAPINFO*)&header,DIB_RGB_COLORS,&pixels,nullptr,0);
    if (!color) return CopyIcon(LoadIconW(nullptr,IDI_APPLICATION));
    memset(pixels,0,32*32*4);
    {
        Gdiplus::Bitmap bitmap(32,32,128,PixelFormat32bppPARGB,(BYTE*)pixels); Gdiplus::Graphics g(&bitmap); g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        Gdiplus::SolidBrush fur(Gdiplus::Color(255,237,238,216)), inner(Gdiplus::Color(255,128,182,158)), ink(Gdiplus::Color(255,45,65,60));
        Gdiplus::Point ears[]={{4,15},{3,2},{13,9},{20,9},{29,2},{28,17}}; g.FillPolygon(&fur,ears,6); g.FillEllipse(&fur,3,8,26,22);
        g.FillEllipse(&ink,10,16,3,4); g.FillEllipse(&ink,20,16,3,4); g.FillEllipse(&inner,14,22,5,4);
    }
    HBITMAP mask=CreateBitmap(32,32,1,1,nullptr); ICONINFO info{TRUE,0,0,mask,color}; HICON icon=CreateIconIndirect(&info); DeleteObject(mask); DeleteObject(color); return icon;
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HRESULT com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    Gdiplus::GdiplusStartupInput input; ULONG_PTR token=0; Gdiplus::GdiplusStartup(&token,&input,nullptr);
    int result=0;
    try {
        int argc=0; auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);
        fs::path imported, preview; bool isolated=false, inspectable=false; App app;
        auto local=environment(L"LOCALAPPDATA"); if (local.empty()) local=exeFolder(); app.config=local/L"PocketPet"/L"settings.json";
        for (int i=1;i<argc;++i) {
            std::wstring arg=argv[i];
            if (arg==L"--settings" && i+1<argc) { app.config=fs::absolute(argv[++i]); isolated=true; }
            else if (arg==L"--diagnostics" && i+1<argc) app.diagnostics=fs::absolute(argv[++i]);
            else if (arg==L"--exit-after" && i+1<argc) app.autoExit=_wtof(argv[++i]);
            else if (arg==L"--preview" && i+1<argc) preview=fs::absolute(argv[++i]);
            else if (arg==L"--portable") app.config=exeFolder()/L"settings.json";
            else if (arg==L"--inspectable") inspectable=true;
            else if (arg==L"--pet" && i+1<argc) imported=fs::absolute(argv[++i]);
            else if (!arg.empty() && arg[0]!=L'-') imported=fs::absolute(arg);
        }
        LocalFree(argv);
        if (!preview.empty()) {
            auto p=imported.empty()?loadInitialPackage(exeFolder()/L"pets"):loadPackage(imported);
            Visual v; v.happy=true; v.bubble=L"慢慢工作，我在这里。"; v.scale=2; savePng(renderPet(p,v),preview);
        } else {
            HANDLE mutex=CreateMutexW(nullptr,FALSE,isolated?nullptr:L"Local\\PocketPet.Desktop.v1");
            if (!isolated && GetLastError()==ERROR_ALREADY_EXISTS) {
                if (HWND old=FindWindowW(ClassName,nullptr)) {
                    std::wstring path=imported.wstring(); COPYDATASTRUCT data{1,(DWORD)((path.size()+1)*sizeof(wchar_t)),(void*)path.c_str()};
                    DWORD_PTR reply; SendMessageTimeoutW(old,WM_COPYDATA,0,(LPARAM)&data,SMTO_ABORTIFHUNG,2000,&reply);
                }
            } else {
                app.loadSettings(); app.body.p={app.settings.x,app.settings.y};
                auto preferred=imported.empty()?app.settings.package:imported;
                if (!preferred.empty() && preferred.is_relative()) preferred=exeFolder()/preferred;
                fs::create_directories(exeFolder()/L"pets");
                app.package=loadInitialPackage(exeFolder()/L"pets",preferred);
                app.settings.package=fs::relative(app.package.manifest.empty()?app.package.sheet:app.package.manifest,exeFolder());
                app.applyRenderOptions();
                WNDCLASSEXW cls{sizeof(cls)}; cls.style=CS_DBLCLKS; cls.lpfnWndProc=procedure; cls.hInstance=instance; cls.hCursor=LoadCursorW(nullptr,IDC_HAND); cls.lpszClassName=ClassName;
                app.icon=makeIcon(); cls.hIcon=app.icon; cls.hIconSm=app.icon; RegisterClassExW(&cls);
                auto area=app.workArea(); if (app.body.p.x < -1e8) app.body.p={area.right-260,area.bottom};
                DWORD styles=WS_EX_LAYERED|WS_EX_NOACTIVATE|(inspectable?WS_EX_APPWINDOW:WS_EX_TOOLWINDOW)|(app.settings.topmost?WS_EX_TOPMOST:0);
                HWND hwnd=CreateWindowExW(styles,ClassName,(L"PocketPet · "+app.package.name).c_str(),WS_POPUP,(int)app.body.p.x-128,(int)app.body.p.y-288,256,300,nullptr,nullptr,instance,&app);
                if (!hwnd) throw std::runtime_error("Cannot create desktop pet window");
                app.dpi=GetDpiForWindow(hwnd)/96.; app.dimensions(); app.body.constrain(app.workArea());
                app.taskbarCreated=RegisterWindowMessageW(L"TaskbarCreated"); app.tray(true); DragAcceptFiles(hwnd,TRUE);
                app.refreshPackages(); app.draw(now()); ShowWindow(hwnd,SW_SHOWNOACTIVATE); SetTimer(hwnd,1,16,nullptr);
                if (app.bubble.empty()) app.say(L"你好呀，拖住我试试。右键有小窝。",6);
                app.log("start"); MSG message;
                while (GetMessageW(&message,nullptr,0,0)>0) { TranslateMessage(&message); DispatchMessageW(&message); }
            }
            if (mutex) CloseHandle(mutex);
        }
    } catch (const std::exception& e) { MessageBoxW(nullptr,widen(e.what()).c_str(),L"PocketPet",MB_OK|MB_ICONERROR); result=1; }
    Gdiplus::GdiplusShutdown(token); if (SUCCEEDED(com)) CoUninitialize(); return result;
}
