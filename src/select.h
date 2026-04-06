#ifndef SELECT_H
#define SELECT_H

#include <imgui.h>
#include <vector>
#include <graphics.h>

struct PolySelect
{
    bool selecting = false, keep = true;
    Graphics::Plot *plot = nullptr;
    std::vector<ImVec2> clicks;
    std::vector<float> xs, ys;

    void Add(ImVec2 mouse, Graphics::Plot *p, bool last = false); // adds a new point
    void Reset();                                                 // resets filter data
};

#endif