#include "state.h"
#include <format>
#include <zlib.h>
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
    ProcessResult(data_result, hist_result);
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
    ProcessColor(result);
}

auto link_shader = [](GLuint &program, const char *vert_src, const char *frag_src)
{
    glDeleteProgram(program);
    auto compile = [](GLenum type, const char *src) -> GLuint
    {
        GLuint shader = glCreateShader(type);
        glShaderSource(shader, 1, &src, nullptr);
        glCompileShader(shader);
        return shader;
    };
    GLuint vert = compile(GL_VERTEX_SHADER, vert_src);
    GLuint frag = compile(GL_FRAGMENT_SHADER, frag_src);
    program = glCreateProgram();
    glAttachShader(program, vert);
    glAttachShader(program, frag);
    glLinkProgram(program);
    glDeleteShader(vert);
    glDeleteShader(frag);
};

void State::SetVHFShader()
{
    std::string vhf_vert = std::string(R"(
#version 330 core
layout(location = 0) in float x_pos;
layout(location = 1) in float y_pos;
layout(location = 2) in float value;
uniform mat4 projection;
out float vValue;
void main() {
    gl_Position = projection * vec4(x_pos, y_pos, 0.0, 1.0);
    gl_PointSize = )") + std::to_string(style.size.vhf) +
                           R"(.0;
    vValue = value;
}
)";

    const char *vhf_frag = R"(
#version 330 core
in float vValue;
out vec4 FragColor;
uniform sampler2D colormaps;
uniform int cmap_index;
void main() {
    if (length(gl_PointCoord - vec2(0.5)) > 0.5)
        discard;
    float y = (float(cmap_index) + 0.5) / 5.0;
    FragColor = texture(colormaps, vec2(vValue, y));
    FragColor.a = 1.0;
}
)";

    // vhf source shader
    link_shader(graphics.shader.vhf, vhf_vert.c_str(), vhf_frag);
    Render(); // in the beginnning this is harmless as sources.graphics is 0
}

void State::SetStationShader()
{
    // stations shader
    const char *sta_vert = R"(
#version 330 core
layout(location = 0) in float x_pos;
layout(location = 1) in float y_pos;
uniform mat4 projection;
void main() {
    gl_Position = projection * vec4(x_pos, y_pos, 0.0, 1.0);
    gl_PointSize = 5.0;
}
)";

    const char *sta_frag = R"(
#version 330 core
out vec4 FragColor;
void main() {
    FragColor = vec4(1.0, 0.0, 0.0, 1.0);
}
)";

    link_shader(graphics.shader.stations, sta_vert, sta_frag);
}

void State::SetLineShader()
{
    const char *line_vert = R"(
#version 330 core
layout(location = 0) in float x_pos;
layout(location = 1) in float y_pos;
uniform mat4 projection;
void main() {
    gl_Position = projection * vec4(x_pos, y_pos, 0.0, 1.0);
}
)";

    std::string line_frag = std::string(R"(
    #version 330 core
    out vec4 FragColor;
    void main() {
        FragColor = vec4()") +
                            std::to_string(style.color.diff) + R"();
    }
)";

    link_shader(graphics.shader.line, line_vert, line_frag.c_str());
    Render();
}

void State::Flip()
{
    timer.Start();
    style.mode = style.mode == Style::Mode::Dark ? Style::Mode::Light : Style::Mode::Dark;
    style.color = style.mode == Style::Mode::Dark ? Style::Color{255, 0.0f, 1.0f} : Style::Color{0, 1.0f, 0.0f};
    SetLineShader();
}

void State::InitializeGraphics()
{
    // initializing shaders
    SetVHFShader();
    SetStationShader();
    SetLineShader();

    // setting up colormaps
    float colormap_data[5][256][3];
    FILE *f = fopen("bin/colormap.bin", "rb");
    fread(colormap_data, sizeof(float), 5 * 256 * 3, f);
    fclose(f);
    glGenTextures(1, &graphics.colormap.texture);
    glBindTexture(GL_TEXTURE_2D, graphics.colormap.texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, 256, 5, 0, GL_RGB, GL_FLOAT, colormap_data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // setting up plot textures
    for (Plot *p : plots)
    {
        glGenTextures(1, &p->texture);
        glBindTexture(GL_TEXTURE_2D, p->texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, p->rect.w, p->rect.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glGenFramebuffers(1, &p->fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, p->fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, p->texture, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    // setting up vbos
    glGenBuffers(1, &graphics.vbo.vhf);
    glGenBuffers(1, &graphics.vbo.hist);
    glGenBuffers(1, &graphics.vbo.stations);
    glGenBuffers(1, &graphics.vbo.map);

    // setting up vaos
    auto setup_vao = [](GLuint &vao, GLuint vbo, int x_off = 0, int y_off = 0, int stride = 2, bool color = false)
    {
        glGenVertexArrays(1, &vao);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glVertexAttribPointer(0, 1, GL_FLOAT, GL_FALSE, stride * sizeof(float), (void *)(x_off * sizeof(float)));
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, stride * sizeof(float), (void *)(y_off * sizeof(float)));
        glEnableVertexAttribArray(1);
        if (color)
        {
            glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, stride * sizeof(float), (void *)(4 * sizeof(float)));
            glEnableVertexAttribArray(2);
        }
        glBindVertexArray(0);
    };
    setup_vao(time_alt.vao, graphics.vbo.vhf, 0, 3, 5, true);
    setup_vao(lon_alt.vao, graphics.vbo.vhf, 1, 3, 5, true);
    setup_vao(lon_lat.vao, graphics.vbo.vhf, 1, 2, 5, true);
    setup_vao(alt_lat.vao, graphics.vbo.vhf, 3, 2, 5, true);
    setup_vao(alt_hist.vao, graphics.vbo.hist, 0, 1);
    setup_vao(graphics.vao.stations, graphics.vbo.stations, 0, 1);

    // setting up map data
    setup_vao(graphics.vao.map, graphics.vbo.map, 0, 1);
    glBindBuffer(GL_ARRAY_BUFFER, graphics.vbo.map);
    glBufferData(GL_ARRAY_BUFFER, 2 * 1771152 * sizeof(float), nullptr, GL_STATIC_DRAW);
    void *map_ptr = glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
    f = fopen("bin/map.bin", "rb");
    fread(map_ptr, sizeof(float), 2 * 1771152, f);
    fclose(f);
    glUnmapBuffer(GL_ARRAY_BUFFER);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    graphics.initialized = true;
}

void State::ProcessResult(duckdb::unique_ptr<duckdb::MaterializedQueryResult> &data_res, duckdb::unique_ptr<duckdb::MaterializedQueryResult> &hist_res)
{
    // initializing opengl stuff
    if (!graphics.initialized)
        InitializeGraphics();

    graphics.count.vhf = data_res->RowCount();

    if (graphics.count.vhf <= 1) // to prevent axis collapse from min and max being equal
    {
        status = "Not enough data to plot with current selection";
        return;
    }
    else
    {
        // reading station info
        ReadStations(graphics.filepath);

        // axis updates
        time_alt.x_min = data_res->GetValue<float>(5, 0);
        time_alt.x_max = data_res->GetValue<float>(6, 0);
        time_alt.y_min = lon_alt.y_min = alt_hist.y_min = alt_lat.x_min = data_res->GetValue<float>(11, 0);
        time_alt.y_max = lon_alt.y_max = alt_hist.y_max = alt_lat.x_max = data_res->GetValue<float>(12, 0);
        lon_alt.x_min = lon_lat.x_min = data_res->GetValue<float>(7, 0);
        lon_alt.x_max = lon_lat.x_max = data_res->GetValue<float>(8, 0);
        alt_lat.y_min = lon_lat.y_min = data_res->GetValue<float>(9, 0);
        alt_lat.y_max = lon_lat.y_max = data_res->GetValue<float>(10, 0);
        alt_hist.x_min = hist_res->GetValue<float>(2, 0);
        alt_hist.x_max = hist_res->GetValue<float>(3, 0);
        for (int i = 0; i < 5; i++)
        {
            float t = 0.1f + i * 0.2f;
            auto ns = static_cast<int64_t>(time_alt.x_min + t * (time_alt.x_max - time_alt.x_min));
            int64_t total_sec = ns / 1000000000LL;
            int h = total_sec / 3600;
            int m = (total_sec % 3600) / 60;
            int s = total_sec % 60;
            int ms = (ns % 1000000000LL) / 1000000LL;
            time_alt.x_major_ticks[i] = std::format("{:02}:{:02}:{:02}.{:03}", h, m, s, ms);
            lon_alt.x_major_ticks[i] = std::format("{:.4f}", lon_alt.x_min + t * (lon_alt.x_max - lon_alt.x_min));
            lon_lat.x_major_ticks[i] = std::format("{:.4f}", lon_lat.x_min + t * (lon_lat.x_max - lon_lat.x_min));
            lon_lat.y_major_ticks[4 - i] = std::format("{:.4f}", lon_lat.y_min + t * (lon_lat.y_max - lon_lat.y_min));
            alt_lat.y_major_ticks[4 - i] = std::format("{:.4f}", alt_lat.y_min + t * (alt_lat.y_max - alt_lat.y_min));
        }
        for (int i = 0; i < 3; i++)
        {
            float t = 0.2f + i * 0.3f;
            time_alt.y_major_ticks[2 - i] = std::format("{:.1f}", time_alt.y_min + t * (time_alt.y_max - time_alt.y_min));
            alt_hist.x_major_ticks[i] = std::format("{:.1f}", alt_hist.x_min + t * (alt_hist.x_max - alt_hist.x_min));
            alt_hist.y_major_ticks[2 - i] = std::format("{:.1f}", alt_hist.y_min + t * (alt_hist.y_max - alt_hist.y_min));
            lon_alt.y_major_ticks[2 - i] = std::format("{:.1f}", lon_alt.y_min + t * (lon_alt.y_max - lon_alt.y_min));
            alt_lat.x_major_ticks[i] = std::format("{:.1f}", alt_lat.x_min + t * (alt_lat.x_max - alt_lat.x_min));
        }

        // vhf stuff
        glBindBuffer(GL_ARRAY_BUFFER, graphics.vbo.vhf);
        glBufferData(GL_ARRAY_BUFFER, graphics.count.vhf * 5 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
        float *vhf_ptr = (float *)glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
        size_t chunk_i = 0; // global index
        while (auto chunk = data_res->Fetch())
        {
            auto &time_vec = chunk->data[0];
            auto &lon_vec = chunk->data[1];
            auto &lat_vec = chunk->data[2];
            auto &alt_vec = chunk->data[3];
            auto &color_vec = chunk->data[4];
            float *time_data = duckdb::FlatVector::GetData<float>(time_vec);
            float *lon_data = duckdb::FlatVector::GetData<float>(lon_vec);
            float *lat_data = duckdb::FlatVector::GetData<float>(lat_vec);
            float *alt_data = duckdb::FlatVector::GetData<float>(alt_vec);
            float *color_data = duckdb::FlatVector::GetData<float>(color_vec);
            size_t row_count = chunk->size();
            for (size_t i = 0; i < row_count; i++)
            {
                vhf_ptr[(chunk_i + i) * 5 + 0] = time_data[i];
                vhf_ptr[(chunk_i + i) * 5 + 1] = lon_data[i];
                vhf_ptr[(chunk_i + i) * 5 + 2] = lat_data[i];
                vhf_ptr[(chunk_i + i) * 5 + 3] = alt_data[i];
                vhf_ptr[(chunk_i + i) * 5 + 4] = color_data[i];
            }
            chunk_i += row_count;
        }
        glUnmapBuffer(GL_ARRAY_BUFFER);
        glBindBuffer(GL_ARRAY_BUFFER, 0);

        // histogram stuff
        glBindBuffer(GL_ARRAY_BUFFER, graphics.vbo.hist);
        glBufferData(GL_ARRAY_BUFFER, 200 * 2 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
        float *hist_ptr = (float *)glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
        chunk_i = 0; // global index
        while (auto chunk = hist_res->Fetch())
        {
            auto &y_vec = chunk->data[0];
            auto &x_vec = chunk->data[1];
            float *y_data = duckdb::FlatVector::GetData<float>(y_vec);
            float *x_data = duckdb::FlatVector::GetData<float>(x_vec);
            size_t row_count = chunk->size();
            for (size_t i = 0; i < row_count; i++)
            {
                hist_ptr[(chunk_i + i) * 2 + 0] = x_data[i];
                hist_ptr[(chunk_i + i) * 2 + 1] = y_data[i];
            }
            chunk_i += row_count;
        }
        glUnmapBuffer(GL_ARRAY_BUFFER);
        glBindBuffer(GL_ARRAY_BUFFER, 0);

        // rendering the points
        Render();
    }
}

void State::ProcessColor(duckdb::unique_ptr<duckdb::MaterializedQueryResult> &res)
{
    if (res->RowCount() != graphics.count.vhf || graphics.count.vhf <= 1)
    {
        status = std::string("Color by ") + graphics.colormap.by_options[graphics.colormap.by_index] + " failed";
        return;
    }
    glBindBuffer(GL_ARRAY_BUFFER, graphics.vbo.vhf);
    float *vhf_ptr = (float *)glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
    size_t chunk_i = 0;
    while (auto chunk = res->Fetch())
    {
        float *color_data = duckdb::FlatVector::GetData<float>(chunk->data[0]);
        size_t row_count = chunk->size();
        for (size_t i = 0; i < row_count; i++)
            vhf_ptr[(chunk_i + i) * 5 + 4] = color_data[i];
        chunk_i += row_count;
    }
    glUnmapBuffer(GL_ARRAY_BUFFER);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    Render();
}

void State::ReadStations(const std::string &filepath)
{
    bool is_gz = false;
    {
        std::ifstream probe(filepath, std::ios::binary);
        if (probe)
        {
            unsigned char b[2] = {};
            probe.read((char *)b, 2);
            is_gz = (b[0] == 0x1F && b[1] == 0x8B);
        }
    }
    std::vector<float> sta_coords;
    auto parse_line = [&](const std::string &line)
    {
        if (line.rfind("Sta_info:", 0) != 0)
            return;
        std::vector<std::string> tokens;
        std::istringstream ss(line);
        std::string token;
        while (ss >> token)
            tokens.push_back(token);
        if (tokens.size() < 6)
            return;
        sta_coords.push_back(std::stof(tokens[tokens.size() - 5])); // lon
        sta_coords.push_back(std::stof(tokens[tokens.size() - 6])); // lat
    };
    if (is_gz)
    {
        gzFile f = gzopen(filepath.c_str(), "rb");
        if (!f)
            return;
        char buf[512];
        while (gzgets(f, buf, sizeof(buf)))
            parse_line(std::string(buf));
        gzclose(f);
    }
    else
    {
        std::ifstream f(filepath);
        if (!f)
            return;
        std::string line;
        while (std::getline(f, line))
            parse_line(line);
    }
    graphics.count.stations = sta_coords.size() / 2;
    glBindBuffer(GL_ARRAY_BUFFER, graphics.vbo.stations);
    glBufferData(GL_ARRAY_BUFFER, sta_coords.size() * sizeof(float), sta_coords.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void State::Render()
{
    if (graphics.count.vhf <= 1)
        return;

    for (Plot *p : plots)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, p->fbo);
        glViewport(0, 0, p->rect.w, p->rect.h);
        glClearColor(style.color.same, style.color.same, style.color.same, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glm::mat4 proj = glm::ortho(p->x_min, p->x_max, p->y_max, p->y_min, -1.0f, 1.0f);
        if (p == &alt_hist) // histogram
        {
            glBindVertexArray(p->vao);
            glUseProgram(graphics.shader.line);
            glUniformMatrix4fv(glGetUniformLocation(graphics.shader.line, "projection"), 1, GL_FALSE, glm::value_ptr(proj));
            glDrawArrays(GL_LINE_STRIP, 0, 200);
            glBindVertexArray(0);
        }
        else
        {
            if (p == &lon_lat) // handling maps, stations, features, etc.
            {
                if (graphics.map.index != 0)
                {
                    glBindVertexArray(graphics.vao.map);
                    glUseProgram(graphics.shader.line);
                    glUniformMatrix4fv(glGetUniformLocation(graphics.shader.line, "projection"), 1, GL_FALSE, glm::value_ptr(proj));
                    glDrawArrays(GL_LINES, 0, graphics.map.sizes[graphics.map.index]);
                    glBindVertexArray(0);
                }
                if (graphics.count.stations > 0)
                {
                    glBindVertexArray(graphics.vao.stations);
                    glUseProgram(graphics.shader.stations);
                    glUniformMatrix4fv(glGetUniformLocation(graphics.shader.stations, "projection"), 1, GL_FALSE, glm::value_ptr(proj));
                    glEnable(GL_PROGRAM_POINT_SIZE);
                    glDrawArrays(GL_POINTS, 0, graphics.count.stations);
                    glBindVertexArray(0);
                }
            }
            // vhf scatter plot
            glBindVertexArray(p->vao);
            glUseProgram(graphics.shader.vhf);
            glEnable(GL_PROGRAM_POINT_SIZE);
            glUniformMatrix4fv(glGetUniformLocation(graphics.shader.vhf, "projection"), 1, GL_FALSE, glm::value_ptr(proj));
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, graphics.colormap.texture);
            glUniform1i(glGetUniformLocation(graphics.shader.vhf, "colormaps"), 0);
            glUniform1i(glGetUniformLocation(graphics.shader.vhf, "cmap_index"), graphics.colormap.index);
            glDrawArrays(GL_POINTS, 0, anime.animating ? anime.sources : graphics.count.vhf);
            glBindVertexArray(0);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    status = "Plotted " + std::to_string(graphics.count.vhf) + " sources in " + std::to_string(timer.End()) + "ms";
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
            float threshold = time_alt.x_min + elapsed * (time_alt.x_max - time_alt.x_min);
            glBindBuffer(GL_ARRAY_BUFFER, graphics.vbo.vhf);
            float *ptr = (float *)glMapBuffer(GL_ARRAY_BUFFER, GL_READ_ONLY);
            size_t i = 0;
            while (i < graphics.count.vhf && ptr[i * 5] <= threshold)
                i++;
            glUnmapBuffer(GL_ARRAY_BUFFER);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            anime.sources = i;
        }
        Render();
    }
    else
    {
        anime.End();
        Render();
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

void State::ClearPlot()
{
    for (Plot *p : plots)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, p->fbo);
        glViewport(0, 0, p->rect.w, p->rect.h);
        glClearColor(style.color.same, style.color.same, style.color.same, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
}