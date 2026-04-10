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
#include <external/miniaudio.h>

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
    struct Music
    {
        ma_sound background;
        ma_engine engine;
        bool play = false, waiting = false;
        int current = 0;
        clock_t end_time = 0;
        const std::array<const char *, 6> files = {"music/piano 1.wav", "music/piano 2.mp3", "music/piano 3.mp3",
                                                   "music/piano 4.wav", "music/piano 5.wav", "music/piano 6.mp3"};

        void Tick()
        {
            // song ended
            if (!waiting)
            {
                end_time = clock();
                waiting = true;
            }
            // cooldown for one minute
            else if ((clock() - end_time) * 1000 / CLOCKS_PER_SEC >= 60000)
            {
                waiting = false;
                int next;
                do
                {
                    next = rand() % files.size(); // ensuring no repeat
                } while (next == current);
                current = next;
                ma_sound_uninit(&background);
                ma_sound_init_from_file(&engine, files[current], 0, NULL, NULL, &background);
                ma_sound_start(&background); // new song!
            }
        }
    };

    // variables
    DB db;
    Status status;
    Graphics graphics;
    Anime anime;
    Filter filter;
    Style style;
    PolySelect polyselect;
    Music music;

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