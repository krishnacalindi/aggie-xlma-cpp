#include "state.h"
#include <format>
#include <gif.h>

// helper for color by
std::string State::ColorBy()
{
    switch (graphics.colormap.by_index)
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

void State::Filter()
{
    // updating plot column
    std::string filter_query = "UPDATE lma SET plot = (number_stations >= " + std::to_string(filter.min_stations) +
                               " AND alt >= " + std::to_string(filter.min_alt) +
                               " AND alt <= " + std::to_string(filter.max_alt) +
                               " AND chi >= " + std::to_string(filter.min_chi) +
                               " AND chi <= " + std::to_string(filter.max_chi) +
                               " AND pdb >= " + std::to_string(filter.min_power) +
                               " AND pdb <= " + std::to_string(filter.max_power) + ");";
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

    graphics.ProcessResult(data_result, hist_result);
}

void State::EntlnFilter()
{
    std::string entln_query =
        "WITH day_start AS ("
        "SELECT DATE_TRUNC('day', MIN(datetime)) AS day FROM lma WHERE plot = true"
        ") "
        "SELECT "
        "CAST(EPOCH_NS(timestamp::TIMESTAMP) - EPOCH_NS(day) AS FLOAT) AS time, "
        "longitude::FLOAT AS lon, "
        "latitude::FLOAT AS lat, "
        "CASE WHEN type = 1 THEN icheight::FLOAT / 1000.0 ELSE 1.0 END AS alt, "
        "CASE WHEN type = 40 THEN 0.0 ELSE type::FLOAT END AS l_type, "
        "peakcurrent::FLOAT AS charge, "
        "SUM(CASE WHEN type = 40 OR type = 0 THEN 1 ELSE 0 END) OVER() AS cg_count "
        "FROM entln CROSS JOIN day_start "
        "ORDER BY l_type";
    auto entln_result = con.Query(entln_query);
    graphics.ProcessEntlnResult(entln_result);
}

void State::Color()
{
    timer.Start();
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
    graphics.ProcessColor(result);
}

void State::Flip()
{
    timer.Start();
    style.mode = style.mode == Style::Mode::Dark ? Style::Mode::Light : Style::Mode::Dark;
    style.color = style.mode == Style::Mode::Dark ? Style::Color{255, 0.0f, 1.0f} : Style::Color{0, 1.0f, 0.0f};
    graphics.shader.UpdateLineShader(style.color.diff);
    graphics.Render();
}

void State::StartSaveGIF(const std::string &path)
{
    anime.gif_path = path;
    anime.gif = new GifWriter();
    GifBegin((GifWriter *)anime.gif, path.c_str(), graphics.rect.w, graphics.rect.h, 10);
    anime.saving = true;
}

void State::Frame()
{
    if (graphics.count.vhf <= 1)
        return;
    float elapsed = (float)anime.Elapsed() / anime.duration_ms;
    if (elapsed <= 1)
    {
        if (anime.by_index == 0)
            anime.sources = (size_t)(elapsed * graphics.count.vhf);
        else
        {
            float threshold = graphics.time_alt.x_min + elapsed * (graphics.time_alt.x_max - graphics.time_alt.x_min);
            glBindBuffer(GL_ARRAY_BUFFER, graphics.time_alt.vhf.vbo);
            float *ptr = (float *)glMapBuffer(GL_ARRAY_BUFFER, GL_READ_ONLY);
            size_t i = 0;
            while (i < graphics.count.vhf && ptr[i * 5] <= threshold)
                i++;
            glUnmapBuffer(GL_ARRAY_BUFFER);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            anime.sources = i;
        }
        graphics.Render();
    }
    else
    {
        anime.End();
        graphics.Render();
    }
}

void State::SaveGIFFrame()
{
    int fb_w, fb_h;
    glfwGetFramebufferSize(window, &fb_w, &fb_h); // getting entire imgui wino
    std::vector<uint8_t> pixels(graphics.rect.w * graphics.rect.h * 4);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glReadBuffer(GL_BACK);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(graphics.rect.x, fb_h - (graphics.rect.y + graphics.rect.h), graphics.rect.w, graphics.rect.h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    for (int y = 0; y < graphics.rect.h / 2; ++y)
        for (int x = 0; x < graphics.rect.w * 4; ++x)
            std::swap(pixels[y * graphics.rect.w * 4 + x], pixels[(graphics.rect.h - 1 - y) * graphics.rect.w * 4 + x]);
    GifWriteFrame((GifWriter *)anime.gif, pixels.data(), graphics.rect.w, graphics.rect.h, 10);
    if (!anime.animating)
    {
        status = "Saved animation to " + anime.gif_path;
        anime.saving = false;
        GifEnd((GifWriter *)anime.gif);
        delete (GifWriter *)anime.gif;
        anime.gif = nullptr;
    }
}
