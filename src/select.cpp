#include <select.h>
#include <state.h>

extern State state;

void PolySelect::Add(ImVec2 mouse, Graphics::Plot *p, bool last)
{
    if (plot == nullptr)
        plot = p;
    if (plot == p)
    {
        if (!last)
        {
            selecting = true;
            clicks.emplace_back(mouse);
            xs.emplace_back(p->X(mouse.x));
            ys.emplace_back(p->Y(mouse.y));
        }
        else
        {
            // mouse does not matter
            selecting = false;
            state.graphics.Reset();
            if (clicks.size() >= 3) // atleat 3 points needed to make a polygon
                state.SpatialFilter();
            Reset();
        }
    }
}

void PolySelect::Reset()
{
    selecting = false;
    clicks.clear();
    xs.clear();
    ys.clear();
    plot = nullptr;
}