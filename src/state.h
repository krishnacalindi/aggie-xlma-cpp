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
            int index;
            GLuint texture;
            std::array<const char *, 5> options = {"Viridis", "Plasma", "Inferno", "Magma", "Cividis"};
        };
        GLuint shader_program;
        bool initialized = false;
        ColorMap colormap;
        size_t sources = 0;
    };
    struct Plot
    {
        GLuint texture, fbo, vao, vbo;
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

    std::string status = "Let's do this! :)";
    Filter filter;
    Graphics graphics;
    Plot time_alt, lon_alt, alt_hist, lon_lat, alt_lat;

    // functions
    void Clear();                                                        // TODO: not fully implemented yet
    void Draw(duckdb::unique_ptr<duckdb::MaterializedQueryResult> &res); // plots data using the output of the filter_query
    void InitializeGraphics();                                           // initailzies the opengl shaders, colormaps, textures, etc.
};

#endif