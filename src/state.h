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

extern GLFWwindow *window;

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
        int plot_x, plot_y, plot_width, plot_height;
    };
    struct Plot
    {
        GLuint texture, fbo, vao;
        float x_min, x_max, y_min, y_max;
        int width, height;
        int x, y;
        float zoom = 1.0f, uv_x = 0.0f, uv_y = 0.0f;
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
    struct Theme
    {
        int vhf_size = 1;
        int dark = 1;              // 1: dark, 0: light
        int color_32 = 255;        // int color (white if black, black if white)
        float same_color_f = 0.0f; // float same color (black if black, white if white)
        float diff_color_f = 1.0f; // float flipped color (white if black, black if white)
    };

    std::string status = "Let's do this! :)";
    Timer timer;
    Anime anime;
    Filter filter;
    Graphics graphics;
    Plot time_alt, lon_alt, alt_hist, lon_lat, alt_lat;
    Plot* plots[5] = {&time_alt, &lon_alt, &alt_hist, &lon_lat, &alt_lat};
    Theme theme;

    // functions
    void ClearPlot();
    void ProcessResult(duckdb::unique_ptr<duckdb::MaterializedQueryResult> &data_res,
                       duckdb::unique_ptr<duckdb::MaterializedQueryResult> &hist_res); // populates vbo and sets up the axes
    void ProcessColor(duckdb::unique_ptr<duckdb::MaterializedQueryResult> &res);       // edits vbo with new color bys
    void Render();                                                                     // renders the points in OpenGL
    void Frame();                                                                      // render a frame of animation
    void Flip();                                                                       // flippping the theme (please use dark mode)
    void StartSaveGIF(const std::string &path);                                        // does gif writer initializing for saving
    void SaveGIFFrame();                                                               // saves current plot frame
    void InitializeGraphics();                                                         // initailzies the components needed for opengl
    void SetVHFShader();                                                               // helper for updating VHF shader
    void SetLineShader();                                                              // helper for updating line shader (used for maps and histogram)
};

#endif