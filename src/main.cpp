#define IMGUI_DEFINE_MATH_OPERATORS
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define GIF_FLIP_VERT

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include "imgui_internal.h"
#include <iostream>
#include <portable-file-dialogs.h>
#include <filesystem>
#include <regex>
#include <state.h>
#include "stb_image_write.h"
#include <ctime>

duckdb::DuckDB db(nullptr); // in memory databse
duckdb::Connection con(db); // connection to database
static State state;         // state of application
GLFWwindow *window = nullptr;
// rotation logic obtained from https://github.com/ocornut/imgui/issues/705
inline void AddVerticalText(ImDrawList *DrawList, const char *text, ImVec2 pos, ImU32 color_32)
{
    pos.x = IM_ROUND(pos.x);
    pos.y = IM_ROUND(pos.y);
    ImFont *font = GImGui->Font;
    const ImFontGlyph *glyph;
    char c;
    float scale = GImGui->FontSize / font->FontSize;
    while ((c = *text++))
    {
        glyph = font->FindGlyph(c);
        if (!glyph)
            continue;
        DrawList->PrimReserve(6, 4);
        DrawList->PrimQuadUV(
            pos + ImVec2(glyph->Y0, -glyph->X0) * scale,
            pos + ImVec2(glyph->Y0, -glyph->X1) * scale,
            pos + ImVec2(glyph->Y1, -glyph->X1) * scale,
            pos + ImVec2(glyph->Y1, -glyph->X0) * scale,
            ImVec2(glyph->U0, glyph->V0),
            ImVec2(glyph->U1, glyph->V0),
            ImVec2(glyph->U1, glyph->V1),
            ImVec2(glyph->U0, glyph->V1),
            color_32);
        pos.y -= glyph->AdvanceX * scale;
    }
}
// helper for color by
std::string ColorBy()
{
    switch (state.graphics.colormap.by_index)
    {
    case 0:
        return "(time - b.min_time) / (b.max_time - b.min_time)";
    case 1:
        return "(t.row_idx - 1)::FLOAT / (t.total_rows - 1)::FLOAT";
    case 2:
        return "bin_count::FLOAT / d.max_c";
    case 3:
        return "LN(bin_count)::FLOAT / d.max_lc::FLOAT";
    case 4:
        return "(alt - b.min_alt) / (b.max_alt - b.min_alt)";
    case 5:
        return "(lon - b.min_lon) / (b.max_lon - b.min_lon)";
    case 6:
        return "(lat - b.min_lat) / (b.max_lat - b.min_lat)";
    case 7:
        return "(pdb - b.min_pdb) / (b.max_pdb - b.min_pdb)";
    default:
        return "(time - b.min_time) / (b.max_time - b.min_time)";
    }
}
// filter helper
void Filter()
{
    // updating plot column
    std::string filter_query = "UPDATE lma SET plot = (number_stations >= " + std::to_string(state.filter.min_stations) +
                               " AND alt >= " + std::to_string(state.filter.min_alt) +
                               " AND alt <= " + std::to_string(state.filter.max_alt) +
                               " AND chi >= " + std::to_string(state.filter.min_chi) +
                               " AND chi <= " + std::to_string(state.filter.max_chi) +
                               " AND pdb >= " + std::to_string(state.filter.min_power) +
                               " AND pdb <= " + std::to_string(state.filter.max_power) + ");";
    con.Query(filter_query);

    // getting data
    std::string data_query =
        "WITH bins AS ("
        "SELECT FLOOR(lon / 0.01) AS bin_lon, FLOOR(lat / 0.01) AS bin_lat, COUNT(*) AS bin_count "
        "FROM lma WHERE plot = true GROUP BY bin_lon, bin_lat"
        "), "
        "density AS ("
        "SELECT MAX(bin_count) AS max_c, MAX(LN(bin_count)) AS max_lc FROM bins"
        "), "
        "times AS ("
        "SELECT *, CAST(EPOCH_NS(datetime) - EPOCH_NS(DATE_TRUNC('day', MIN(datetime) OVER ())) AS FLOAT) AS time, "
        "ROW_NUMBER() OVER (ORDER BY datetime) AS row_idx, "
        "COUNT(*) OVER () AS total_rows "
        "FROM lma WHERE plot = true"
        "), "
        "bounds AS ("
        "SELECT MIN(time) AS min_time, MAX(time) AS max_time, "
        "MIN(lon) AS min_lon, MAX(lon) AS max_lon, "
        "MIN(lat) AS min_lat, MAX(lat) AS max_lat, "
        "MIN(alt) AS min_alt, MAX(alt) AS max_alt, "
        "MIN(pdb) AS min_pdb, MAX(pdb) AS max_pdb FROM times"
        ") "
        "SELECT t.time, t.lon, t.lat, t.alt, " +
        ColorBy() + " AS color, "
                    "b.min_time, b.max_time, b.min_lon, b.max_lon, b.min_lat, b.max_lat, b.min_alt, b.max_alt, b.min_pdb, b.max_pdb "
                    "FROM times t "
                    "JOIN bins k ON FLOOR(t.lon / 0.01) = k.bin_lon AND FLOOR(t.lat / 0.01) = k.bin_lat "
                    "CROSS JOIN density d "
                    "CROSS JOIN bounds b "
                    "ORDER BY t.time";
    auto data_result = con.Query(data_query);

    // histogram (done seperately as size of output differs)
    std::string hist_query = "SELECT "
                             "FLOOR(alt / 0.1) * 0.1 AS bin, "
                             "COUNT(*)::FLOAT AS count, "
                             "MIN(COUNT(*)) OVER() AS min_count, "
                             "MAX(COUNT(*)) OVER() AS max_count "
                             "FROM lma "
                             "WHERE plot = true AND alt >= 0 AND alt <= 20 GROUP BY bin "
                             "ORDER BY bin";
    auto hist_result = con.Query(hist_query);
    state.ProcessResult(data_result, hist_result);
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
                    state.timer.Start();
                    try
                    {
                        con.Query("DROP TABLE IF EXISTS lma");
                        con.Query("CREATE TABLE lma (datetime TIMESTAMP_NS, lat FLOAT, lon FLOAT, alt FLOAT, chi FLOAT, pdb FLOAT, number_stations UTINYINT, plot BOOLEAN)");

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
                                files_by_day[ns_since_epoch].emplace_back(filepath);
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
                        Filter();
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
                auto save = pfd::save_file("Save animaton", "plots.gif", {"GIF", "*.gif"}).result();
                if (!save.empty())
                {
                    if (!save.ends_with(".gif"))
                        save += "gif";
                    state.ClearPlot();
                    state.StartSaveGIF(save);
                    state.timer.Start();
                    state.anime.Start();
                }
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Export animation as a .GIF video.");

            if (ImGui::MenuItem("Image"))
            {
                int fb_w, fb_h;
                glfwGetFramebufferSize(window, &fb_w, &fb_h);
                std::vector<unsigned char> pixels(state.graphics.plot_width * state.graphics.plot_height * 3);
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                glReadBuffer(GL_BACK);
                glPixelStorei(GL_PACK_ALIGNMENT, 1);
                glReadPixels(state.graphics.plot_x, fb_h - (state.graphics.plot_y + state.graphics.plot_height), state.graphics.plot_width, state.graphics.plot_height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
                for (int y = 0; y < state.graphics.plot_height / 2; ++y)
                {
                    int top = y * state.graphics.plot_width * 3;
                    int bottom = (state.graphics.plot_height - 1 - y) * state.graphics.plot_width * 3;
                    for (int x = 0; x < state.graphics.plot_width * 3; ++x)
                        std::swap(pixels[top + x], pixels[bottom + x]);
                }
                auto save = pfd::save_file("Save image", "plots.png", {"PNG", "*.png", "JPEG", "*.jpg"}).result();
                if (!save.empty())
                {
                    if (!save.ends_with(".png") && !save.ends_with(".jpg"))
                        save += ".png";

                    if (save.ends_with(".png"))
                        stbi_write_png(save.c_str(), state.graphics.plot_width, state.graphics.plot_height, 3, pixels.data(), state.graphics.plot_width * 3);
                    else
                        stbi_write_jpg(save.c_str(), state.graphics.plot_width, state.graphics.plot_height, 3, pixels.data(), 100);

                    state.status = "Saved image to " + save;
                }
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Save current view as a .PNG or .JPEG image.");

            if (ImGui::MenuItem("Data"))
            {
                auto save = pfd::save_file("Save parquet", "data.parquet", {"PARQUET", "*.parquet", "CSV", ".csv"}).result();
                if (!save.empty())
                {
                    if (!save.ends_with(".parquet") && !save.ends_with(".csv"))
                        save += ".parquet";

                    auto result = save.ends_with(".parquet")
                                      ? con.Query("COPY (SELECT * FROM lma WHERE plot = true) TO '" + save + "' (FORMAT PARQUET)")
                                      : con.Query("COPY (SELECT * FROM lma WHERE plot = true) TO '" + save + "' (FORMAT CSV)");

                    if (!result->HasError())
                        state.status = "Saved data to " + save;
                }
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Export data as .PARQUET or .CSV.");

            if (ImGui::MenuItem("State"))
            {
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Save current application state.");

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View"))
        {
            if (ImGui::MenuItem("Animate", "Ctrl+d"))
            {
                state.timer.Start();
                state.anime.Start();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Start animation playback.");

            if (ImGui::MenuItem("Reset", "F5"))
            {
                for (int i = 0; i < 5; i++)
                {
                    State::Plot *p = state.plots[i];
                    p->uv_x = 0.0f;
                    p->uv_y = 0.0f;
                    p->zoom = 1.0f;
                }
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Reset view to default.");

            if (ImGui::MenuItem("Clear"))
            {
                con.Query("DROP TABLE IF EXISTS lma");
                con.Query("DROP TABLE IF EXISTS ctg");
                state.ClearPlot();
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

    // filters
    ImGui::Text("Filters");
    if (ImGui::InputFloat("Min. Stations", &state.filter.min_stations))
    {
        Filter();
    }
    if (ImGui::InputFloat("Min. Altitude", &state.filter.min_alt))
    {
        Filter();
    }

    if (ImGui::InputFloat("Max. Altitude", &state.filter.max_alt))
    {
        Filter();
    }

    if (ImGui::InputFloat("Min. Chi", &state.filter.min_chi))
    {
        Filter();
    }

    if (ImGui::InputFloat("Max. Chi", &state.filter.max_chi))
    {
        Filter();
    }

    if (ImGui::InputFloat("Min. Power", &state.filter.min_power))
    {
        Filter();
    }

    if (ImGui::InputFloat("Max. Power", &state.filter.max_power))
    {
        Filter();
    }

    // layers:  maps, features, etc
    ImGui::Text("Layers");
    if (ImGui::Combo("Maps", &state.graphics.map.index, state.graphics.map.options.data(), state.graphics.map.options.size()))
    {
        state.timer.Start();
        state.Render();
    }

    // colors
    ImGui::Text("Colors");

    if (ImGui::Combo("Colormaps", &state.graphics.colormap.index, state.graphics.colormap.options.data(), state.graphics.colormap.options.size()))
    {
        state.timer.Start();
        state.Render();
    }
    if (ImGui::Combo("Color by", &state.graphics.colormap.by_index, state.graphics.colormap.by_options.data(), state.graphics.colormap.by_options.size()))
    {
        state.timer.Start();
        std::string color_query =
            "WITH bins AS ("
            "SELECT FLOOR(lon / 0.01) AS bin_lon, FLOOR(lat / 0.01) AS bin_lat, COUNT(*) AS bin_count "
            "FROM lma WHERE plot = true GROUP BY bin_lon, bin_lat"
            "), "
            "density AS ("
            "SELECT MAX(bin_count) AS max_c, MAX(LN(bin_count)) AS max_lc FROM bins"
            "), "
            "times AS ("
            "SELECT *, CAST(EPOCH_NS(datetime) - EPOCH_NS(DATE_TRUNC('day', MIN(datetime) OVER ())) AS FLOAT) AS time, "
            "ROW_NUMBER() OVER (ORDER BY datetime) AS row_idx, "
            "COUNT(*) OVER () AS total_rows "
            "FROM lma WHERE plot = true"
            "), "
            "bounds AS ("
            "SELECT MIN(time) AS min_time, MAX(time) AS max_time, "
            "MIN(lon) AS min_lon, MAX(lon) AS max_lon, "
            "MIN(lat) AS min_lat, MAX(lat) AS max_lat, "
            "MIN(alt) AS min_alt, MAX(alt) AS max_alt, "
            "MIN(pdb) AS min_pdb, MAX(pdb) AS max_pdb FROM times"
            ") "
            "SELECT " +
            ColorBy() + " AS color "
                        "FROM times t "
                        "JOIN bins k ON FLOOR(t.lon / 0.01) = k.bin_lon AND FLOOR(t.lat / 0.01) = k.bin_lat "
                        "CROSS JOIN density d "
                        "CROSS JOIN bounds b "
                        "ORDER BY t.time";

        auto result = con.Query(color_query);
        state.ProcessColor(result);
    }

    // animation
    ImGui::Text("Animation");
    ImGui::InputInt("Duration", &state.anime.duration);
    ImGui::Combo("Animate by", &state.anime.by_index, state.anime.options.data(), state.anime.options.size());

    // other
    ImGui::Text("Other");
    if (ImGui::InputInt("VHF Size", &state.theme.vhf_size, 1, 2))
    {
        state.timer.Start();
        state.theme.vhf_size = std::clamp(state.theme.vhf_size, 1, 10);
        state.SetVHFShader();
    }
    if (ImGui::RadioButton("Light", &state.theme.dark, 0))
    {
        state.Flip();
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Dark", &state.theme.dark, 1))
    {
        state.Flip();
    }
    ImGui::SameLine();
    ImGui::Text(" Theme");

    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("##Plots", ImVec2(0, 0), ImGuiChildFlags_Borders);
    ImVec2 plot_pos = ImGui::GetWindowPos();
    ImVec2 plot_size = ImGui::GetWindowSize();
    state.graphics.plot_x = plot_pos.x;
    state.graphics.plot_y = plot_pos.y;
    state.graphics.plot_height = plot_size.y;
    state.graphics.plot_width = plot_size.x;

    float fixed_plot_height = ImGui::GetContentRegionAvail().y * 0.18f;
    float fixed_plot_width = ImGui::GetContentRegionAvail().x * 0.8f;
    float axis_size = ImGui::GetFontSize() * 1.8f;
    float tick_height = ImGui::GetFontSize() * 0.4f;
    ImGui::BeginChild("##TimeAltitude", ImVec2(-1, fixed_plot_height), ImGuiChildFlags_Borders);
    {
        ImVec2 pos = ImGui::GetCursorScreenPos();
        state.time_alt.x = pos.x;
        state.time_alt.y = pos.y;
        ImVec2 window_size = ImGui::GetWindowSize();
        float width = window_size.x;
        float height = window_size.y;
        state.time_alt.width = width - axis_size;
        state.time_alt.height = height - axis_size;
        ImGui::BeginChild("##AltAxis1", ImVec2(axis_size, height - axis_size), false);
        {
            ImDrawList *draw_list = ImGui::GetWindowDrawList();
            ImVec2 p = ImGui::GetWindowPos();
            float tick_positions[] = {0.2f, 0.5f, 0.8f};
            for (int i = 0; i < 3; i++)
            {
                float y = p.y + tick_positions[i] * state.time_alt.height;
                float x = p.x + axis_size;
                draw_list->AddLine(ImVec2(x, y), ImVec2(x - tick_height, y), IM_COL32(state.theme.color_32, state.theme.color_32, state.theme.color_32, 255), 1.0f);
                const char *label = state.time_alt.y_major_ticks[i].c_str();
                ImVec2 text_size = ImGui::CalcTextSize(label);
                AddVerticalText(draw_list, label, ImVec2(x - tick_height - text_size.y, y + text_size.x * 0.5f), IM_COL32(state.theme.color_32, state.theme.color_32, state.theme.color_32, 255));
            }
        }
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::Image((ImTextureID)state.time_alt.texture, ImVec2(width - axis_size, height - axis_size), ImVec2(state.time_alt.uv_x, state.time_alt.uv_y),
                     ImVec2(state.time_alt.uv_x + state.time_alt.zoom, state.time_alt.uv_y + state.time_alt.zoom));
        ImGui::BeginChild("##Node1", ImVec2(axis_size, axis_size), false);
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("##TimeAxis1", ImVec2(width - axis_size, axis_size), false);
        {
            ImDrawList *draw_list = ImGui::GetWindowDrawList();
            ImVec2 p = ImGui::GetWindowPos();
            float tick_positions[] = {0.1f, 0.3f, 0.5f, 0.7f, 0.9f};
            for (int i = 0; i < 5; i++)
            {
                float x = p.x + tick_positions[i] * state.time_alt.width;
                draw_list->AddLine(ImVec2(x, p.y), ImVec2(x, p.y + tick_height), IM_COL32(state.theme.color_32, state.theme.color_32, state.theme.color_32, 255), 1.0f);
                const char *label = state.time_alt.x_major_ticks[i].c_str();
                ImVec2 text_size = ImGui::CalcTextSize(label);
                draw_list->AddText(ImVec2(x - text_size.x * 0.5f, p.y + tick_height), IM_COL32(state.theme.color_32, state.theme.color_32, state.theme.color_32, 255), label);
            }
        }
        ImGui::EndChild();
    }
    ImGui::EndChild();
    ImGui::BeginChild("##LongitudeAltitude", ImVec2(fixed_plot_width, fixed_plot_height), ImGuiChildFlags_Borders);
    {
        ImVec2 pos = ImGui::GetCursorScreenPos();
        state.lon_alt.x = pos.x;
        state.lon_alt.y = pos.y;
        ImVec2 window_size = ImGui::GetWindowSize();
        float width = window_size.x;
        float height = window_size.y;
        state.lon_alt.width = width - axis_size;
        state.lon_alt.height = height - axis_size;
        ImGui::BeginChild("##AltAxis2", ImVec2(axis_size, height - axis_size), false);
        {
            ImDrawList *draw_list = ImGui::GetWindowDrawList();
            ImVec2 p = ImGui::GetWindowPos();
            float tick_positions[] = {0.2f, 0.5f, 0.8f};
            for (int i = 0; i < 3; i++)
            {
                float y = p.y + tick_positions[i] * state.lon_alt.height;
                float x = p.x + axis_size;
                draw_list->AddLine(ImVec2(x, y), ImVec2(x - tick_height, y), IM_COL32(state.theme.color_32, state.theme.color_32, state.theme.color_32, 255), 1.0f);
                const char *label = state.lon_alt.y_major_ticks[i].c_str();
                ImVec2 text_size = ImGui::CalcTextSize(label);
                AddVerticalText(draw_list, label, ImVec2(x - tick_height - text_size.y, y + text_size.x * 0.5f), IM_COL32(state.theme.color_32, state.theme.color_32, state.theme.color_32, 255));
            }
        }
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::Image((ImTextureID)state.lon_alt.texture, ImVec2(width - axis_size, height - axis_size), ImVec2(state.lon_alt.uv_x, state.lon_alt.uv_y),
                     ImVec2(state.lon_alt.uv_x + state.lon_alt.zoom, state.lon_alt.uv_y + state.lon_alt.zoom));
        ImGui::BeginChild("##Node2", ImVec2(axis_size, axis_size), false);
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("##LonAxis2", ImVec2(width - axis_size, axis_size), false);
        {
            ImDrawList *draw_list = ImGui::GetWindowDrawList();
            ImVec2 p = ImGui::GetWindowPos();
            float tick_positions[] = {0.1f, 0.3f, 0.5f, 0.7f, 0.9f};
            for (int i = 0; i < 5; i++)
            {
                float x = p.x + tick_positions[i] * state.lon_alt.width;
                draw_list->AddLine(ImVec2(x, p.y), ImVec2(x, p.y + tick_height), IM_COL32(state.theme.color_32, state.theme.color_32, state.theme.color_32, 255), 1.0f);
                const char *label = state.lon_alt.x_major_ticks[i].c_str();
                ImVec2 text_size = ImGui::CalcTextSize(label);
                draw_list->AddText(ImVec2(x - text_size.x * 0.5f, p.y + tick_height), IM_COL32(state.theme.color_32, state.theme.color_32, state.theme.color_32, 255), label);
            }
        }
        ImGui::EndChild();
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##AltitudeHistogram", ImVec2(-1, fixed_plot_height), ImGuiChildFlags_Borders);
    {
        ImVec2 pos = ImGui::GetCursorScreenPos();
        state.alt_hist.x = pos.x;
        state.alt_hist.y = pos.y;
        ImVec2 window_size = ImGui::GetWindowSize();
        float width = window_size.x;
        float height = window_size.y;
        state.alt_hist.width = width - axis_size;
        state.alt_hist.height = height - axis_size;
        ImGui::BeginChild("##AltAxis3", ImVec2(axis_size, height - axis_size), false);
        {
            ImDrawList *draw_list = ImGui::GetWindowDrawList();
            ImVec2 p = ImGui::GetWindowPos();
            float tick_positions[] = {0.2f, 0.5f, 0.8f};
            for (int i = 0; i < 3; i++)
            {
                float y = p.y + tick_positions[i] * state.alt_hist.height;
                float x = p.x + axis_size;
                draw_list->AddLine(ImVec2(x, y), ImVec2(x - tick_height, y), IM_COL32(state.theme.color_32, state.theme.color_32, state.theme.color_32, 255), 1.0f);
                const char *label = state.alt_hist.y_major_ticks[i].c_str();
                ImVec2 text_size = ImGui::CalcTextSize(label);
                AddVerticalText(draw_list, label, ImVec2(x - tick_height - text_size.y, y + text_size.x * 0.5f), IM_COL32(state.theme.color_32, state.theme.color_32, state.theme.color_32, 255));
            }
        }
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::Image((ImTextureID)state.alt_hist.texture, ImVec2(width - axis_size, height - axis_size), ImVec2(state.alt_hist.uv_x, state.alt_hist.uv_y),
                     ImVec2(state.alt_hist.uv_x + state.alt_hist.zoom, state.alt_hist.uv_y + state.alt_hist.zoom));
        ImGui::BeginChild("##Node3", ImVec2(axis_size, axis_size), false);
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("##CountAxis3", ImVec2(width - axis_size, axis_size), false);
        {
            ImDrawList *draw_list = ImGui::GetWindowDrawList();
            ImVec2 p = ImGui::GetWindowPos();
            float tick_positions[] = {0.2f, 0.5f, 0.8f};
            for (int i = 0; i < 3; i++)
            {
                float x = p.x + tick_positions[i] * state.alt_hist.width;
                draw_list->AddLine(ImVec2(x, p.y), ImVec2(x, p.y + tick_height), IM_COL32(state.theme.color_32, state.theme.color_32, state.theme.color_32, 255), 1.0f);
                const char *label = state.alt_hist.x_major_ticks[i].c_str();
                ImVec2 text_size = ImGui::CalcTextSize(label);
                draw_list->AddText(ImVec2(x - text_size.x * 0.5f, p.y + tick_height), IM_COL32(state.theme.color_32, state.theme.color_32, state.theme.color_32, 255), label);
            }
        }
        ImGui::EndChild();
    }
    ImGui::EndChild();
    ImGui::BeginChild("##LongitudeLatitude", ImVec2(fixed_plot_width, -1), ImGuiChildFlags_Borders);
    {
        ImVec2 pos = ImGui::GetCursorScreenPos();
        state.lon_lat.x = pos.x;
        state.lon_lat.y = pos.y;
        ImVec2 window_size = ImGui::GetWindowSize();
        float width = window_size.x;
        float height = window_size.y;
        state.lon_lat.width = width - axis_size;
        state.lon_lat.height = height - axis_size;
        ImGui::BeginChild("##LatAxis4", ImVec2(axis_size, height - axis_size), false);
        {
            ImDrawList *draw_list = ImGui::GetWindowDrawList();
            ImVec2 p = ImGui::GetWindowPos();
            float tick_positions[] = {0.1f, 0.3f, 0.5f, 0.7f, 0.9f};
            for (int i = 0; i < 5; i++)
            {
                float y = p.y + tick_positions[i] * state.lon_lat.height;
                float x = p.x + axis_size;
                draw_list->AddLine(ImVec2(x, y), ImVec2(x - tick_height, y), IM_COL32(state.theme.color_32, state.theme.color_32, state.theme.color_32, 255), 1.0f);
                const char *label = state.lon_lat.y_major_ticks[i].c_str();
                ImVec2 text_size = ImGui::CalcTextSize(label);
                AddVerticalText(draw_list, label, ImVec2(x - tick_height - text_size.y, y + text_size.x * 0.5f), IM_COL32(state.theme.color_32, state.theme.color_32, state.theme.color_32, 255));
            }
        }
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::Image((ImTextureID)state.lon_lat.texture, ImVec2(width - axis_size, height - axis_size), ImVec2(state.lon_lat.uv_x, state.lon_lat.uv_y),
                     ImVec2(state.lon_lat.uv_x + state.lon_lat.zoom, state.lon_lat.uv_y + state.lon_lat.zoom));
        ImGui::BeginChild("##Node4", ImVec2(axis_size, axis_size), false);
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("##LonAxis4", ImVec2(width - axis_size, axis_size), false);
        {
            ImDrawList *draw_list = ImGui::GetWindowDrawList();
            ImVec2 p = ImGui::GetWindowPos();
            float tick_positions[] = {0.1f, 0.3f, 0.5f, 0.7f, 0.9f};
            for (int i = 0; i < 5; i++)
            {
                float x = p.x + tick_positions[i] * state.lon_lat.width;
                draw_list->AddLine(ImVec2(x, p.y), ImVec2(x, p.y + tick_height), IM_COL32(state.theme.color_32, state.theme.color_32, state.theme.color_32, 255), 1.0f);
                const char *label = state.lon_lat.x_major_ticks[i].c_str();
                ImVec2 text_size = ImGui::CalcTextSize(label);
                draw_list->AddText(ImVec2(x - text_size.x * 0.5f, p.y + tick_height), IM_COL32(state.theme.color_32, state.theme.color_32, state.theme.color_32, 255), label);
            }
        }
        ImGui::EndChild();
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##AltitudeLatitude", ImVec2(-1, -1), ImGuiChildFlags_Borders);
    {
        ImVec2 pos = ImGui::GetCursorScreenPos();
        state.alt_lat.x = pos.x;
        state.alt_lat.y = pos.y;
        ImVec2 window_size = ImGui::GetWindowSize();
        float width = window_size.x;
        float height = window_size.y;
        state.alt_lat.width = width - axis_size;
        state.alt_lat.height = height - axis_size;
        ImGui::BeginChild("##LatAxis5", ImVec2(axis_size, height - axis_size), false);
        {
            ImDrawList *draw_list = ImGui::GetWindowDrawList();
            ImVec2 p = ImGui::GetWindowPos();
            float tick_positions[] = {0.1f, 0.3f, 0.5f, 0.7f, 0.9f};
            for (int i = 0; i < 5; i++)
            {
                float y = p.y + tick_positions[i] * state.alt_lat.height;
                float x = p.x + axis_size;
                draw_list->AddLine(ImVec2(x, y), ImVec2(x - tick_height, y), IM_COL32(state.theme.color_32, state.theme.color_32, state.theme.color_32, 255), 1.0f);
                const char *label = state.alt_lat.y_major_ticks[i].c_str();
                ImVec2 text_size = ImGui::CalcTextSize(label);
                AddVerticalText(draw_list, label, ImVec2(x - tick_height - text_size.y, y + text_size.x * 0.5f), IM_COL32(state.theme.color_32, state.theme.color_32, state.theme.color_32, 255));
            }
        }
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::Image((ImTextureID)state.alt_lat.texture, ImVec2(width - axis_size, height - axis_size), ImVec2(state.alt_lat.uv_x, state.alt_lat.uv_y),
                     ImVec2(state.alt_lat.uv_x + state.alt_lat.zoom, state.alt_lat.uv_y + state.alt_lat.zoom));
        ImGui::BeginChild("##Node5", ImVec2(axis_size, axis_size), false);
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("##AltAxis5", ImVec2(width - axis_size, axis_size), false);
        {
            ImDrawList *draw_list = ImGui::GetWindowDrawList();
            ImVec2 p = ImGui::GetWindowPos();
            float tick_positions[] = {0.2f, 0.5f, 0.8f};
            for (int i = 0; i < 3; i++)
            {
                float x = p.x + tick_positions[i] * state.alt_lat.width;
                draw_list->AddLine(ImVec2(x, p.y), ImVec2(x, p.y + tick_height), IM_COL32(state.theme.color_32, state.theme.color_32, state.theme.color_32, 255), 1.0f);
                const char *label = state.alt_lat.x_major_ticks[i].c_str();
                ImVec2 text_size = ImGui::CalcTextSize(label);
                draw_list->AddText(ImVec2(x - text_size.x * 0.5f, p.y + tick_height), IM_COL32(state.theme.color_32, state.theme.color_32, state.theme.color_32, 255), label);
            }
        }
        ImGui::EndChild();
    }
    ImGui::EndChild();
    ImGui::EndChild();
    ImGui::PopStyleVar(2);

    // shorcut handling
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_D))
    {
        state.timer.Start();
        state.anime.Start();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F5))
    {
        for (int i = 0; i < 5; i++)
        {
            State::Plot *p = state.plots[i];
            p->uv_x = 0.0f;
            p->uv_y = 0.0f;
            p->zoom = 1.0f;
        }
    }

    // interactivity
    ImVec2 mouse = ImGui::GetMousePos();
    float scroll = ImGui::GetIO().MouseWheel;
    if (scroll != 0.0f)
    {
        for (int i = 0; i < 5; i++)
        {
            State::Plot *p = state.plots[i];
            if (mouse.x >= p->x && mouse.y >= p->y &&
                mouse.x <= p->x + p->width && mouse.y <= p->y + p->height)
            {
                float mx = (mouse.x - p->x) / p->width;
                float my = (mouse.y - p->y) / p->height;
                float uv_mouse_x = p->uv_x + mx * p->zoom;
                float uv_mouse_y = p->uv_y + my * p->zoom;
                float old_zoom = p->zoom;
                p->zoom -= scroll * 0.05f;
                p->zoom = std::clamp(p->zoom, 0.1f, 1.0f);
                p->uv_x = uv_mouse_x - mx * p->zoom;
                p->uv_y = uv_mouse_y - my * p->zoom;
                p->uv_x = std::clamp(p->uv_x, 0.0f, 1.0f - p->zoom);
                p->uv_y = std::clamp(p->uv_y, 0.0f, 1.0f - p->zoom);
                break;
            }
        }
    }
    if (ImGui::GetIO().MouseDown[1])
    {
        ImVec2 delta = ImGui::GetIO().MouseDelta;
        for (int i = 0; i < 5; i++)
        {
            State::Plot *p = state.plots[i];
            if (mouse.x >= p->x && mouse.y >= p->y &&
                mouse.x <= p->x + p->width && mouse.y <= p->y + p->height)
            {
                p->uv_x -= (delta.x / p->width) * p->zoom;
                p->uv_y -= (delta.y / p->height) * p->zoom;
                p->uv_x = std::clamp(p->uv_x, 0.0f, 1.0f - p->zoom);
                p->uv_y = std::clamp(p->uv_y, 0.0f, 1.0f - p->zoom);
                break;
            }
        }
    }
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
    window = glfwCreateWindow(mode->width, mode->height, "Aggie XLMA", nullptr, nullptr);

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

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        if (state.theme.dark == 1)
            ImGui::StyleColorsDark();
        else
            ImGui::StyleColorsLight();

        if (state.anime.animating) // animation handler has to work per frame of UI renderer
            state.Frame();
        RenderUI();

        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);

        if (state.anime.saving)
            state.SaveGIFFrame();
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}