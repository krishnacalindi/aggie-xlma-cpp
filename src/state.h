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
#include <select.h>
#include <style.h>

extern GLFWwindow *window;

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
    struct Status
    {
        std::string global;
        std::string plot, idle;
    };
    struct DB
    {
        duckdb::DuckDB db;
        duckdb::Connection con;

        DB() : db(nullptr), con(db) {}
    };

    // variables
    DB db;
    Status status;
    Graphics graphics;
    Anime anime;
    Filter filter;
    Style style;
    PolySelect polyselect;

    // functions

    void Frame();                               // render a frame of animation
    void Flip();                                // flippping the theme (please use dark mode)
    void StartSaveGIF(const std::string &path); // does gif writer initializing for saving
    void SaveGIFFrame();                        // saves current plot frame
    void Filter();                              // filters db using sql query
    void SpatialFilter();                       // filters plotted with duckdb spatial
    void EntlnFilter();                         // entln fitler
    std::string ColorBy();                      // helper for color by query
    void Color();                               // color only query
};

#endif