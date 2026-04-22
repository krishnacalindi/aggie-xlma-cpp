#include <format>
#include <zlib.h>
#include <graphics.h>
#include <state.h>

extern State state;

float Graphics::Plot::X(float mouse_x)
{
    return x_min + (uv_x + (mouse_x - rect.x) / rect.w * zoom) * (x_max - x_min);
}

float Graphics::Plot::Y(float mouse_y)
{
    return y_min + (uv_y + (1.0f - (mouse_y - rect.y) / rect.h) * zoom) * (y_max - y_min);
}

void Graphics::_setup_vao(GLuint &vao, GLuint vbo, int stride, std::initializer_list<int> offsets)
{
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    for (int i = 0; int offset : offsets)
    {
        glVertexAttribPointer(i, 1, GL_FLOAT, GL_FALSE, stride * sizeof(float), reinterpret_cast<void*>(offset * sizeof(float)));
        glEnableVertexAttribArray(i);
        i++;
    }
    glBindVertexArray(0);
}

void Graphics::Initialize()
{
    // initialzing shaders
    shader.Initialize();

    // setting up colormaps
    float colormap_data[5][256][3];
    FILE *f = fopen("bin/colormap.bin", "rb");
    fread(colormap_data, sizeof(float), 5 * 256 * 3, f);
    fclose(f);
    glGenTextures(1, &colormap.texture);
    glBindTexture(GL_TEXTURE_2D, colormap.texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, 256, 5, 0, GL_RGB, GL_FLOAT, colormap_data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // generating vbos
    glGenBuffers(1, &time_alt.vhf.vbo);
    glGenBuffers(1, &time_alt.entln.vbo);
    glGenBuffers(1, &alt_hist.vhf.vbo);
    glGenBuffers(1, &stations.vbo);
    glGenBuffers(1, &map.data.vbo);

    // setting up fbo and texture and using only one vbo for vhf and entln
    GLuint hist_vbo = alt_hist.vhf.vbo;
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

        p->vhf.vbo = time_alt.vhf.vbo;
        p->entln.vbo = time_alt.entln.vbo;
    }
    alt_hist.vhf.vbo = hist_vbo;

    // setting up maps
    _setup_vao(map.data.vao, map.data.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, map.data.vbo);
    glBufferData(GL_ARRAY_BUFFER, 2 * 1771152 * sizeof(float), nullptr, GL_STATIC_DRAW);
    void *map_ptr = glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
    f = fopen("bin/map.bin", "rb");
    fread(map_ptr, sizeof(float), 2 * 1771152, f);
    fclose(f);
    glUnmapBuffer(GL_ARRAY_BUFFER);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    // setting up vaos
    _setup_vao(time_alt.vhf.vao, time_alt.vhf.vbo, 5, {0, 3, 4});        // time, alt, color
    _setup_vao(lon_alt.vhf.vao, lon_alt.vhf.vbo, 5, {1, 3, 4});          // lon, alt, color
    _setup_vao(lon_lat.vhf.vao, lon_lat.vhf.vbo, 5, {1, 2, 4});          // lon, lat, color
    _setup_vao(alt_lat.vhf.vao, alt_lat.vhf.vbo, 5, {3, 2, 4});          // alt, lat, color
    _setup_vao(alt_hist.vhf.vao, alt_hist.vhf.vbo, 2, {0, 1});           // count, alt
    _setup_vao(stations.vao, stations.vbo);                              // lon, lat
    _setup_vao(time_alt.entln.vao, time_alt.entln.vbo, 6, {0, 3, 4, 5}); // time, alt, type, charge
    _setup_vao(lon_alt.entln.vao, lon_alt.entln.vbo, 6, {1, 3, 4, 5});   // lon, alt, type, charge
    _setup_vao(lon_lat.entln.vao, lon_lat.entln.vbo, 6, {1, 2, 4, 5});   // lon, lat, type, charge
    _setup_vao(alt_lat.entln.vao, alt_lat.entln.vbo, 6, {3, 2, 4, 5});   // alt, lat, type, charge
}

void Graphics::ProcessResult(QueryResult &data_res, QueryResult &hist_res)
{
    count.vhf = data_res->RowCount();

    if (count.vhf <= 1) // to prevent axis collapse from min and max being equal
    {
        state.status.plot = "Not enough data to plot with current selections.";
        return;
    }
    // min max for axes labels
    auto chunk = data_res->Fetch();
    float *min_time = duckdb::FlatVector::GetData<float>(chunk->data[5]);
    float *max_time = duckdb::FlatVector::GetData<float>(chunk->data[6]);
    float *min_lon = duckdb::FlatVector::GetData<float>(chunk->data[7]);
    float *max_lon = duckdb::FlatVector::GetData<float>(chunk->data[8]);
    float *min_lat = duckdb::FlatVector::GetData<float>(chunk->data[9]);
    float *max_lat = duckdb::FlatVector::GetData<float>(chunk->data[10]);
    float *min_alt = duckdb::FlatVector::GetData<float>(chunk->data[11]);
    float *max_alt = duckdb::FlatVector::GetData<float>(chunk->data[12]);
    time_alt.x_min = min_time[0];
    time_alt.x_max = max_time[0];
    time_alt.y_min = lon_alt.y_min = alt_hist.y_min = alt_lat.x_min = min_alt[0];
    time_alt.y_max = lon_alt.y_max = alt_hist.y_max = alt_lat.x_max = max_alt[0];
    lon_alt.x_min = lon_lat.x_min = min_lon[0];
    lon_alt.x_max = lon_lat.x_max = max_lon[0];
    alt_lat.y_min = lon_lat.y_min = min_lat[0];
    alt_lat.y_max = lon_lat.y_max = max_lat[0];
    alt_hist.x_min = hist_res->GetValue<float>(2, 0);
    alt_hist.x_max = hist_res->GetValue<float>(3, 0);
    UpdateTickLabels();

    // vhf stuff
    glBindBuffer(GL_ARRAY_BUFFER, time_alt.vhf.vbo);
    glBufferData(GL_ARRAY_BUFFER, count.vhf * 5 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    float* vhf_ptr = reinterpret_cast<float*>(glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY));
    size_t chunk_i = 0; // global index
    while (chunk)
    {
        float *time_data = duckdb::FlatVector::GetData<float>(chunk->data[0]);
        float *lon_data = duckdb::FlatVector::GetData<float>(chunk->data[1]);
        float *lat_data = duckdb::FlatVector::GetData<float>(chunk->data[2]);
        float *alt_data = duckdb::FlatVector::GetData<float>(chunk->data[3]);
        float *color_data = duckdb::FlatVector::GetData<float>(chunk->data[4]);
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
        chunk = data_res->Fetch();
    }
    glUnmapBuffer(GL_ARRAY_BUFFER);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    // histogram stuff
    glBindBuffer(GL_ARRAY_BUFFER, alt_hist.vhf.vbo);
    glBufferData(GL_ARRAY_BUFFER, 200 * 2 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    float* hist_ptr = reinterpret_cast<float*>(glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY));
    chunk_i = 0; // global index
    while (chunk = hist_res->Fetch())
    {
        float *y_data = duckdb::FlatVector::GetData<float>(chunk->data[0]);
        float *x_data = duckdb::FlatVector::GetData<float>(chunk->data[1]);
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

    // color
    state.Color();
}

void Graphics::ProcessEntlnResult(QueryResult &entln_res)
{
    count.entln = entln_res->RowCount();
    if (count.entln > 0)
    {
        count.entln_cg = entln_res->GetValue<int>(6, 0);
        glBindBuffer(GL_ARRAY_BUFFER, time_alt.entln.vbo);
        glBufferData(GL_ARRAY_BUFFER, count.entln * 6 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
        float* entln_ptr = reinterpret_cast<float*>(glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY));
        int chunk_i = 0;
        while (auto chunk = entln_res->Fetch())
        {
            float *time_data = duckdb::FlatVector::GetData<float>(chunk->data[0]);
            float *lon_data = duckdb::FlatVector::GetData<float>(chunk->data[1]);
            float *lat_data = duckdb::FlatVector::GetData<float>(chunk->data[2]);
            float *alt_data = duckdb::FlatVector::GetData<float>(chunk->data[3]);
            float *type_data = duckdb::FlatVector::GetData<float>(chunk->data[4]);
            float *charge_data = duckdb::FlatVector::GetData<float>(chunk->data[5]);
            size_t row_count = chunk->size();
            for (size_t i = 0; i < row_count; i++)
            {
                entln_ptr[(chunk_i + i) * 6 + 0] = time_data[i];
                entln_ptr[(chunk_i + i) * 6 + 1] = lon_data[i];
                entln_ptr[(chunk_i + i) * 6 + 2] = lat_data[i];
                entln_ptr[(chunk_i + i) * 6 + 3] = alt_data[i];
                entln_ptr[(chunk_i + i) * 6 + 4] = type_data[i];
                entln_ptr[(chunk_i + i) * 6 + 5] = charge_data[i];
            }
            chunk_i += row_count;
        }
        glUnmapBuffer(GL_ARRAY_BUFFER);
        glBindBuffer(GL_ARRAY_BUFFER, 0);

        Render();
    }
}

void Graphics::ProcessColor(QueryResult &res)
{
    if (res->RowCount() == count.vhf && count.vhf > 1)
    {
        glBindBuffer(GL_ARRAY_BUFFER, time_alt.vhf.vbo);
        float* vhf_ptr = reinterpret_cast<float*>(glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY));
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
}

void Graphics::Render(Plot *one)
{
    if (count.vhf <= 1)
    {
        state.status.plot = "Not enough data to plot with current selections.";
        return;
    }

    for (Plot *p : plots)
    {
        if (one && p != one)
            continue;
        glBindFramebuffer(GL_FRAMEBUFFER, p->fbo);
        glViewport(0, 0, p->rect.w, p->rect.h);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f); // black background
        glClear(GL_COLOR_BUFFER_BIT);
        float xr = p->x_max - p->x_min;
        float yr = p->y_max - p->y_min;
        glm::mat4 proj = glm::ortho(p->x_min + p->uv_x * xr, p->x_min + (p->uv_x + p->zoom) * xr,
                                    p->y_min + (p->uv_y + p->zoom) * yr, p->y_min + p->uv_y * yr,
                                    -1.0f, 1.0f);
        if (p == &alt_hist) // histogram
        {
            glBindVertexArray(p->vhf.vao);
            glUseProgram(shader.line);
            glUniformMatrix4fv(glGetUniformLocation(shader.line, "projection"), 1, GL_FALSE, glm::value_ptr(proj));
            glDrawArrays(GL_LINE_STRIP, 0, 200);
            glBindVertexArray(0);
        }
        else
        {
            if (p == &lon_lat) // handling maps, stations, features, etc.
            {
                if (map.index != 0)
                {
                    glBindVertexArray(map.data.vao);
                    glUseProgram(shader.line);
                    glUniformMatrix4fv(glGetUniformLocation(shader.line, "projection"), 1, GL_FALSE, glm::value_ptr(proj));
                    glDrawArrays(GL_LINES, 0, map.sizes[map.index]);
                    glBindVertexArray(0);
                }
                if (count.stations > 0)
                {
                    glBindVertexArray(stations.vao);
                    glUseProgram(shader.stations);
                    glUniformMatrix4fv(glGetUniformLocation(shader.stations, "projection"), 1, GL_FALSE, glm::value_ptr(proj));
                    glEnable(GL_PROGRAM_POINT_SIZE);
                    glDrawArrays(GL_POINTS, 0, count.stations);
                    glBindVertexArray(0);
                }
            }
            // vhf scatter plot
            glBindVertexArray(p->vhf.vao);
            glUseProgram(shader.vhf);
            glEnable(GL_PROGRAM_POINT_SIZE);
            glUniformMatrix4fv(glGetUniformLocation(shader.vhf, "projection"), 1, GL_FALSE, glm::value_ptr(proj));
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, colormap.texture);
            glUniform1i(glGetUniformLocation(shader.vhf, "colormaps"), 0);
            glUniform1i(glGetUniformLocation(shader.vhf, "cmap_index"), colormap.index);
            glDrawArrays(GL_POINTS, 0, state.anime.animating ? state.anime.sources : count.vhf);
            glBindVertexArray(0);
            if (entln.cg || entln.ic)
            {
                int plot_from = entln.cg ? 0 : count.entln_cg;
                int plot_to = entln.ic ? count.entln : count.entln_cg;
                glBindVertexArray(p->entln.vao);
                glUseProgram(shader.entln);
                glEnable(GL_PROGRAM_POINT_SIZE);
                glUniformMatrix4fv(glGetUniformLocation(shader.entln, "projection"), 1, GL_FALSE, glm::value_ptr(proj));
                glDrawArrays(GL_POINTS, plot_from, plot_to);
                glBindVertexArray(0);
            }
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
}

void Graphics::UpdateTickLabels()
{
    // updates tick labels based on pre-determined tick locations
    // simply calculates values for labels based on min, max and zoom level
    for (unsigned int i = 0; i < 5; i++)
    {
        float t = 0.1f + i * 0.2f;

        auto ns = static_cast<int64_t>(
            (time_alt.x_min + (time_alt.uv_x + t * time_alt.zoom) * (time_alt.x_max - time_alt.x_min)));
        int64_t total_sec = ns / 1000000000LL;
        int h = total_sec / 3600;
        int m = (total_sec % 3600) / 60;
        int s = total_sec % 60;
        int ms = (ns % 1000000000LL) / 1000000LL;
        time_alt.x_major_ticks[i] = std::format("{:02}:{:02}:{:02}.{:03}", h, m, s, ms);
        lon_alt.x_major_ticks[i] = std::format("{:.4f}", lon_alt.x_min + (lon_alt.uv_x + t * lon_alt.zoom) * (lon_alt.x_max - lon_alt.x_min));
        lon_lat.x_major_ticks[i] = std::format("{:.4f}", lon_lat.x_min + (lon_lat.uv_x + t * lon_lat.zoom) * (lon_lat.x_max - lon_lat.x_min));
        lon_lat.y_major_ticks[i] = std::format("{:.4f}", lon_lat.y_min + (lon_lat.uv_y + t * lon_lat.zoom) * (lon_lat.y_max - lon_lat.y_min));
        alt_lat.y_major_ticks[i] = std::format("{:.4f}", alt_lat.y_min + (alt_lat.uv_y + t * alt_lat.zoom) * (alt_lat.y_max - alt_lat.y_min));
    }
    for (unsigned int i = 0; i < 3; i++)
    {
        float t = 0.2f + i * 0.3f;

        time_alt.y_major_ticks[i] = std::format("{:.1f}", time_alt.y_min + (time_alt.uv_y + t * time_alt.zoom) * (time_alt.y_max - time_alt.y_min));
        alt_hist.x_major_ticks[i] = std::format("{:.1f}", alt_hist.x_min + (alt_hist.uv_x + t * alt_hist.zoom) * (alt_hist.x_max - alt_hist.x_min));
        alt_hist.y_major_ticks[i] = std::format("{:.1f}", alt_hist.y_min + (alt_hist.uv_y + t * alt_hist.zoom) * (alt_hist.y_max - alt_hist.y_min));
        lon_alt.y_major_ticks[i] = std::format("{:.1f}", lon_alt.y_min + (lon_alt.uv_y + t * lon_alt.zoom) * (lon_alt.y_max - lon_alt.y_min));
        alt_lat.x_major_ticks[i] = std::format("{:.1f}", alt_lat.x_min + (alt_lat.uv_x + t * alt_lat.zoom) * (alt_lat.x_max - alt_lat.x_min));
    }
}

void Graphics::ReadStations(const std::string &filepath)
{
    bool is_gz = false;
    {
        std::ifstream probe(filepath, std::ios::binary);
        if (probe)
        {
            unsigned char b[2] = {};
            probe.read(reinterpret_cast<char*>(b), 2);
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
    count.stations = sta_coords.size() / 2;
    glBindBuffer(GL_ARRAY_BUFFER, stations.vbo);
    glBufferData(GL_ARRAY_BUFFER, sta_coords.size() * sizeof(float), sta_coords.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void Graphics::Reset()
{
    // clears all plots with black for now
    for (Plot *p : plots)
    {
        p->zoom = 1.0f;
        p->uv_x = 0.0f;
        p->uv_y = 0.0f;
    }
}

void Graphics::ClearPlot()
{
    // clears all plots with black for now
    for (Plot *p : plots)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, p->fbo);
        glViewport(0, 0, p->rect.w, p->rect.h);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        p->zoom = 1.0f;
        p->uv_x = 0.0f;
        p->uv_y = 0.0f;
    }
}
