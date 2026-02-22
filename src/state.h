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
            std::array<const char *, 7> by_options = {"Time", "Event density", "log Event density", "Altitude", "Longitude", "Latitude", "log RF of sources"};
        };
        GLuint shader_program, vao, vbo;
        bool initialized = false;
        ColorMap colormap;
        size_t sources = 0;
    };
    struct Plot
    {
        GLuint texture, fbo;
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

    std::string status = "Let's do this! :)";
    Timer timer;
    Filter filter;
    Graphics graphics;
    Plot time_alt, lon_alt, alt_hist, lon_lat, alt_lat;

    // functions
    void Clear();                                                                 // TODO: not fully implemented yet
    void ProcessResult(duckdb::unique_ptr<duckdb::MaterializedQueryResult> &res); // populates vbo and sets up the axes
    void ProcessColor(duckdb::unique_ptr<duckdb::MaterializedQueryResult> &res);  // edits vbo with new color bys
    void Render();                                                                // renders the points in OpenGL
    void InitializeGraphics();                                                    // initailzies the opengl shaders, colormaps, textures, etc.
};

#endif