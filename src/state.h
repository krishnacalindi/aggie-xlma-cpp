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
    struct View
    {
        bool initialized = false;
        size_t num_sources = 0;
        int colormap_idx = 0;
        std::array<const char*, 5> colormap_options = {"Viridis", "Plasma", "Inferno", "Magma", "Cividis"};
        float lon_min, lon_max, alt_min, alt_max, lat_min, lat_max, time_min, time_max;
        int time_alt_width, time_alt_height, lon_alt_width, lon_alt_height, alt_hist_width,
            alt_hist_height, lon_lat_width, lon_lat_height, alt_lat_width, alt_lat_height;
        GLuint time_alt_texture, time_alt_fbo, time_alt_vbo, time_alt_vao;
        GLuint lon_alt_texture, lon_alt_fbo, lon_alt_vbo, lon_alt_vao;
        GLuint alt_hist_texture, alt_hist_fbo, alt_hist_vbo, alt_hist_vao;
        GLuint lon_lat_texture, lon_lat_fbo, lon_lat_vbo, lon_lat_vao;
        GLuint alt_lat_texture, alt_lat_fbo, alt_lat_vbo, alt_lat_vao;
        GLuint colormaps, shader_program;
    };
    View view;
    std::string status = "Ready";
    float min_stations = 6.0;
    float min_alt = 0.0;
    float max_alt = 20.0;
    float min_chi = 0.0;
    float max_chi = 5.0;
    float min_power = -60.0;
    float max_power = 60.0;

    void Clear();                                                        // clears all data
    void Plot(duckdb::unique_ptr<duckdb::MaterializedQueryResult> &res); // adds data to Data
    void Initialize();                                                   // initailzies the opengl shaders, colormaps, textures, etc.
};

#endif