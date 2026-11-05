#include "assets.h"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <fstream>
#include <set>
#include <stdexcept>
#include "../third_party/picojson.h"
#include <webp/decode.h>

using Microsoft::WRL::ComPtr;
namespace pet {
std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),(int)s.size(),nullptr,0);
    if (!n) throw std::runtime_error("Invalid UTF-8 text");
    std::wstring out(n,0); MultiByteToWideChar(CP_UTF8,0,s.data(),(int)s.size(),out.data(),n); return out;
}
std::string narrow(const std::wstring& s) {
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8,0,s.data(),(int)s.size(),nullptr,0,nullptr,nullptr);
    std::string out(n,0); WideCharToMultiByte(CP_UTF8,0,s.data(),(int)s.size(),out.data(),n,nullptr,nullptr); return out;
}
std::string readText(const fs::path& path, size_t limit) {
    std::ifstream f(path,std::ios::binary);
    if (!f) throw std::runtime_error("Cannot open file: " + narrow(path.wstring()));
    auto size = fs::file_size(path);
    if (size > limit) throw std::runtime_error("File exceeds size limit");
    std::string out((size_t)size,0); f.read(out.data(),(std::streamsize)size);
    if (!f && size) throw std::runtime_error("Cannot read complete file");
    return out;
}
static void check(HRESULT hr, const char* what) { if (FAILED(hr)) throw std::runtime_error(what); }
static void validDimensions(unsigned w, unsigned h) {
    if (!w || !h || w > 8192 || h > 8192 || uint64_t(w)*h > 24*1024*1024)
        throw std::runtime_error("Image dimensions are too large or invalid");
}
Image loadImage(const fs::path& path) {
    auto bytes = readText(path,20*1024*1024);
    Image out;
    if (bytes.size() >= 12 && bytes.substr(0,4) == "RIFF" && bytes.substr(8,4) == "WEBP") {
        if (!WebPGetInfo((const uint8_t*)bytes.data(),bytes.size(),&out.width,&out.height))
            throw std::runtime_error("Invalid WebP image");
        validDimensions(out.width,out.height);
        out.pixels.resize(size_t(out.width)*out.height);
        if (!WebPDecodeBGRAInto((const uint8_t*)bytes.data(),bytes.size(),(uint8_t*)out.pixels.data(),out.pixels.size()*4,out.width*4))
            throw std::runtime_error("WebP decode failed");
        for (auto& p : out.pixels) {
            unsigned a = p>>24;
            p = (a<<24) | ((((p>>16)&255)*a+127)/255<<16) | ((((p>>8)&255)*a+127)/255<<8) | (((p&255)*a+127)/255);
        }
    } else {
        ComPtr<IWICImagingFactory> factory;
        check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)),"Cannot initialize image decoder");
        ComPtr<IWICStream> stream; check(factory->CreateStream(&stream),"Cannot create image stream");
        check(stream->InitializeFromMemory((BYTE*)bytes.data(),(DWORD)bytes.size()),"Cannot read image stream");
        ComPtr<IWICBitmapDecoder> decoder;
        check(factory->CreateDecoderFromStream(stream.Get(),nullptr,WICDecodeMetadataCacheOnLoad,&decoder),"Unsupported image (use PNG or WebP)");
        ComPtr<IWICBitmapFrameDecode> frame; check(decoder->GetFrame(0,&frame),"Image has no frame");
        UINT w,h; check(frame->GetSize(&w,&h),"Cannot read image size"); validDimensions(w,h);
        out.width = (int)w; out.height = (int)h; out.pixels.resize(size_t(w)*h);
        ComPtr<IWICFormatConverter> converter; check(factory->CreateFormatConverter(&converter),"Cannot create pixel converter");
        check(converter->Initialize(frame.Get(),GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom),"Cannot convert image pixels");
        check(converter->CopyPixels(nullptr,w*4,(UINT)out.pixels.size()*4,(BYTE*)out.pixels.data()),"Cannot decode image pixels");
    }
    return out;
}
static const picojson::value& field(const picojson::value& root, const std::string& key) { return root.get(key); }
static std::string str(const picojson::value& root, const char* key, std::string fallback = {}) {
    const auto& v = field(root,key);
    if (v.is<picojson::null>()) return fallback;
    if (!v.is<std::string>()) throw std::runtime_error(std::string(key)+" must be a string");
    return v.get<std::string>();
}
static int integer(const picojson::value& root, const char* key, int fallback, int min, int max) {
    const auto& v = field(root,key); if (v.is<picojson::null>()) return fallback;
    if (!v.is<double>()) throw std::runtime_error(std::string(key)+" must be an integer");
    double d = v.get<double>();
    if (!std::isfinite(d) || d != std::floor(d) || d < min || d > max) throw std::runtime_error(std::string(key)+" is outside supported range");
    return (int)d;
}
static void defaults(Package& p) {
    static const char* names[] = {"idle","running-right","running-left","waving","jumping","failed","waiting","running","review"};
    static const int counts[] = {6,8,8,4,5,8,6,6,6};
    for (int row = 0; row < std::min(9,p.rows); ++row) {
        Track t; t.loop = row == 0 || row == 1 || row == 2 || row == 6 || row == 7;
        for (int c = 0; c < std::min(counts[row],p.columns); ++c) {
            t.frames.push_back(row*p.columns+c);
            t.durations.push_back(row == 0 ? .7 : .14);
        }
        p.tracks[names[row]] = t;
    }
    if (!p.tracks.count("idle")) p.tracks["idle"] = Track{{0},{1.0},true,"idle"};
}
Package loadPackage(fs::path input) {
    Package p;
    if (fs::is_directory(input)) input /= L"pet.json";
    auto ext = input.extension().wstring(); std::transform(ext.begin(),ext.end(),ext.begin(),::towlower);
    picojson::value root(picojson::object{});
    if (ext == L".json") {
        auto text = readText(input);
        if (text.compare(0,3,"\xEF\xBB\xBF") == 0) text.erase(0,3);
        auto err = picojson::parse(root,text);
        if (!err.empty() || !root.is<picojson::object>()) throw std::runtime_error("Invalid pet.json: " + err);
        p.manifest = fs::weakly_canonical(input);
        p.name = widen(str(root,"displayName",narrow(input.parent_path().filename().wstring())));
        p.description = widen(str(root,"description","Imported companion"));
        auto relative = fs::path(widen(str(root,"spritesheetPath","spritesheet.webp")));
        if (relative.empty() || relative.is_absolute() || relative.has_root_name()) throw std::runtime_error("spritesheetPath must be relative to the pet folder");
        for (auto& part : relative) if (part == L"..") throw std::runtime_error("spritesheetPath cannot leave the pet folder");
        auto base = fs::weakly_canonical(input.parent_path());
        p.sheet = fs::weakly_canonical(base/relative);
        auto bi = base.begin(), si = p.sheet.begin();
        for (; bi != base.end(); ++bi,++si) if (si == p.sheet.end() || *si != *bi) throw std::runtime_error("Spritesheet resolves outside the pet folder");
    } else if (ext == L".png" || ext == L".webp") {
        p.sheet = fs::weakly_canonical(input); p.name = input.stem().wstring(); p.description = L"直接导入的精灵图";
    } else throw std::runtime_error("Select pet.json, a pet folder, or a PNG/WebP spritesheet");
    const auto& cleanup=field(root,"cleanGreenEdges");
    if(!cleanup.is<picojson::null>() && !cleanup.is<bool>()) throw std::runtime_error("cleanGreenEdges must be boolean");
    p.cleanGreenEdges=cleanup.is<bool>() && cleanup.get<bool>();
    p.image = loadImage(p.sheet);
    p.version = integer(root,"spriteVersionNumber",p.image.height == 2288 ? 2 : 1,1,2);
    const auto& frame = field(root,"frame");
    if (!frame.is<picojson::null>()) {
        if (!frame.is<picojson::object>()) throw std::runtime_error("frame must be an object");
        p.frameWidth = integer(frame,"width",192,1,2048); p.frameHeight = integer(frame,"height",208,1,2048);
        p.columns = integer(frame,"columns",8,1,64); p.rows = integer(frame,"rows",p.version == 2 ? 11 : 9,1,64);
    } else p.rows = p.version == 2 ? 11 : 9;
    if (p.columns*p.rows > 512 || p.frameWidth*p.columns != p.image.width || p.frameHeight*p.rows != p.image.height)
        throw std::runtime_error("Spritesheet size does not match grid. Codex v1: 1536x1872; v2: 1536x2288. Custom grids need a frame object.");
    defaults(p);
    const auto& animations = field(root,"animations");
    if (!animations.is<picojson::null>()) {
        if (!animations.is<picojson::object>()) throw std::runtime_error("animations must be an object");
        for (auto& [name,spec] : animations.get<picojson::object>()) {
            if (!spec.is<picojson::object>()) throw std::runtime_error("Animation must be an object");
            const auto& frames = field(spec,"frames");
            if (!frames.is<picojson::array>() || frames.get<picojson::array>().empty() || frames.get<picojson::array>().size()>1024)
                throw std::runtime_error("Animation frames must be a nonempty array (max 1024)");
            double fps = 8;
            const auto& fv = field(spec,"fps");
            if (!fv.is<picojson::null>()) { if (!fv.is<double>()) throw std::runtime_error("fps must be a number"); fps = fv.get<double>(); }
            if (!std::isfinite(fps) || fps <= 0 || fps > 60) throw std::runtime_error("fps must be between 0 and 60");
            Track t; t.fallback = str(spec,"fallback","idle");
            const auto& loop = field(spec,"loop");
            if (!loop.is<picojson::null>() && !loop.is<bool>()) throw std::runtime_error("loop must be boolean");
            t.loop = loop.is<bool>() ? loop.get<bool>() : name == "idle";
            for (auto& f : frames.get<picojson::array>()) {
                if (!f.is<double>() || f.get<double>() < 0 || f.get<double>() >= p.rows*p.columns || f.get<double>() != std::floor(f.get<double>()))
                    throw std::runtime_error("Animation references a frame outside the grid");
                t.frames.push_back((int)f.get<double>()); t.durations.push_back(1/fps);
            }
            p.tracks[name] = std::move(t);
        }
    }
    for (auto& [name,track] : p.tracks) if (!p.tracks.count(track.fallback)) throw std::runtime_error("Animation fallback is missing: " + track.fallback);
    int x0 = p.frameWidth, y0 = p.frameHeight, x1 = -1, y1 = -1;
    for (int idx : p.tracks.at("idle").frames) {
        int ox = (idx%p.columns)*p.frameWidth, oy = (idx/p.columns)*p.frameHeight;
        for (int y = 0; y < p.frameHeight; ++y) for (int x = 0; x < p.frameWidth; ++x) {
            if ((p.image.pixels[size_t(oy+y)*p.image.width+ox+x]>>24) > 24) {
                x0=std::min(x0,x); x1=std::max(x1,x); y0=std::min(y0,y); y1=std::max(y1,y);
            }
        }
    }
    if (x1 < x0) throw std::runtime_error("Idle animation is fully transparent");
    p.anchorX = (x0+x1+1)*.5; p.anchorY = y1+1; p.visibleHeight = y1-y0+1; p.visibleWidth = x1-x0+1;
    if (p.name.empty()) p.name = L"未命名宠物";
    return p;
}
std::vector<fs::path> discoverPackages(const std::vector<fs::path>& roots) {
    std::vector<fs::path> out; std::set<fs::path> seen;
    for (const auto& root : roots) {
        std::error_code ec;
        for (fs::directory_iterator i(root,fs::directory_options::skip_permission_denied,ec), end; !ec && i != end; i.increment(ec)) {
            auto folderName = i->path().filename().wstring();
            if (folderName.empty() || folderName.front() == L'.') continue;
            auto path = i->path()/L"pet.json";
            if (fs::is_regular_file(path,ec)) {
                auto full = fs::weakly_canonical(path,ec);
                if (!ec && seen.insert(full).second) out.push_back(full);
            }
        }
    }
    std::sort(out.begin(),out.end());
    return out;
}
static bool inside(const fs::path& path, const fs::path& root) {
    auto full=fs::weakly_canonical(path), base=fs::weakly_canonical(root);
    auto a=full.begin(), b=base.begin();
    for (;b!=base.end();++a,++b) if (a==full.end() || _wcsicmp(a->c_str(),b->c_str())!=0) return false;
    return true;
}
fs::path storePackage(const fs::path& source, const fs::path& petsRoot) {
    // Validate before writing; importing never mutates the source or existing pets.
    auto p=loadPackage(source);
    auto original=p.manifest.empty()?p.sheet:p.manifest;
    // A bare atlas still needs a discoverable manifest, even when it was
    // already dropped somewhere beneath pets before the user imports it.
    if (inside(original,petsRoot) && !p.manifest.empty()) return original;
    fs::create_directories(petsRoot);
    auto root=fs::weakly_canonical(petsRoot);
    auto name=(p.manifest.empty()?p.sheet.stem():p.manifest.parent_path().filename()).wstring();
    for (auto& c:name) if (c<32 || std::wstring(L"<>:\"/\\|?*").find(c)!=std::wstring::npos) c=L'_';
    while (!name.empty() && (name.back()==L'.' || name.back()==L' ')) name.pop_back();
    if (name.empty() || name.front()==L'.') name=L"imported-pet";
    // Prefix keeps reserved Windows basenames (CON, AUX, ...) usable too.
    name=L"pet-"+name;
    auto dest=root/name;
    for (int suffix=2;fs::exists(dest);++suffix) dest=root/(name+L"-"+std::to_wstring(suffix));
    auto staging=root/(L".import-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    if (!fs::create_directory(staging)) throw std::runtime_error("Import staging directory already exists");
    auto sheetName=L"spritesheet"+p.sheet.extension().wstring();
    fs::copy_file(p.sheet,staging/sheetName);
    picojson::value doc(picojson::object{});
    if (!p.manifest.empty()) {
        auto text=readText(p.manifest); if (text.compare(0,3,"\xEF\xBB\xBF")==0) text.erase(0,3);
        auto error=picojson::parse(doc,text); if (!error.empty()) throw std::runtime_error(error);
    } else {
        doc.get<picojson::object>()["displayName"]=picojson::value(narrow(p.name));
        doc.get<picojson::object>()["spriteVersionNumber"]=picojson::value((double)p.version);
    }
    doc.get<picojson::object>()["spritesheetPath"]=picojson::value(narrow(sheetName));
    { std::ofstream file(staging/L"pet.json",std::ios::binary); file<<doc.serialize(true); if (!file) throw std::runtime_error("Cannot save imported manifest"); }
    loadPackage(staging); // Keep the currently displayed package if copy/validation fails.
    fs::rename(staging,dest);
    return dest/L"pet.json";
}
Package loadInitialPackage(const fs::path& petsRoot, const fs::path& preferred) {
    if (!preferred.empty()) {
        try { return loadPackage(storePackage(preferred,petsRoot)); }
        catch (const std::exception&) { /* A missing old selection falls back to a local package. */ }
    }
    for (const auto& path:discoverPackages({petsRoot})) {
        try { return loadPackage(path); } catch (const std::exception&) {}
    }
    throw std::runtime_error(narrow(L"未找到可用角色。请把包含 pet.json 和 PNG/WebP 精灵图的角色文件夹放进 EXE 同级的 pets 目录，然后重新启动：\n\n"+petsRoot.wstring()));
}
void savePng(const Image& image, const fs::path& path) {
    ComPtr<IWICImagingFactory> factory; check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)),"WIC factory failed");
    ComPtr<IWICStream> stream; check(factory->CreateStream(&stream),"WIC stream failed");
    check(stream->InitializeFromFilename(path.c_str(),GENERIC_WRITE),"Cannot write PNG");
    ComPtr<IWICBitmapEncoder> encoder; check(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder),"PNG encoder failed");
    check(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache),"PNG encoder init failed");
    ComPtr<IWICBitmapFrameEncode> frame; ComPtr<IPropertyBag2> bag;
    check(encoder->CreateNewFrame(&frame,&bag),"PNG frame failed"); check(frame->Initialize(bag.Get()),"PNG frame init failed");
    check(frame->SetSize(image.width,image.height),"PNG size failed");
    WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA; check(frame->SetPixelFormat(&fmt),"PNG format failed");
    auto pixels = image.pixels;
    for (auto& p : pixels) { unsigned a = p>>24; if (a) p = (a<<24) | (std::min(255u,((p>>16)&255)*255/a)<<16) | (std::min(255u,((p>>8)&255)*255/a)<<8) | std::min(255u,(p&255)*255/a); }
    check(frame->WritePixels(image.height,image.width*4,(UINT)pixels.size()*4,(BYTE*)pixels.data()),"PNG pixels failed");
    check(frame->Commit(),"PNG frame commit failed"); check(encoder->Commit(),"PNG commit failed");
}
}
