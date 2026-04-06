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
            float x = p->x_min + (p->uv_x + (mouse.x - p->rect.x) / p->rect.w * p->zoom) * (p->x_max - p->x_min);
            float y = p->y_min + (p->uv_y + (1.0f - (mouse.y - p->rect.y) / p->rect.h) * p->zoom) * (p->y_max - p->y_min);
            xs.emplace_back(x);
            ys.emplace_back(y);
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
    clicks.clear();
    xs.clear();
    ys.clear();
    plot = nullptr;
}