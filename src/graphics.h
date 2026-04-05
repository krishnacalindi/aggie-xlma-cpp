#ifndef GRAPHICS_H
#define GRAPHICS_H

#include <GL/glew.h>
#include <array>
#include <string>
#include <duckdb.hpp>
#include <shader.h>

using QueryResult = duckdb::unique_ptr<duckdb::MaterializedQueryResult>;

struct Graphics
{
    struct Rect
    {
        unsigned int x, y, w, h;
    };
    struct Data
    {
        GLuint vbo, vao;
    };
    struct Colormap
    {
        int index, by_index;
        Data data;
        GLuint texture;
        std::array<const char *, 5> options = {"Viridis", "Plasma", "Inferno", "Magma", "Cividis"};
        std::array<const char *, 8> by_options = {"Time", "Points", "Event density", "log Event density", "Altitude", "Longitude", "Latitude", "log RF of sources"};
    };
    struct Map
    {
        int index = 1;
        Data data;
        std::array<const char *, 3> options = {"None", "State", "County"};
        std::array<unsigned int, 3> sizes{0, 266478, 1277960}; // comes from sizes of each shapefile
    };
    struct Count
    {
        unsigned int stations, entln, entln_cg;
        unsigned long vhf;
    };
    struct Entln
    {
        bool ic = true, cg = true;
    };
    struct Plot
    {
        GLuint texture, fbo;
        Data vhf, entln;
        Rect rect;
        float zoom = 1.0f, uv_x = 0.0f, uv_y = 0.0f;
        float x_min, x_max, y_min, y_max;
        std::array<std::string, 5> x_major_ticks = {"", "", "", "", ""};
        std::array<std::string, 5> y_major_ticks = {"", "", "", "", ""};
    };

    Shader shader;                                                         // shaders
    Count count;                                                           // count for various things
    Colormap colormap;                                                     // colormap info
    Map map;                                                               // map info
    Data stations;                                                         // stations info
    Rect rect;                                                             // entire graphics rect used for screenshots and gifs
    Entln entln;                                                           // flag for plotting entln data
    Plot time_alt, lon_alt, alt_hist, lon_lat, alt_lat;                    // five plots xlma style
    Plot *plots[5] = {&time_alt, &lon_alt, &alt_hist, &lon_lat, &alt_lat}; // helper for all the plots

    // functions
    void Initialize(); // initailizes vbos, vaos, fbos, textures and shaders

    void ProcessResult(QueryResult &data_res, QueryResult &hist_res); // processes duckdb results and modify vbos
    void ProcessEntlnResult(QueryResult &entln_res);                  // processes duckdb entln result
    void ProcessColor(QueryResult &color_res);                        // processes duckdb result and edits vhf vbo with new color data

    void Render(Plot *one = nullptr); // main rendering function !!

    void UpdateTickLabels();                        // updates tick label based on min, max, zoom
    void ReadStations(const std::string &filepath); // reads lma station info from a filepath and populates station vbo
    void ClearPlot();                               // clears plots

private:
    void _setup_vao(GLuint &vao, GLuint vbo, int stride = 2, std::initializer_list<int> offsets = {0, 1}); // helper to setup vao
};

#endif