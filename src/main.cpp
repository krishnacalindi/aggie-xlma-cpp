#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <implot.h>
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <portable-file-dialogs.h>
#include <filesystem>
#include <duckdb.hpp>
#include <regex>

duckdb::DuckDB db(nullptr);                       // in memory databse
duckdb::Connection con(db);                       // connection to database
static std::string status = "Ready";              // status bar message
std::vector<double> lma_lons, lma_lats, lma_alts; // arrays used for plotting
size_t plot_count = 0;
static bool reset_plot_limits = false;
static double min_stations = 6.0;
static double min_alt = 0.0;
static double max_alt = 20.0;
static double min_chi = 0.0;
static double max_chi = 5.0;
static double min_power = -60.0;
static double max_power = 60.0;

void FilterLMA()
{
    std::string sql =
        "SELECT datetime, lon, lat, alt "
        "FROM lightning "
        "WHERE number_stations >= " +
        std::to_string(min_stations) +
        " AND alt >= " + std::to_string(min_alt) +
        " AND alt <= " + std::to_string(max_alt) +
        " AND chi >= " + std::to_string(min_chi) +
        " AND chi <= " + std::to_string(max_chi) +
        " AND pdb >= " + std::to_string(min_power) +
        " AND pdb <= " + std::to_string(max_power);
    auto result = con.Query(sql);
    plot_count = result->RowCount();
    status = "Plotted " + std::to_string(plot_count) + " sources";

    lma_lons.clear();
    lma_lats.clear();
    lma_alts.clear();
    lma_lons.reserve(plot_count);
    lma_lats.reserve(plot_count);
    lma_alts.reserve(plot_count);

    for (size_t i = 0; i < plot_count; i++)
    {
        lma_lons.push_back(result->GetValue(1, i).GetValue<double>());
        lma_lats.push_back(result->GetValue(2, i).GetValue<double>());
        lma_alts.push_back(result->GetValue(3, i).GetValue<double>());
    }

    reset_plot_limits = true;
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
                    status = "loading files";
                    try
                    {
                        con.Query("DROP TABLE IF EXISTS lightning");
                        con.Query("CREATE TABLE lightning (datetime TIMESTAMP, lat DOUBLE, lon DOUBLE, alt DOUBLE, chi FLOAT, pdb FLOAT, number_stations UTINYINT)");

                        std::regex date_pattern(R"(.*\w+_(\d+)_\d+_\d+\.dat)");
                        std::unordered_map<std::string, std::vector<std::string>> files_by_day; // grouping files per day to take advantage of DuckDB multi file reading
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
                                std::string key = std::to_string(year) + "-" + (month < 10 ? "0" : "") + std::to_string(month) + "-" + (day < 10 ? "0" : "") + std::to_string(day);
                                files_by_day[key].push_back(filepath);
                            }
                        }

                        for (const auto &[date_str, paths] : files_by_day)
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
                                "INSERT INTO lightning (datetime, lat, lon, alt, chi, pdb, number_stations) "
                                "SELECT TRY(MAKE_TIMESTAMP(" +
                                date_str.substr(0, 4) + ", " + date_str.substr(5, 2) + ", " + date_str.substr(8, 2) + ", "
                                                                                                                      "CAST(FLOOR(utc_sec / 3600) AS INTEGER), "
                                                                                                                      "CAST(FLOOR(utc_sec / 60 % 60) AS INTEGER), "
                                                                                                                      "CAST(utc_sec % 60 AS INTEGER))), "
                                                                                                                      "TRY_CAST(arr[2] AS DOUBLE), "
                                                                                                                      "TRY_CAST(arr[3] AS DOUBLE), "
                                                                                                                      "TRY(CAST(arr[4] AS DOUBLE) / 1000), "
                                                                                                                      "TRY_CAST(arr[5] AS FLOAT), "
                                                                                                                      "TRY_CAST(arr[6] AS FLOAT), "
                                                                                                                      "CAST(bit_count(TRY_CAST(arr[7] AS INTEGER)) AS UTINYINT) "
                                                                                                                      "FROM ("
                                                                                                                      "SELECT REGEXP_SPLIT_TO_ARRAY(TRIM(column0), ' +') AS arr, "
                                                                                                                      "CAST((REGEXP_SPLIT_TO_ARRAY(TRIM(column0), ' +'))[1] AS DOUBLE) AS utc_sec "
                                                                                                                      "FROM read_csv(" +
                                paths_sql + ", auto_detect=false, delim='|', quote='\"', escape='\"', new_line='\\n', "
                                            "comment='', columns={'column0':'VARCHAR'}, header=false, skip=53)"
                                            ") t;");
                        }
                        status = "Loaded " + std::to_string(selection.size()) + " file(s)";

                        FilterLMA();
                    }
                    catch (const std::exception &e)
                    {
                        status = "Error occured while loading the files. Please check file format.";
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
                lma_lons.clear();
                lma_lats.clear();
                lma_alts.clear();
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
            ImGui::Text("%s", status.c_str());
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
    ImPlot::GetStyle().MarkerSize = 1.0f;
    ImGui::BeginChild("Tools", ImVec2(left_width, 0), true);
    ImGui::Text("Filters");
    if (ImGui::InputDouble("Min. Stations", &min_stations))
    {
        FilterLMA();
    }
    if (ImGui::InputDouble("Min. Altitude", &min_alt))
    {
        FilterLMA();
    }

    if (ImGui::InputDouble("Max. Altitude", &max_alt))
    {
        FilterLMA();
    }

    if (ImGui::InputDouble("Min. Chi", &min_chi))
    {
        FilterLMA();
    }

    if (ImGui::InputDouble("Max. Chi", &max_chi))
    {
        FilterLMA();
    }

    if (ImGui::InputDouble("Min. Power", &min_power))
    {
        FilterLMA();
    }

    if (ImGui::InputDouble("Max. Power", &max_power))
    {
        FilterLMA();
    }
    ImGui::Text("Maps");
    ImGui::Text("Colors");
    ImGui::Text("Animation");
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("Plots", ImVec2(0, 0), true);
    float fixed_plot_height = ImGui::GetContentRegionAvail().y * 0.15f;
    float fixed_plot_width = ImGui::GetContentRegionAvail().x * 0.8f;
    ImPlotFlags plot_flags = ImPlotFlags_NoTitle |
                             ImPlotFlags_NoLegend |
                             ImPlotFlags_NoMouseText |
                             ImPlotFlags_NoMenus;
    ImPlotAxisFlags axis_flags = ImPlotAxisFlags_NoGridLines;
    if (ImPlot::BeginPlot("##TimeAltitude", ImVec2(-1, fixed_plot_height), plot_flags))
    {
        ImPlot::SetupAxis(ImAxis_X1, nullptr, axis_flags);
        ImPlot::SetupAxis(ImAxis_Y1, nullptr, axis_flags);
        ImPlot::EndPlot();
    }
    if (ImPlot::BeginPlot("##LongitudeAltitude", ImVec2(fixed_plot_width, fixed_plot_height), plot_flags))
    {
        ImPlot::SetupAxis(ImAxis_X1, nullptr, axis_flags);
        ImPlot::SetupAxis(ImAxis_Y1, nullptr, axis_flags);
        if (reset_plot_limits)
        {
            double x_min = 0.0, x_max = 0.0, y_min = 0.0, y_max = 0.0;
            if (!lma_lons.empty() && !lma_alts.empty())
            {
                x_min = *std::min_element(lma_lons.begin(), lma_lons.end());
                x_max = *std::max_element(lma_lons.begin(), lma_lons.end());
                y_min = *std::min_element(lma_alts.begin(), lma_alts.end());
                y_max = *std::max_element(lma_alts.begin(), lma_alts.end());
                ImPlot::SetupAxisLimits(ImAxis_X1, x_min, x_max, ImPlotCond_Always);
                ImPlot::SetupAxisLimits(ImAxis_Y1, y_min, y_max, ImPlotCond_Always);
            }
        }
        if (!lma_lons.empty() && !lma_alts.empty())
        {
            ImPlot::PlotScatter("Lightning", lma_lons.data(), lma_alts.data(), plot_count);
        }
        ImPlot::EndPlot();
    }
    ImGui::SameLine();
    if (ImPlot::BeginPlot("##AltitudeHistogram", ImVec2(-1, fixed_plot_height), plot_flags))
    {
        ImPlot::SetupAxis(ImAxis_X1, nullptr, axis_flags);
        ImPlot::SetupAxis(ImAxis_Y1, nullptr, axis_flags);
        ImPlot::EndPlot();
    }
    if (ImPlot::BeginPlot("##LongitudeLatitude", ImVec2(fixed_plot_width, -1), plot_flags))
    {
        if (ImGui::IsMouseClicked(0))
        {
            ImPlotPoint pt = ImPlot::GetPlotMousePos();
            status = "Point clicked- x: " + std::to_string(pt.x) + " y: " + std::to_string(pt.y);
            // data.push_back(pt);
        }
        ImPlot::SetupAxis(ImAxis_X1, nullptr, axis_flags);
        ImPlot::SetupAxis(ImAxis_Y1, nullptr, axis_flags);
        if (reset_plot_limits)
        {
            double x_min = 0.0, x_max = 0.0, y_min = 0.0, y_max = 0.0;
            if (!lma_lats.empty() && !lma_lons.empty())
            {
                x_min = *std::min_element(lma_lons.begin(), lma_lons.end());
                x_max = *std::max_element(lma_lons.begin(), lma_lons.end());
                y_min = *std::min_element(lma_lats.begin(), lma_lats.end());
                y_max = *std::max_element(lma_lats.begin(), lma_lats.end());
                ImPlot::SetupAxisLimits(ImAxis_X1, x_min, x_max, ImPlotCond_Always);
                ImPlot::SetupAxisLimits(ImAxis_Y1, y_min, y_max, ImPlotCond_Always);
            }
        }
        if (!lma_lats.empty() && !lma_lons.empty())
        {
            ImPlot::PlotScatter("Lightning", lma_lons.data(), lma_lats.data(), plot_count);
        }
        ImPlot::EndPlot();
    }
    ImGui::SameLine();
    if (ImPlot::BeginPlot("##AltitudeLatitude", ImVec2(-1, -1), plot_flags))
    {
        ImPlot::SetupAxis(ImAxis_X1, nullptr, axis_flags);
        ImPlot::SetupAxis(ImAxis_Y1, nullptr, axis_flags);
        if (reset_plot_limits)
        {
            double x_min = 0.0, x_max = 0.0, y_min = 0.0, y_max = 0.0;
            if (!lma_lats.empty() && !lma_lons.empty())
            {
                x_min = *std::min_element(lma_alts.begin(), lma_alts.end());
                x_max = *std::max_element(lma_alts.begin(), lma_alts.end());
                y_min = *std::min_element(lma_lats.begin(), lma_lats.end());
                y_max = *std::max_element(lma_lats.begin(), lma_lats.end());
                ImPlot::SetupAxisLimits(ImAxis_X1, x_min, x_max, ImPlotCond_Always);
                ImPlot::SetupAxisLimits(ImAxis_Y1, y_min, y_max, ImPlotCond_Always);
            }
            reset_plot_limits = false;
        }
        if (!lma_alts.empty() && !lma_lats.empty())
        {
            ImPlot::PlotScatter("Lightning", lma_alts.data(), lma_lats.data(), plot_count);
        }
        ImPlot::EndPlot();
    }
    ImGui::EndChild();
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
    ImPlot::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.FontGlobalScale = 1.8f;
    ImGui::StyleColorsLight();

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
    ImPlot::DestroyContext();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}