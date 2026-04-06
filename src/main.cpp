#define IMGUI_DEFINE_MATH_OPERATORS
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
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
#include "stb_image.h"
#include <ctime>

duckdb::DuckDB db(nullptr); // in memory databse
duckdb::Connection con(db); // connection to database
State state;                // state of application
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
                        con.Query("CREATE TABLE lma (datetime TIMESTAMP_NS, lat FLOAT, lon FLOAT, alt FLOAT, chi FLOAT, pdb FLOAT, number_stations UTINYINT, plot BOOLEAN DEFAULT true, epoch FLOAT)");
                        
                        state.graphics.ReadStations(selection[0]);
                        state.graphics.Reset(); // resetting zoom and pan

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
                                                            "TRY_CAST(arr[2] AS FLOAT), "
                                                            "TRY_CAST(arr[3] AS FLOAT), "
                                                            "TRY(CAST(arr[4] AS FLOAT) / 1000), "
                                                            "TRY_CAST(arr[5] AS FLOAT), "
                                                            "TRY_CAST(arr[6] AS FLOAT), "
                                                            "CAST(bit_count(TRY_CAST(arr[7] AS INTEGER)) AS UTINYINT) "
                                                            "FROM ("
                                                            "SELECT REGEXP_SPLIT_TO_ARRAY(TRIM(column0), ' +') AS arr "
                                                            "FROM read_csv(" +
                                paths_sql + ", auto_detect=false, delim='|', quote='\"', escape='\"', "
                                            "new_line='\\n', comment='', columns={'column0':'VARCHAR'}, header=false, skip=53)"
                                            ") t;");
                            con.Query("UPDATE lma SET epoch = CAST(EPOCH_NS(datetime) - EPOCH_NS(DATE_TRUNC('day', (SELECT MIN(datetime) FROM lma))) AS FLOAT)");
                        }
                        state.status = "Loaded " + std::to_string(selection.size()) + " files";
                        state.Filter();
                    }
                    catch (const std::exception &e)
                    {
                        state.status = "Exception " + std::string(e.what()) + " happened when trying to load files.";
                    }
                }
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Open LYLOUT files ending with .dat or .dat.gz.");

            if (ImGui::MenuItem("ENTLN"))
            {
                auto selection = pfd::open_file(
                                     "Select ENTLN/NLDN file(s)",
                                     "",
                                     {"ENTLN/NLDN files", "*.csv *.txt"},
                                     pfd::opt::multiselect)
                                     .result();

                if (!selection.empty())
                {

                    state.timer.Start();
                    try
                    {
                        std::string paths_sql = "[";
                        for (size_t i = 0; i < selection.size(); ++i)
                        {
                            paths_sql += "'" + selection[i] + "'";
                            if (i + 1 < selection.size())
                                paths_sql += ",";
                        }
                        paths_sql += "]";

                        con.Query("DROP TABLE IF EXISTS entln");
                        std::string entln_query =
                            "CREATE TABLE entln AS SELECT * FROM read_csv(" + paths_sql + ", "
                                                                                          "auto_detect=false, delim=',', new_line='\\n', skip=0, header=true, "
                                                                                          "columns={'type': 'BIGINT', 'timestamp': 'TIMESTAMP', 'latitude': 'DOUBLE', "
                                                                                          "'longitude': 'DOUBLE', 'peakcurrent': 'BIGINT', 'icheight': 'BIGINT', "
                                                                                          "'numbersensor': 'BIGINT', 'majoraxis': 'DOUBLE', 'minoraxis': 'DOUBLE', "
                                                                                          "'bearing': 'VARCHAR'})";
                        con.Query(entln_query);
                        state.EntlnFilter();
                    }
                    catch (const std::exception &e)
                    {
                        state.status = "Exception " + std::string(e.what()) + " happened when trying to load files.";
                    }
                }
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Open ENTLN lightning data.");

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
                    state.graphics.ClearPlot();
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
                std::vector<unsigned char> pixels(state.graphics.rect.w * state.graphics.rect.h * 3);
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                glReadBuffer(GL_BACK);
                glPixelStorei(GL_PACK_ALIGNMENT, 1);
                glReadPixels(state.graphics.rect.x, fb_h - (state.graphics.rect.y + state.graphics.rect.h), state.graphics.rect.w, state.graphics.rect.h, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
                for (int y = 0; y < state.graphics.rect.h / 2; ++y)
                {
                    int top = y * state.graphics.rect.w * 3;
                    int bottom = (state.graphics.rect.h - 1 - y) * state.graphics.rect.w * 3;
                    for (int x = 0; x < state.graphics.rect.w * 3; ++x)
                        std::swap(pixels[top + x], pixels[bottom + x]);
                }
                auto save = pfd::save_file("Save image", "plots.png", {"PNG", "*.png", "JPEG", "*.jpg"}).result();
                if (!save.empty())
                {
                    if (!save.ends_with(".png") && !save.ends_with(".jpg"))
                        save += ".png";

                    if (save.ends_with(".png"))
                        stbi_write_png(save.c_str(), state.graphics.rect.w, state.graphics.rect.h, 3, pixels.data(), state.graphics.rect.w * 3);
                    else
                        stbi_write_jpg(save.c_str(), state.graphics.rect.w, state.graphics.rect.h, 3, pixels.data(), 100);

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
                for (Graphics::Plot *p : state.graphics.plots)
                {
                    p->uv_x = 0.0f;
                    p->uv_y = 0.0f;
                    p->zoom = 1.0f;
                }
                state.graphics.UpdateTickLabels();
                state.graphics.Render();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Reset view to default.");

            if (ImGui::MenuItem("Clear"))
            {
                con.Query("DROP TABLE IF EXISTS lma");
                con.Query("DROP TABLE IF EXISTS ctg");
                state.graphics.ClearPlot();
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
        state.Filter();
    }
    if (ImGui::InputFloat("Min. Altitude", &state.filter.min_alt))
    {
        state.Filter();
    }

    if (ImGui::InputFloat("Max. Altitude", &state.filter.max_alt))
    {
        state.Filter();
    }

    if (ImGui::InputFloat("Min. Chi", &state.filter.min_chi))
    {
        state.Filter();
    }

    if (ImGui::InputFloat("Max. Chi", &state.filter.max_chi))
    {
        state.Filter();
    }

    if (ImGui::InputFloat("Min. Power", &state.filter.min_power))
    {
        state.Filter();
    }

    if (ImGui::InputFloat("Max. Power", &state.filter.max_power))
    {
        state.Filter();
    }

    // layers:  maps, features, etc
    ImGui::Text("Layers");
    if (ImGui::Combo("Maps", &state.graphics.map.index, state.graphics.map.options.data(), state.graphics.map.options.size()))
    {
        state.timer.Start();
        state.graphics.Render();
    }
    if (ImGui::Checkbox("ENTLN IC&CC", &state.graphics.entln.ic))
    {
        state.timer.Start();
        state.graphics.Render();
    }
    if (ImGui::Checkbox("ENTLN CG", &state.graphics.entln.cg))
    {
        state.timer.Start();
        state.graphics.Render();
    }

    // selection: keep/remove
    ImGui::Text("Select");
    if (ImGui::RadioButton("Keep", state.polyselect.keep == true))
        state.polyselect.keep = true;
    ImGui::SameLine();
    if (ImGui::RadioButton("Remove", state.polyselect.keep == false))
        state.polyselect.keep = false;
    if (ImGui::Button("Cancel"))
        state.polyselect.Reset();

    // colors
    ImGui::Text("Colors");

    if (ImGui::Combo("Colormaps", &state.graphics.colormap.index, state.graphics.colormap.options.data(), state.graphics.colormap.options.size()))
    {
        state.timer.Start();
        state.graphics.Render();
    }
    if (ImGui::Combo("Color by", &state.graphics.colormap.by_index, state.graphics.colormap.by_options.data(), state.graphics.colormap.by_options.size()))
    {
        state.Color();
    }

    // animation
    ImGui::Text("Animation");
    ImGui::InputInt("Duration", &state.anime.duration);
    ImGui::Combo("Animate by", &state.anime.by_index, state.anime.options.data(), state.anime.options.size());

    // other
    ImGui::Text("Other");
    if (ImGui::InputInt("VHF Size", &state.style.size.vhf, 1, 2))
    {
        state.timer.Start();
        state.style.size.vhf = std::clamp(state.style.size.vhf, 1, 10);
        state.graphics.shader.UpdateVHFShader(state.style.size.vhf);
        state.graphics.Render();
    }
    if (ImGui::RadioButton("Light", state.style.mode == State::Style::Mode::Light))
        state.Flip();
    ImGui::SameLine();
    if (ImGui::RadioButton("Dark", state.style.mode == State::Style::Mode::Dark))
        state.Flip();
    ImGui::SameLine();
    ImGui::Text(" Theme");

    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("##Plots", ImVec2(0, 0), ImGuiChildFlags_Borders);
    ImVec2 plot_pos = ImGui::GetWindowPos();
    ImVec2 plot_size = ImGui::GetWindowSize();
    state.graphics.rect.x = plot_pos.x;
    state.graphics.rect.y = plot_pos.y;
    state.graphics.rect.h = plot_size.y;
    state.graphics.rect.w = plot_size.x;

    // plots rendering
    float fixed_plot_height = ImGui::GetContentRegionAvail().y * 0.18f;
    float fixed_plot_width = ImGui::GetContentRegionAvail().x * 0.8f;
    float axis_size = ImGui::GetFontSize() * 1.8f;
    float tick_height = ImGui::GetFontSize() * 0.4f;
    static const float three_ticks[] = {0.2f, 0.5f, 0.8f};
    static const float five_ticks[] = {0.1f, 0.3f, 0.5f, 0.7f, 0.9f};
    ImVec2 mouse = ImGui::GetMousePos();
    float scroll = ImGui::GetIO().MouseWheel;
    // lamba helper for cleaner appearence
    auto plot = [&](const char *id, Graphics::Plot *p, ImVec2 size, int y_ticks, int x_ticks)
    {
        const float *y_pos = y_ticks == 3 ? three_ticks : five_ticks;
        const float *x_pos = x_ticks == 3 ? three_ticks : five_ticks;
        ImGui::BeginChild(id, size, ImGuiChildFlags_Borders);
        {
            ImVec2 window_size = ImGui::GetWindowSize();
            float width = window_size.x;
            float height = window_size.y;
            // y axis
            ImGui::BeginChild((std::string(id) + "_yaxis").c_str(), ImVec2(axis_size, height - axis_size), false);
            {
                ImDrawList *draw_list = ImGui::GetWindowDrawList();
                ImVec2 wp = ImGui::GetWindowPos();
                for (int i = 0; i < y_ticks; i++)
                {
                    float y = wp.y + (1.0f - y_pos[i]) * p->rect.h - (ImGui::GetStyle().FramePadding.y + ImGui::GetStyle().ChildBorderSize);
                    float x = wp.x + axis_size;
                    draw_list->AddLine(ImVec2(x, y), ImVec2(x - tick_height, y), IM_COL32(state.style.color.as_int, state.style.color.as_int, state.style.color.as_int, 255), 1.0f);
                    const char *label = p->y_major_ticks[i].c_str();
                    ImVec2 text_size = ImGui::CalcTextSize(label);
                    AddVerticalText(draw_list, label, ImVec2(x - tick_height - text_size.y, y + text_size.x * 0.5f), IM_COL32(state.style.color.as_int, state.style.color.as_int, state.style.color.as_int, 255));
                }
            }
            ImGui::EndChild();
            ImGui::SameLine();
            ImVec2 pos = ImGui::GetCursorScreenPos();
            p->rect.x = pos.x;
            p->rect.y = pos.y;
            p->rect.w = width - axis_size;
            p->rect.h = height - axis_size;
            ImGui::Image((ImTextureID)p->texture, ImVec2(width - axis_size, height - axis_size), ImVec2(0, 0), ImVec2(1, 1));
            // x axis
            ImGui::BeginChild((std::string(id) + "_node").c_str(), ImVec2(axis_size, axis_size), false);
            ImGui::EndChild();
            ImGui::SameLine();
            ImGui::BeginChild((std::string(id) + "_xaxis").c_str(), ImVec2(width - axis_size, axis_size), false);
            {
                ImDrawList *draw_list = ImGui::GetWindowDrawList();
                ImVec2 wp = ImGui::GetWindowPos();
                for (int i = 0; i < x_ticks; i++)
                {
                    float x = wp.x + x_pos[i] * p->rect.w;
                    draw_list->AddLine(ImVec2(x, wp.y), ImVec2(x, wp.y + tick_height), IM_COL32(state.style.color.as_int, state.style.color.as_int, state.style.color.as_int, 255), 1.0f);
                    const char *label = p->x_major_ticks[i].c_str();
                    ImVec2 text_size = ImGui::CalcTextSize(label);
                    draw_list->AddText(ImVec2(x - text_size.x * 0.5f, wp.y + tick_height), IM_COL32(state.style.color.as_int, state.style.color.as_int, state.style.color.as_int, 255), label);
                }
            }
            ImGui::EndChild();
        }
        ImGui::EndChild();

        // plot specific interactivity
        bool hovered = mouse.x >= p->rect.x && mouse.y >= p->rect.y &&
                       mouse.x <= p->rect.x + p->rect.w && mouse.y <= p->rect.y + p->rect.h;
        // zoom
        if (!state.polyselect.selecting && scroll != 0.0f && hovered)
        {
            float mx = (mouse.x - p->rect.x) / p->rect.w;
            float my = (mouse.y - p->rect.y) / p->rect.h;
            float uv_mouse_x = p->uv_x + mx * p->zoom;
            float uv_mouse_y = p->uv_y + my * p->zoom;
            p->zoom -= scroll * 0.05f;
            p->zoom = std::clamp(p->zoom, 0.1f, 1.0f);
            p->uv_x = uv_mouse_x - mx * p->zoom;
            p->uv_y = uv_mouse_y - my * p->zoom;
            p->uv_x = std::clamp(p->uv_x, 0.0f, 1.0f - p->zoom);
            p->uv_y = std::clamp(p->uv_y, 0.0f, 1.0f - p->zoom);
            state.graphics.UpdateTickLabels();
            state.graphics.Render(p);
        }
        // pan
        // if (!state.polyselect.selecting && ImGui::GetIO().MouseDown[1] && hovered)
        // {
        //     ImVec2 delta = ImGui::GetIO().MouseDelta;
        //     // 0.5 is sensitivity, feel free to tune it
        //     p->uv_x -= (delta.x / p->rect.w) * p->zoom * 0.5f;
        //     p->uv_y += (delta.y / p->rect.h) * p->zoom * 0.5f;
        //     p->uv_x = std::clamp(p->uv_x, 0.0f, 1.0f - p->zoom);
        //     p->uv_y = std::clamp(p->uv_y, 0.0f, 1.0f - p->zoom);
        //     state.graphics.UpdateTickLabels();
        //     state.graphics.Render(p);
        // }
        // selecting
        if (ImGui::IsMouseClicked(0) && hovered) // left click
            state.polyselect.Add(mouse, p);
        if (state.polyselect.selecting && ImGui::IsMouseClicked(1) && hovered) // right click
            state.polyselect.Add(mouse, p, true);
        if (state.polyselect.selecting && state.polyselect.plot == p && hovered)
        {
            ImDrawList *draw_list = ImGui::GetForegroundDrawList();
            ImU32 color = state.polyselect.keep ? IM_COL32(0, 255, 0, 255) : IM_COL32(255, 0, 0, 255);
            ImU32 faded = state.polyselect.keep ? IM_COL32(0, 255, 0, 128) : IM_COL32(255, 0, 0, 128);
            for (int i = 0; i + 1 < state.polyselect.clicks.size(); i++)
            {
                draw_list->AddLine(state.polyselect.clicks[i], state.polyselect.clicks[i + 1], color, 2.0f);
                draw_list->AddCircleFilled(state.polyselect.clicks[i], 4.0f, color);
            }
            draw_list->AddCircleFilled(state.polyselect.clicks.back(), 4.0f, color);
            draw_list->AddLine(state.polyselect.clicks.back(), mouse, faded, 1.0f);
            draw_list->AddLine(mouse, state.polyselect.clicks.front(), faded, 1.0f);
            // state.status = std::format("Polygon has {} points, {}ing selection", state.polyselect.clicks.size(), state.polyselect.keep ? "keep" : "remov");
        }
    };
    plot("##TimeAltitude", &state.graphics.time_alt, ImVec2(-1, fixed_plot_height), 3, 5);
    plot("##LongitudeAltitude", &state.graphics.lon_alt, ImVec2(fixed_plot_width, fixed_plot_height), 3, 5);
    ImGui::SameLine();
    plot("##AltitudeHistogram", &state.graphics.alt_hist, ImVec2(-1, fixed_plot_height), 3, 3);
    plot("##LongitudeLatitude", &state.graphics.lon_lat, ImVec2(fixed_plot_width, -1), 5, 5);
    ImGui::SameLine();
    plot("##AltitudeLatitude", &state.graphics.alt_lat, ImVec2(-1, -1), 5, 3);

    ImGui::EndChild();
    ImGui::PopStyleVar(2);

    // shorcut handling
    // ctrl + d: animation
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_D))
    {
        state.timer.Start();
        state.anime.Start();
    }
    // f5: reset / clear plots
    if (ImGui::IsKeyPressed(ImGuiKey_F5))
    {
        for (int i = 0; i < 5; i++)
        {
            Graphics::Plot *p = state.graphics.plots[i];
            p->uv_x = 0.0f;
            p->uv_y = 0.0f;
            p->zoom = 1.0f;
        }
        state.graphics.UpdateTickLabels();
        state.graphics.Render();
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

    // icon
    GLFWimage icon;
    icon.pixels = stbi_load("bin/xlma.png", &icon.width, &icon.height, nullptr, 4);
    if (icon.pixels)
    {
        glfwSetWindowIcon(window, 1, &icon);
        stbi_image_free(icon.pixels);
    }

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

    // initial renderui so i can initialize shaders, textures, fbo
    con.Query("INSTALL spatial");
    con.Query("LOAD spatial"); //  loading spatial
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    RenderUI();
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(window);
    state.graphics.Initialize();
    state.graphics.ClearPlot();

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        state.style.mode == State::Style::Mode::Dark ? ImGui::StyleColorsDark() : ImGui::StyleColorsLight();

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