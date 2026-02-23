#ifndef STATE_H
#define STATE_H

#include <string>
#include <vector>
#include <duckdb.hpp>
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <fstream>
#include <sstream>

struct State
{
    struct Graphics
    {
        struct ColorMap
        {
            int index, by_index;
            GLuint texture;
            std::array<const char *, 5> options = {"Viridis", "Plasma", "Inferno", "Magma", "Cividis"};
            std::array<const char *, 8> by_options = {"Time", "Points", "Event density", "log Event density", "Altitude", "Longitude", "Latitude", "log RF of sources"};
        };
        struct Map
        {
            int index = 1;
            std::array<const char *, 3> options = {"None", "State", "County"};
            std::array<int, 3> sizes{0, 266478, 1277960}; // comes from sizes of each shapefile
            GLuint vao, vbo;
        };
        GLuint vhf_shader, line_shader, vbo, hist_vbo, hist_vao;
        bool initialized = false;
        ColorMap colormap;
        Map map;
        size_t sources = 0;
    };
    struct Plot
    {
        GLuint texture, fbo, vao;
        float x_min, x_max, y_min, y_max;
        int width, height;
        std::array<std::string, 5> x_major_ticks = {"", "", "", "", ""};
        // std::array<std::string, 5> x_minor_ticks = {"", "", "", "", ""};
        std::array<std::string, 5> y_major_ticks = {"", "", "", "", ""};
        // std::array<std::string, 5> y_minor_ticks = {"", "", "", "", ""};
    };
    struct Filter
    {
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
        size_t sources;                // how many sources to display this frame
        clock_t start;                 // when animation started
        bool animating = false;

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

    std::string status = "Let's do this! :)";
    int animation_timer = 5;
    Timer timer;
    Anime anime;
    Filter filter;
    Graphics graphics;
    Plot time_alt, lon_alt, alt_hist, lon_lat, alt_lat;

    // functions
    void Clear();                                                                 // TODO: not fully implemented yet
    void ProcessResult(duckdb::unique_ptr<duckdb::MaterializedQueryResult> &res); // populates vbo and sets up the axes
    void ProcessColor(duckdb::unique_ptr<duckdb::MaterializedQueryResult> &res);  // edits vbo with new color bys
    void Render();                                                                // renders the points in OpenGL
    void Frame();                                                                 // render a frame of animation
    void Histogram(duckdb::unique_ptr<duckdb::MaterializedQueryResult> &res);     // histogram needs seperate query so it gets seperate function
    void InitializeGraphics();                                                    // initailzies the opengl shaders, colormaps, textures, etc.
};

#endif