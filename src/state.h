#ifndef STATE_H
#define STATE_H

#include <string>
#include <vector>
#include <duckdb.hpp>
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <fstream>
#include <sstream>

#include <graphics.h>

extern GLFWwindow *window;
extern duckdb::Connection con;

struct State
{
    struct Filter
    {
        // filters
        float min_stations = 6.0;
        float min_alt = 0.0;
        float max_alt = 20.0;
        float min_chi = 0.0;
        float max_chi = 5.0;
        float min_power = -60.0;
        float max_power = 60.0;
    };
    struct Timer
    {
        // start
        clock_t start_time;

        void Start()
        {
            start_time = clock();
        }

        int End()
        {
            // returns ms since start_time
            return (clock() - start_time) * 1000.0 / CLOCKS_PER_SEC;
        }
    };
    struct Anime
    {
        int duration = 5, duration_ms; // duration_ms for convenience
        int by_index = 0;
        std::array<const char *, 2> options = {"Point", "Time"};
        size_t sources; // how many sources to display this frame
        clock_t start;  // when animation started
        bool animating = false, saving = false;
        void *gif = nullptr;
        std::string gif_path;

        void Start()
        {
            start = clock();
            animating = true;
            duration_ms = duration * 1000;
        }

        int Elapsed()
        {
            // returns ms since start of animation
            return (clock() - start) * 1000.0 / CLOCKS_PER_SEC;
        }

        void End()
        {
            animating = false;
        }
    };
    struct Style
    {
        enum class Mode
        {
            Light,
            Dark
        };
        struct Size
        {
            int vhf = 1;
        };
        struct Color
        {
            int as_int = 255; // int color (white if black, black if white)
            float same = 0.0f, diff = 1.0f;
            // float same color (black if black, white if white)
            // float flipped color (white if black, black if white)
        };

        // vars
        Size size;
        Mode mode = Mode::Dark;
        Color color;
    };

    // variables
    std::string status = "Let's do this! :)";
    Graphics graphics;
    Timer timer;
    Anime anime;
    Filter filter;
    Style style;

    // functions

    void Frame();                               // render a frame of animation
    void Flip();                                // flippping the theme (please use dark mode)
    void StartSaveGIF(const std::string &path); // does gif writer initializing for saving
    void SaveGIFFrame();                        // saves current plot frame
    void Filter();                              // filters db using sql query
    void EntlnFilter();                         // entln fitler
    std::string ColorBy();                      // helper for color by query
    void Color();                               // color only query
};

#endif