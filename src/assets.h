#pragma once
#include "core.h"
#include <filesystem>
#include <memory>
#include <cstdint>

namespace pet {
namespace fs = std::filesystem;
struct Image {
    int width = 0, height = 0;
    std::vector<uint32_t> pixels; // Premultiplied BGRA, top-down.
};
struct Package {
    std::wstring name, description;
    fs::path manifest, sheet;
    int version = 1, columns = 8, rows = 9, frameWidth = 192, frameHeight = 208;
    Image image;
    bool cleanGreenEdges=false;
    std::map<std::string,Track> tracks;
    // Union over idle frames: a stable shared ground anchor, never per-frame recentering.
    double anchorX = 96, anchorY = 200, visibleHeight = 160, visibleWidth = 128;
};
std::wstring widen(const std::string& s);
std::string narrow(const std::wstring& s);
std::string readText(const fs::path& path, size_t limit = 1024*1024);
Image loadImage(const fs::path& path);
Package loadPackage(fs::path path);
fs::path storePackage(const fs::path& source, const fs::path& petsRoot);
Package loadInitialPackage(const fs::path& petsRoot, const fs::path& preferred = {});
std::vector<fs::path> discoverPackages(const std::vector<fs::path>& roots);
void savePng(const Image& image, const fs::path& path);
}
