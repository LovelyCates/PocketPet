#pragma once
#include "assets.h"
namespace pet {
struct Visual {
    double time = 0, actionTime = 0, scale = 1, impact = 0, tilt = 0;
    double lookX = 0, lookY = 0;
    bool dragged = false, airborne = false, sleeping = false, happy = false, walking = false;
    bool reducedMotion = false, loopAction=false;
    int direction = 1;
    std::string action = "idle";
    std::wstring bubble;
};
Image renderPet(const Package& package, const Visual& visual);
void cleanGreenFringe(Image& image);
}
