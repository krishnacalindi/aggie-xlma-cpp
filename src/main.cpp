#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <portable-file-dialogs.h>
#include <filesystem>
#include <duckdb.hpp>
#include <regex>
#include <state.h>

duckdb::DuckDB db(nullptr); // in memory databse
duckdb::Connection con(db); // connection to database
static State state;         // state of application

void FilterLMA()
{
    std::string filter_query =
        "WITH filtered AS ("
        "  SELECT "
        "    CAST(EPOCH_NS(datetime) AS DOUBLE) AS time, "
        "    lon, "
        "    lat, "
        "    alt "
        "  FROM lma "
        "  WHERE number_stations >= " +
        std::to_string(state.min_stations) +
        "    AND alt >= " + std::to_string(state.min_alt) +
        "    AND alt <= " + std::to_string(state.max_alt) +
        "    AND chi >= " + std::to_string(state.min_chi) +
        "    AND chi <= " + std::to_string(state.max_chi) +
        "    AND pdb >= " + std::to_string(state.min_power) +
        "    AND pdb <= " + std::to_string(state.max_power) +
        ") "
        "SELECT "
        "  CAST(time - MIN(time) OVER () AS FLOAT) AS time, "
        "  lon, lat, alt, "
        "  MIN(time) OVER (), MAX(time) OVER (), "
        "  MIN(lon) OVER (), MAX(lon) OVER (), "
        "  MIN(lat) OVER (), MAX(lat) OVER (), "
        "  MIN(alt) OVER (), MAX(alt) OVER () "
        "FROM filtered";
    auto result = con.Query(filter_query);
    state.Plot(result);
}

void RenderUI()
{
    // menu bar
    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("Open"))
        {
            if (ImGui::MenuItem("LYLOUT"))
            {
                auto selection = pfd::open_file(
                                     "Select LYLOUT file(s)",
                                     "",
                                     {"LYLOUT files", "*.dat *.gz"},
                                     pfd::opt::multiselect)
                                     .result();
                if (!selection.empty())
                {
                    state.status = "loading files";
                    try
                    {
                        con.Query("DROP TABLE IF EXISTS lma");
                        con.Query("CREATE TABLE lma (datetime TIMESTAMP_NS, lat FLOAT, lon FLOAT, alt FLOAT, chi FLOAT, pdb FLOAT, number_stations UTINYINT)");

                        std::regex date_pattern(R"(.*\w+_(\d+)_\d+_\d+\.dat)");
                        std::unordered_map<int64_t, std::vector<std::string>> files_by_day; // grouping files per day to take advantage of DuckDB multi file reading
                        for (const auto &filepath : selection)
                        {
                            std::smatch match;
                            std::string yymmdd;

                            if (std::regex_match(filepath, match, date_pattern))
                            {
                                yymmdd = match[1].str();
                                int year = 2000 + std::stoi(yymmdd.substr(0, 2));
                                int month = std::stoi(yymmdd.substr(2, 2));
                                int day = std::stoi(yymmdd.substr(4, 2));
                                std::tm tm = {};
                                tm.tm_year = year - 1900;
                                tm.tm_mon = month - 1;
                                tm.tm_mday = day;
                                tm.tm_hour = 0;
                                tm.tm_min = 0;
                                tm.tm_sec = 0;
                                std::time_t time_t_value = std::mktime(&tm);
                                int64_t ns_since_epoch = static_cast<int64_t>(time_t_value);
                                files_by_day[ns_since_epoch].push_back(filepath);
                            }
                        }

                        for (const auto &[day_epoch, paths] : files_by_day)
                        {
                            std::string paths_sql = "[";

                            for (size_t i = 0; i < paths.size(); ++i)
                            {
                                paths_sql += "'" + paths[i] + "'";
                                if (i + 1 < paths.size())
                                    paths_sql += ",";
                            }

                            paths_sql += "]";

                            con.Query(
                                "INSERT INTO lma (datetime, lat, lon, alt, chi, pdb, number_stations) "
                                "SELECT "
                                "TRY(MAKE_TIMESTAMP_NS(CAST((CAST(arr[1] AS DOUBLE) + " +
                                std::to_string(day_epoch) + ") * 1E9 AS BIGINT))), "
                                                            "TRY_CAST(arr[2] AS DOUBLE), "
                                                            "TRY_CAST(arr[3] AS DOUBLE), "
                                                            "TRY(CAST(arr[4] AS DOUBLE) / 1000), "
                                                            "TRY_CAST(arr[5] AS FLOAT), "
                                                            "TRY_CAST(arr[6] AS FLOAT), "
                                                            "CAST(bit_count(TRY_CAST(arr[7] AS INTEGER)) AS UTINYINT) "
                                                            "FROM ("
                                                            "SELECT REGEXP_SPLIT_TO_ARRAY(TRIM(column0), ' +') AS arr "
                                                            "FROM read_csv(" +
                                paths_sql + ", auto_detect=false, delim='|', quote='\"', escape='\"', "
                                            "new_line='\\n', comment='', columns={'column0':'VARCHAR'}, header=false, skip=53)"
                                            ") t;");
                        }
                        state.status = "Loaded " + std::to_string(selection.size()) + " files";
                        FilterLMA();
                    }
                    catch (const std::exception &e)
                    {
                        state.status = "Exception " + std::string(e.what()) + " happened when trying to load files.";
                    }
                }
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Open LYLOUT files ending with .dat or .dat.gz.");

            if (ImGui::MenuItem("ENTLN/NLDN"))
            {
                auto selection = pfd::open_file(
                                     "Select ENTLN/NLDN file(s)",
                                     "",
                                     {"ENTLN/NLDN files", "*.csv *.txt"},
                                     pfd::opt::multiselect)
                                     .result();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Open ENTLN/NLDN lightning data for Cloud-to-Ground lightning data.");

            if (ImGui::MenuItem("State"))
            {
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Load a saved application state.");

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Save"))
        {
            if (ImGui::MenuItem("Animation"))
            {
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Export animation as a .GIF video.");

            if (ImGui::MenuItem("Image"))
            {
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Save current view as a .PNG image.");

            if (ImGui::MenuItem("DAT"))
            {
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Export data as .dat file compatible with lmatools.");

            if (ImGui::MenuItem("Parquet"))
            {
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Export data as parquet file with Apache arrow.");

            if (ImGui::MenuItem("State"))
            {
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Save current application state.");

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View"))
        {
            if (ImGui::MenuItem("Animate"))
            {
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Start animation playback.");

            if (ImGui::MenuItem("Reset"))
            {
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Reset view to default.");

            if (ImGui::MenuItem("Clear"))
            {
                con.Query("DROP TABLE IF EXISTS lma");
                con.Query("DROP TABLE IF EXISTS ctg");
                state.Clear();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Clear all current data and plots.");

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Flash"))
        {
            if (ImGui::MenuItem("XLMA"))
            {
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("XLMA dot-to-dot flash propagation algorithm.");

            if (ImGui::MenuItem("McCaul"))
            {
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("McCaul flash propagation algorithm.");

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Help"))
        {
            if (ImGui::MenuItem("Contact"))
            {
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Contact current maintainer of Aggie XLMA.");

            if (ImGui::MenuItem("About"))
            {
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("About Aggie XLMA.");

            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    // main viewport
    ImGuiViewport *viewport = ImGui::GetMainViewport();

    // status bar
    float menu_bar_height = ImGui::GetFrameHeight();
    ImGui::SetNextWindowPos(
        ImVec2(viewport->Pos.x,
               viewport->Pos.y + viewport->Size.y - menu_bar_height));

    ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, menu_bar_height));

    ImGuiWindowFlags stats_bar_flags =
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_MenuBar;

    if (ImGui::Begin("##StatusBar", nullptr, stats_bar_flags))
    {
        if (ImGui::BeginMenuBar())
        {
            ImGui::Text("%s", state.status.c_str());
            ImGui::EndMenuBar();
        }
        ImGui::End();
    }

    // main window
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x,
                                    viewport->WorkSize.y - menu_bar_height));

    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoTitleBar |
                                    ImGuiWindowFlags_NoCollapse |
                                    ImGuiWindowFlags_NoResize |
                                    ImGuiWindowFlags_NoMove |
                                    ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::Begin("Aggie XLMA", nullptr, window_flags);

    float left_width = ImGui::GetContentRegionAvail().x * 0.3f;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::BeginChild("##Tools", ImVec2(left_width, 0), ImGuiChildFlags_Borders);
    ImGui::Text("Filters");
    if (ImGui::InputFloat("Min. Stations", &state.min_stations))
    {
        FilterLMA();
    }
    if (ImGui::InputFloat("Min. Altitude", &state.min_alt))
    {
        FilterLMA();
    }

    if (ImGui::InputFloat("Max. Altitude", &state.max_alt))
    {
        FilterLMA();
    }

    if (ImGui::InputFloat("Min. Chi", &state.min_chi))
    {
        FilterLMA();
    }

    if (ImGui::InputFloat("Max. Chi", &state.max_chi))
    {
        FilterLMA();
    }

    if (ImGui::InputFloat("Min. Power", &state.min_power))
    {
        FilterLMA();
    }

    if (ImGui::InputFloat("Max. Power", &state.max_power))
    {
        FilterLMA();
    }
    ImGui::Text("Maps");
    ImGui::Text("Colors");

    ImGui::Combo("Colormaps", &state.view.colormap_idx, state.view.colormap_options.data(), state.view.colormap_options.size()); // not dynamic yet
    ImGui::Text("Animation");
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("##Plots", ImVec2(0, 0), ImGuiChildFlags_Borders);
    float fixed_plot_height = ImGui::GetContentRegionAvail().y * 0.15f;
    float fixed_plot_width = ImGui::GetContentRegionAvail().x * 0.8f;
    float axis_height = ImGui::GetFontSize() * 1.2f;
    ImGui::BeginChild("##TimeAltitude", ImVec2(-1, fixed_plot_height), ImGuiChildFlags_Borders);
    {
        ImVec2 window_size = ImGui::GetWindowSize();
        float width = window_size.x;
        float height = window_size.y;
        state.view.time_alt_width = width - axis_height;
        state.view.time_alt_height = height - axis_height;
        ImGui::BeginChild("##AltAxis1", ImVec2(axis_height, height - axis_height), false);
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::Image((ImTextureID)state.view.time_alt_texture, ImVec2(width - axis_height, height - axis_height), ImVec2(0, 0), ImVec2(1, 1));
        ImGui::BeginChild("##Node1", ImVec2(axis_height, axis_height), false);
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("##TimeAxis1", ImVec2(width - axis_height, axis_height), false);
        ImGui::EndChild();
    }
    ImGui::EndChild();
    ImGui::BeginChild("##LongitudeAltitude", ImVec2(fixed_plot_width, fixed_plot_height), ImGuiChildFlags_Borders);
    {
        ImVec2 window_size = ImGui::GetWindowSize();
        float width = window_size.x;
        float height = window_size.y;
        state.view.lon_alt_width = width - axis_height;
        state.view.lon_alt_height = height - axis_height;
        ImGui::BeginChild("##AltAxis2", ImVec2(axis_height, height - axis_height), false);
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::Image((ImTextureID)state.view.lon_alt_texture, ImVec2(width - axis_height, height - axis_height), ImVec2(0, 0), ImVec2(1, 1));
        ImGui::BeginChild("##Node2", ImVec2(axis_height, axis_height), false);
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("##LonAxis2", ImVec2(width - axis_height, axis_height), false);
        ImGui::EndChild();
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##AltitudeHistogram", ImVec2(-1, fixed_plot_height), ImGuiChildFlags_Borders);
    {
        ImVec2 window_size = ImGui::GetWindowSize();
        float width = window_size.x;
        float height = window_size.y;
        state.view.alt_hist_width = width - axis_height;
        state.view.alt_hist_height = height - axis_height;
        ImGui::BeginChild("##AltAxis3", ImVec2(axis_height, height - axis_height), false);
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::Image((ImTextureID)state.view.alt_hist_texture, ImVec2(width - axis_height, height - axis_height), ImVec2(0, 0), ImVec2(1, 1));
        ImGui::BeginChild("##Node3", ImVec2(axis_height, axis_height), false);
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("##CountAxis3", ImVec2(width - axis_height, axis_height), false);
        ImGui::EndChild();
    }
    ImGui::EndChild();
    ImGui::BeginChild("##LongitudeLatitude", ImVec2(fixed_plot_width, -1), ImGuiChildFlags_Borders);
    {
        ImVec2 window_size = ImGui::GetWindowSize();
        float width = window_size.x;
        float height = window_size.y;
        state.view.lon_lat_width = width - axis_height;
        state.view.lon_lat_height = height - axis_height;
        ImGui::BeginChild("##LatAxis4", ImVec2(axis_height, height - axis_height), false);
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::Image((ImTextureID)state.view.lon_lat_texture, ImVec2(width - axis_height, height - axis_height), ImVec2(0, 0), ImVec2(1, 1));
        ImGui::BeginChild("##Node4", ImVec2(axis_height, axis_height), false);
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("##LonAxis4", ImVec2(width - axis_height, axis_height), false);
        ImGui::EndChild();
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##AltitudeLatitude", ImVec2(-1, -1), ImGuiChildFlags_Borders);
    {
        ImVec2 window_size = ImGui::GetWindowSize();
        float width = window_size.x;
        float height = window_size.y;
        state.view.alt_lat_width = width - axis_height;
        state.view.alt_lat_height = height - axis_height;
        ImGui::BeginChild("##LatAxis5", ImVec2(axis_height, height - axis_height), false);
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::Image((ImTextureID)state.view.alt_lat_texture, ImVec2(width - axis_height, height - axis_height), ImVec2(0, 0), ImVec2(1, 1));
        ImGui::BeginChild("##Node5", ImVec2(axis_height, axis_height), false);
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("##AltAxis5", ImVec2(width - axis_height, axis_height), false);
        ImGui::EndChild();
    }
    ImGui::EndChild();
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::End();
}

#ifdef _WIN32
#include <windows.h>
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
#else
int main()
{
#endif
    if (!glfwInit())
    {
        std::cerr << "Failed to initialize GLFW\n";
        return -1;
    }

    const char *glsl_version = "#version 330";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);

    GLFWmonitor *primary = glfwGetPrimaryMonitor();
    const GLFWvidmode *mode = glfwGetVideoMode(primary);
    GLFWwindow *window = glfwCreateWindow(mode->width, mode->height, "Aggie XLMA", nullptr, nullptr);

    if (!window)
    {
        std::cerr << "Failed to create GLFW window\n";
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK)
    {
        std::cerr << "Failed to initialize GLEW\n";
        return -1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.FontGlobalScale = 1.8f;
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        RenderUI();

        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}