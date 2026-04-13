#define IMGUI_DEFINE_MATH_OPERATORS
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define GIF_FLIP_VERT
#define MINIAUDIO_IMPLEMENTATION

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

// global vars
State state; // state of application
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

void Tick()
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
                    try
                    {
                        state.db.con.Query("DROP TABLE IF EXISTS lma");
                        state.db.con.Query("DROP TABLE IF EXISTS lma_tl");
                        state.db.con.Query("CREATE TABLE vhf_tl (tl_1 BOOLEAN, tl_2 BOOLEAN, tl_3 BOOLEAN, tl_4 BOOLEAN, tl_5 BOOLEAN)");
                        state.db.con.Query("CREATE TABLE lma (datetime TIMESTAMP_NS, lat FLOAT, lon FLOAT, alt FLOAT, chi FLOAT, pdb FLOAT, number_stations UTINYINT, plot BOOLEAN DEFAULT true, epoch FLOAT)");

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

                            state.db.con.Query(
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
                            state.db.con.Query("UPDATE lma SET epoch = CAST(EPOCH_NS(datetime) - EPOCH_NS(DATE_TRUNC('day', (SELECT MIN(datetime) FROM lma))) AS FLOAT)");
                        }
                        state.db.con.Query("INSERT INTO vhf_tl SELECT plot, plot, plot, plot, plot FROM lma"); // initializing the vhf timeline db
                        state.status.global = "Loaded " + std::to_string(selection.size()) + " LYLOUT files";
                        state.VhfFilter();
                    }
                    catch (const std::exception &e)
                    {
                        state.status.global = "Exception " + std::string(e.what()) + " happened when trying to load LYLOUT files.";
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

                        state.db.con.Query("DROP TABLE IF EXISTS entln");
                        state.db.con.Query("DROP TABLE IF EXISTS entln_tl");
                        state.db.con.Query("CREATE TABLE entln_tl (tl_1 BOOLEAN, tl_2 BOOLEAN, tl_3 BOOLEAN, tl_4 BOOLEAN, tl_5 BOOLEAN)");
                        std::string entln_query =
                            "CREATE TABLE entln AS SELECT * FROM read_csv(" + paths_sql + ", "
                                                                                          "auto_detect=false, delim=',', new_line='\\n', skip=0, header=true, "
                                                                                          "columns={'type': 'BIGINT', 'timestamp': 'TIMESTAMP', 'latitude': 'DOUBLE', "
                                                                                          "'longitude': 'DOUBLE', 'peakcurrent': 'BIGINT', 'icheight': 'BIGINT', "
                                                                                          "'numbersensor': 'BIGINT', 'majoraxis': 'DOUBLE', 'minoraxis': 'DOUBLE', "
                                                                                          "'bearing': 'VARCHAR'})";
                        state.db.con.Query(entln_query);
                        state.db.con.Query("ALTER TABLE entln ADD COLUMN plot BOOLEAN DEFAULT true"); // plot column for spatial fitler
                        state.db.con.Query("INSERT INTO entln_tl SELECT plot, plot, plot, plot, plot FROM entln"); // initializing the entln timeline db
                        state.entln = true;
                        state.status.global = "Loaded " + std::to_string(selection.size()) + " ENTLN files";
                        state.EntlnFilter();
                    }
                    catch (const std::exception &e)
                    {
                        state.status.global = "Exception " + std::string(e.what()) + " happened when trying to load ENTLN files.";
                    }
                }
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Open ENTLN lightning data.");

            if (ImGui::MenuItem("State"))
                ;
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

                    state.status.global = "Saved image to " + save;
                }
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Save current view as a .PNG or .JPEG image.");

            if (ImGui::MenuItem("Data"))
            {
                auto save = pfd::save_file("Save parquet", "data.parquet", {"PARQUET", "*.parquet", "CSV", ".csv"}).result();
                if (!save.empty())
                {
                    bool is_parquet = !save.ends_with(".csv");
                    if (!save.ends_with(".parquet") && !save.ends_with(".csv"))
                        save += ".parquet";

                    auto result = is_parquet
                                      ? state.db.con.Query("COPY (SELECT * FROM lma WHERE plot = true) TO '" + save + "' (FORMAT PARQUET)")
                                      : state.db.con.Query("COPY (SELECT * FROM lma WHERE plot = true) TO '" + save + "' (FORMAT CSV)");

                    if (!result->HasError())
                        state.status.global = "Saved LMA data to " + save;

                    // saving entln data
                    if (state.graphics.count.entln > 0)
                    {
                        std::string base = is_parquet ? save.substr(0, save.size() - 8) : save.substr(0, save.size() - 4);
                        std::string entln_save = base + "_entln" + (is_parquet ? ".parquet" : ".csv");
                        auto entln_result = is_parquet
                                                ? state.db.con.Query("COPY (SELECT * FROM entln) TO '" + entln_save + "' (FORMAT PARQUET)")
                                                : state.db.con.Query("COPY (SELECT * FROM entln) TO '" + entln_save + "' (FORMAT CSV)");
                        if (!entln_result->HasError())
                            state.status.global += " and ENTLN data to " + entln_save;
                    }
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
                state.anime.Start();
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
                state.db.con.Query("DROP TABLE IF EXISTS lma");
                state.db.con.Query("DROP TABLE IF EXISTS ctg");
                state.graphics.ClearPlot();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Clear all current data and plots.");

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Flash"))
        {
            if (ImGui::MenuItem("XLMA"))
                ;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("XLMA dot-to-dot flash propagation algorithm.");

            if (ImGui::MenuItem("McCaul"))
                ;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("McCaul flash propagation algorithm.");

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Help"))
        {
            if (ImGui::MenuItem("Contact"))
                ;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Contact current maintainer of Aggie XLMA.");

            if (ImGui::MenuItem("About"))
                ;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("About Aggie XLMA.");

            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    // main viewport
    ImGuiViewport *viewport = ImGui::GetMainViewport();

    // status bar
    ImGuiWindowFlags stats_bar_flags =
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_MenuBar;
    float menu_bar_height = ImGui::GetFrameHeight();
    float left_width = viewport->WorkSize.x * 0.3f;
    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + viewport->Size.y - menu_bar_height));
    ImGui::SetNextWindowSize(ImVec2(viewport->Size.x * 0.3f, menu_bar_height));
    if (ImGui::Begin("##StatusBar1", nullptr, stats_bar_flags))
    {
        if (ImGui::BeginMenuBar())
        {
            ImGui::Text("%s", state.status.global.c_str());
            ImGui::EndMenuBar();
        }
        ImGui::End();
    }
    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + viewport->Size.x * 0.3f, viewport->Pos.y + viewport->Size.y - menu_bar_height));
    ImGui::SetNextWindowSize(ImVec2(viewport->Size.x * 0.7f, menu_bar_height));
    if (ImGui::Begin("##StatusBar2", nullptr, stats_bar_flags))
    {
        if (ImGui::BeginMenuBar())
        {
            ImGui::Text("%s", state.status.plot.c_str());
            ImGui::EndMenuBar();
        }
        ImGui::End();
    }

    // main window
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, viewport->WorkSize.y - menu_bar_height));

    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoTitleBar |
                                    ImGuiWindowFlags_NoCollapse |
                                    ImGuiWindowFlags_NoResize |
                                    ImGuiWindowFlags_NoMove |
                                    ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0)); // padding around the main window
    ImGui::Begin("Aggie XLMA", nullptr, window_flags);

    ImGui::BeginChild("##ToolsDummy", ImVec2(left_width, 0), ImGuiChildFlags_Borders);
    // padding for tools
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(state.style.padding, state.style.padding));
    ImGui::BeginChild("##Tools", ImVec2(0, 0), ImGuiChildFlags_Borders);
    // filters
    ImGui::Text("Filters");
    if (ImGui::InputFloat("Min. Stations", &state.filter.min_stations))
        state.VhfFilter();
    if (ImGui::InputFloat("Min. Altitude", &state.filter.min_alt))
        state.VhfFilter();
    if (ImGui::InputFloat("Max. Altitude", &state.filter.max_alt))
        state.VhfFilter();
    if (ImGui::InputFloat("Min. Chi", &state.filter.min_chi))
        state.VhfFilter();
    if (ImGui::InputFloat("Max. Chi", &state.filter.max_chi))
        state.VhfFilter();
    if (ImGui::InputFloat("Min. Power", &state.filter.min_power))
        state.VhfFilter();
    if (ImGui::InputFloat("Max. Power", &state.filter.max_power))
        state.VhfFilter();

    // layers:  maps, features, etc
    ImGui::Text("Layers");
    if (ImGui::Combo("Maps", &state.graphics.map.index, state.graphics.map.options.data(), state.graphics.map.options.size()))
        state.graphics.Render();
    if (ImGui::Checkbox("ENTLN IC&CC", &state.graphics.entln.ic))
        state.graphics.Render();
    if (ImGui::Checkbox("ENTLN CG", &state.graphics.entln.cg))
        state.graphics.Render();

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
        state.graphics.Render();
    if (ImGui::Combo("Color by", &state.graphics.colormap.by_index, state.graphics.colormap.by_options.data(), state.graphics.colormap.by_options.size()))
        state.Color();

    // animation
    ImGui::Text("Animation");
    ImGui::InputInt("Duration", &state.anime.duration);
    ImGui::Combo("Animate by", &state.anime.by_index, state.anime.options.data(), state.anime.options.size());

    // other
    ImGui::Text("Other");
    ImGui::BeginDisabled(state.tl.vhf.undo <= 0);
    if (ImGui::Button("Undo##vhf"))
    {
        int temp = state.tl.vhf.Undo();
        state.db.con.Query("UPDATE lma SET plot = v.tl_" + std::to_string(temp) + " FROM vhf_tl v WHERE lma.rowid = v.rowid");
        state.FetchVhf();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(state.tl.vhf.redo <= 0);
    if (ImGui::Button("Redo##vhf"))
    {
        int temp = state.tl.vhf.Redo();
        state.db.con.Query("UPDATE lma SET plot = v.tl_" + std::to_string(temp) + " FROM vhf_tl v WHERE lma.rowid = v.rowid");
        state.FetchVhf();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::Text("LMA");

    ImGui::BeginDisabled(state.tl.entln.undo <= 0);
    if (ImGui::Button("Undo##entln"))
    {
        int temp = state.tl.entln.Undo();
        state.db.con.Query("UPDATE entln SET plot = v.tl_" + std::to_string(temp) + " FROM entln_tl v WHERE entln.rowid = v.rowid");
        state.FetchEntln();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(state.tl.entln.redo <= 0);
    if (ImGui::Button("Redo##entln"))
    {
        int temp = state.tl.entln.Redo();
        state.db.con.Query("UPDATE entln SET plot = v.tl_" + std::to_string(temp) + " FROM entln_tl v WHERE entln.rowid = v.rowid");
        state.FetchEntln();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::Text("ENTLN/NLDN");

    if (ImGui::InputInt("VHF Size", &state.style.vhf_size, 1, 2))
    {
        state.style.vhf_size = std::clamp(state.style.vhf_size, 1, 10);
        state.graphics.shader.UpdateVHFShader(state.style.vhf_size);
        state.graphics.Render();
    }
    if (ImGui::Combo("Theme", &state.style.theme_index, state.style.themes.data(), state.style.themes.size()))
        state.style.SetTheme();
    if (ImGui::Checkbox("Music", &state.music.play))
        state.music.play ? ma_sound_start(&state.music.background) : ma_sound_stop(&state.music.background);

    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::EndChild();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0)); // spacing between tools and plots

    ImGui::SameLine();

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

    ImVec2 mouse = ImGui::GetMousePos();
    float scroll = ImGui::GetIO().MouseWheel;
    bool plots_hovered = false;
    ImU32 text_color = ImGui::ColorConvertFloat4ToU32(state.style.imgui_style->Colors[ImGuiCol_Text]);
    // lamba helper for cleaner appearence
    auto plot = [&](const char *id, Graphics::Plot *p, ImVec2 size, int y_ticks, int x_ticks)
    {
        const float *y_pos = y_ticks == 3 ? state.style.three_ticks.data() : state.style.five_ticks.data();
        const float *x_pos = x_ticks == 3 ? state.style.three_ticks.data() : state.style.five_ticks.data();
        ImGui::BeginChild(id, size, ImGuiChildFlags_Borders);
        {
            ImVec2 window_size = ImGui::GetWindowSize();
            float width = window_size.x;
            float height = window_size.y;
            // y axis
            ImGui::BeginChild((std::string(id) + "_yaxis").c_str(), ImVec2(state.style.axis_size, height - state.style.axis_size), false);
            {
                ImDrawList *draw_list = ImGui::GetWindowDrawList();
                ImVec2 wp = ImGui::GetWindowPos();
                for (int i = 0; i < y_ticks; i++)
                {
                    float y = wp.y + (1.0f - y_pos[i]) * p->rect.h - (ImGui::GetStyle().FramePadding.y + ImGui::GetStyle().ChildBorderSize);
                    float x = wp.x + state.style.axis_size;
                    draw_list->AddLine(ImVec2(x, y), ImVec2(x - state.style.tick_height, y), text_color, 1.0f);
                    const char *label = p->y_major_ticks[i].c_str();
                    ImVec2 text_size = ImGui::CalcTextSize(label);
                    AddVerticalText(draw_list, label, ImVec2(x - state.style.tick_height - text_size.y, y + text_size.x * 0.5f), text_color);
                }
            }
            ImGui::EndChild();
            ImGui::SameLine();
            ImVec2 pos = ImGui::GetCursorScreenPos();
            p->rect.x = pos.x;
            p->rect.y = pos.y;
            p->rect.w = width - state.style.axis_size;
            p->rect.h = height - state.style.axis_size;
            ImGui::Image((ImTextureID)p->texture, ImVec2(width - state.style.axis_size, height - state.style.axis_size), ImVec2(0, 0), ImVec2(1, 1));
            // x axis
            ImGui::BeginChild((std::string(id) + "_node").c_str(), ImVec2(state.style.axis_size, state.style.axis_size), false);
            ImGui::EndChild();
            ImGui::SameLine();
            ImGui::BeginChild((std::string(id) + "_xaxis").c_str(), ImVec2(width - state.style.axis_size, state.style.axis_size), false);
            {
                ImDrawList *draw_list = ImGui::GetWindowDrawList();
                ImVec2 wp = ImGui::GetWindowPos();
                for (int i = 0; i < x_ticks; i++)
                {
                    float x = wp.x + x_pos[i] * p->rect.w;
                    draw_list->AddLine(ImVec2(x, wp.y), ImVec2(x, wp.y + state.style.tick_height), text_color, 1.0f);
                    const char *label = p->x_major_ticks[i].c_str();
                    ImVec2 text_size = ImGui::CalcTextSize(label);
                    draw_list->AddText(ImVec2(x - text_size.x * 0.5f, wp.y + state.style.tick_height), text_color, label);
                }
            }
            ImGui::EndChild();
        }
        ImGui::EndChild();

        // plot specific interactivity
        bool hovered = mouse.x >= p->rect.x && mouse.y >= p->rect.y &&
                       mouse.x <= p->rect.x + p->rect.w && mouse.y <= p->rect.y + p->rect.h;
        plots_hovered |= hovered;
        // x y status
        if (hovered)
        {
            state.status.plot = std::format("x: {}, y: {}", p->X(mouse.x), p->Y(mouse.y));
            // copy current coordinates
            if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_C))
                glfwSetClipboardString(window, std::format("{}, {}", p->X(mouse.x), p->Y(mouse.y)).c_str());
            // non polygon interactions
            if (!state.polyselect.selecting)
            {
                // zoom
                if (scroll != 0.0f)
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
                if (ImGui::GetIO().MouseDown[1])
                {
                    ImVec2 delta = ImGui::GetIO().MouseDelta;
                    // 0.5 is sensitivity, feel free to tune it
                    p->uv_x -= (delta.x / p->rect.w) * p->zoom * 0.5f;
                    p->uv_y += (delta.y / p->rect.h) * p->zoom * 0.5f;
                    p->uv_x = std::clamp(p->uv_x, 0.0f, 1.0f - p->zoom);
                    p->uv_y = std::clamp(p->uv_y, 0.0f, 1.0f - p->zoom);
                    state.graphics.UpdateTickLabels();
                    state.graphics.Render(p);
                }
                // left click (start of polygon)
                if (ImGui::IsMouseClicked(0))
                {
                    state.polyselect.Add(mouse, p);
                }
            }
            // polygon interactions
            else if (state.polyselect.plot == p && hovered)
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

                // right click end of polygon
                if (ImGui::IsMouseClicked(1))
                    state.polyselect.Add(mouse, p, true);
                // left click (continuation of polygon)
                else if (ImGui::IsMouseClicked(0))
                    state.polyselect.Add(mouse, p);
            }
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
        state.anime.Start();
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
    // ctrl + z: undo
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z))
    {
        // polygon undo
        if (state.polyselect.selecting)
            state.polyselect.Remove();
    }

    // music
    if (state.music.play && ma_sound_at_end(&state.music.background))
    {
        // music ended
        state.music.Tick();
    }

    // status reset
    if (!plots_hovered)
        state.status.plot = state.graphics.count.vhf > 0
                                ? std::format("Plotted {} VHF, {} ENTLN ({} CG) sources", state.graphics.count.vhf, state.graphics.count.entln, state.graphics.count.entln_cg)
                                : "Nothing to plot.";

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
    // some impt stuff
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

    // icon
    GLFWimage icon;
    icon.pixels = stbi_load("bin/xlma.png", &icon.width, &icon.height, nullptr, 4);
    if (icon.pixels)
    {
        glfwSetWindowIcon(window, 1, &icon);
        stbi_image_free(icon.pixels);
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK)
    {
        std::cerr << "Failed to initialize GLEW\n";
        return -1;
    }

    // duckdb stuff
    state.db.con.Query("INSTALL spatial");
    state.db.con.Query("LOAD spatial"); //  loading spatial

    // idle status
    static const std::array<const char *, 10> idle_strings = {
        "Let's do this! :)",
        "All systems go!",
        "Ready when you are!",
        "Feeling electric today!",
        "Looking good!",
        "Clear skies ahead!",
        "Charged up and ready!",
        "Every storm tells a story.",
        "The atmosphere is waiting.",
        "Good things are brewing!",
    };
    srand(time(nullptr));
    state.status.idle = idle_strings[rand() % idle_strings.size()];
    state.status.global = state.status.idle;

    // audio engine
    ma_engine_init(NULL, &state.music.engine);
    state.music.current = rand() % state.music.files.size();
    ma_sound_init_from_file(&state.music.engine, state.music.files[state.music.current], 0, NULL, NULL, &state.music.background);

    // imgui stuff
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    // setting theme
    ImGuiStyle &style = ImGui::GetStyle();
    state.style.SetStyle(style);
    io.FontGlobalScale = state.style.font_scale;
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);
    // manual initial renderui
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    state.style.padding = ImGui::GetFontSize() * 0.5f;
    state.style.axis_size = ImGui::GetFontSize() * 1.8f;
    state.style.tick_height = ImGui::GetFontSize() * 0.4f;
    Tick();
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(window);
    state.graphics.Initialize();
    state.graphics.ClearPlot();

    // main ui loop
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        if (state.anime.animating) // animation handler has to work per frame of UI renderer
            state.Frame();
        Tick();

        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);

        if (state.anime.saving) // saving frame for gif
            state.SaveGIFFrame();
    }

    // closing app
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}