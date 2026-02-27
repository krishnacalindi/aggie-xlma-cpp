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
extern duckdb::Connection con;

struct State
{
    struct Rect
    {
        int x, y, w, h;
    };
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
        };
        struct Shader
        {
            GLuint vhf, line, selection, stations;
        };
        struct Vbo
        {
            GLuint stations, hist, clound, vhf, map;
        };
        struct Vao
        {
            GLuint stations, map;
        };
        struct Count
        {
            int stations, clound;
            unsigned long vhf;
        };

        // nested
        Shader shader;
        Vbo vbo;
        Vao vao;
        Count count;
        ColorMap colormap;
        Map map;
        Rect rect;

        // normal
        bool initialized = false;
        std::string filepath;
    };
    struct Plot
    {
        // graphics
        GLuint texture, fbo, vao;

        // dimensions
        Rect rect;
        float zoom = 1.0f, uv_x = 0.0f, uv_y = 0.0f;

        // axis
        float x_min, x_max, y_min, y_max;
        std::array<std::string, 5> x_major_ticks = {"", "", "", "", ""};
        std::array<std::string, 5> y_major_ticks = {"", "", "", "", ""};
    };
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
            int vhf = 1, clound = 1;
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
    Timer timer;
    Anime anime;
    Filter filter;
    Graphics graphics;
    Style style;

    Plot time_alt, lon_alt, alt_hist, lon_lat, alt_lat;
    Plot *plots[5] = {&time_alt, &lon_alt, &alt_hist, &lon_lat, &alt_lat};

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
    void ReadStations(const std::string &filepath);                                    // reads station info into its vbo
    void SetLineShader();                                                              // helper for updating line shader (used for maps and histogram)
    void SetStationShader();                                                           // shader for stations
    void Filter();                                                                     // filters db using sql query
    std::string ColorBy();                                                             // helper for color by query
    void Color();                                                                    // color only query
};

#endif