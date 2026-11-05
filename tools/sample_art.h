#pragma once
#include "../src/render.h"
// Developer-only sample asset exporter. Never linked into PocketPet.exe.
namespace pet {
void exportDemoAtlas(const std::filesystem::path& folder, int kind, int version, bool webp);
}
