#include "state.h"
#include <format>

void State::InitializeGraphics()
{
    const char *vhf_vert = R"(
#version 330 core
layout(location = 0) in float x_pos;
layout(location = 1) in float y_pos;
layout(location = 2) in float value;
uniform mat4 projection;
out float vValue;
void main() {
    gl_Position = projection * vec4(x_pos, y_pos, 0.0, 1.0);
    gl_PointSize = 1.0;
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
    float y = (float(cmap_index) + 0.5) / 5.0;
    FragColor = texture(colormaps, vec2(vValue, y));
    FragColor.a = 1.0;
}
)";

    const char *line_vert = R"(
#version 330 core
layout(location = 0) in float x_pos;
layout(location = 1) in float y_pos;
uniform mat4 projection;
void main() {
    gl_Position = projection * vec4(x_pos, y_pos, 0.0, 1.0);
}
)";

    const char *line_frag = R"(
#version 330 core
out vec4 FragColor;
void main() {
    FragColor = vec4(1.0);
}
)";

    // vhf source shader
    GLuint vhf_vert_shader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vhf_vert_shader, 1, &vhf_vert, nullptr);
    glCompileShader(vhf_vert_shader);
    GLuint vhf_frag_shader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(vhf_frag_shader, 1, &vhf_frag, nullptr);
    glCompileShader(vhf_frag_shader);
    graphics.vhf_shader = glCreateProgram();
    glAttachShader(graphics.vhf_shader, vhf_vert_shader);
    glAttachShader(graphics.vhf_shader, vhf_frag_shader);
    glLinkProgram(graphics.vhf_shader);
    glDeleteShader(vhf_vert_shader);
    glDeleteShader(vhf_frag_shader);

    // line shader
    GLuint line_vert_shader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(line_vert_shader, 1, &line_vert, nullptr);
    glCompileShader(line_vert_shader);
    GLuint line_frag_shader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(line_frag_shader, 1, &line_frag, nullptr);
    glCompileShader(line_frag_shader);
    graphics.line_shader = glCreateProgram();
    glAttachShader(graphics.line_shader, line_vert_shader);
    glAttachShader(graphics.line_shader, line_frag_shader);
    glLinkProgram(graphics.line_shader);
    glDeleteShader(line_vert_shader);
    glDeleteShader(line_frag_shader);

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

    // setting up map vbo and vao
    glGenVertexArrays(1, &graphics.map.vao);
    glGenBuffers(1, &graphics.map.vbo);
    glBindVertexArray(graphics.map.vao);
    glBindBuffer(GL_ARRAY_BUFFER, graphics.map.vbo);
    size_t map_size = 2 * 1771152 * sizeof(float);
    glBufferData(GL_ARRAY_BUFFER, map_size, nullptr, GL_STATIC_DRAW);
    void *map_ptr = glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
    f = fopen("bin/map.bin", "rb");
    fread(map_ptr, sizeof(float), 2 * 1771152, f);
    fclose(f);
    glUnmapBuffer(GL_ARRAY_BUFFER);
    glVertexAttribPointer(0, 1, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void *)(sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    // histogram vbo and vao
    glGenVertexArrays(1, &graphics.hist_vao);
    glGenBuffers(1, &graphics.hist_vbo);
    glBindVertexArray(graphics.hist_vao);
    glBindBuffer(GL_ARRAY_BUFFER, graphics.hist_vbo);
    glVertexAttribPointer(0, 1, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void *)(sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    glGenBuffers(1, &graphics.vbo);
    // setting up plots
    auto setup = [this](Plot &plot_type, int x_offset, int y_offset)
    {
        // global vhf vbo and vao
        if (x_offset != 0 || y_offset != 0)
        {
            glGenVertexArrays(1, &plot_type.vao);
            glBindVertexArray(plot_type.vao);
            glBindBuffer(GL_ARRAY_BUFFER, graphics.vbo);
            glVertexAttribPointer(0, 1, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void *)(x_offset * sizeof(float)));
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void *)(y_offset * sizeof(float)));
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void *)(4 * sizeof(float)));
            glEnableVertexAttribArray(2);
            glBindVertexArray(0);
        }
        glGenTextures(1, &plot_type.texture);
        glBindTexture(GL_TEXTURE_2D, plot_type.texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, plot_type.width, plot_type.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glGenFramebuffers(1, &plot_type.fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, plot_type.fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, plot_type.texture, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    };

    setup(time_alt, 0, 3);
    setup(lon_alt, 1, 3);
    setup(alt_hist, 0, 0);
    setup(lon_lat, 1, 2);
    setup(alt_lat, 3, 2);

    graphics.initialized = true;
}

void State::ProcessResult(duckdb::unique_ptr<duckdb::MaterializedQueryResult> &res)
{
    // initializing opengl stuff
    if (!graphics.initialized)
        InitializeGraphics();

    graphics.sources = res->RowCount();

    if (graphics.sources > 1) // to prevent axis collapse from min and max being equal
    {
        // ptr to vbo
        glBindBuffer(GL_ARRAY_BUFFER, graphics.vbo);
        glBufferData(GL_ARRAY_BUFFER, graphics.sources * 5 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
        float *vhf_ptr = (float *)glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);

        // axis updates
        time_alt.x_min = res->GetValue<float>(5, 0);
        time_alt.x_max = res->GetValue<float>(6, 0);
        time_alt.y_min = lon_alt.y_min = alt_hist.y_min = alt_lat.x_min = res->GetValue<float>(11, 0);
        time_alt.y_max = lon_alt.y_max = alt_hist.y_max = alt_lat.x_max = res->GetValue<float>(12, 0);
        lon_alt.x_min = lon_lat.x_min = res->GetValue<float>(7, 0);
        lon_alt.x_max = lon_lat.x_max = res->GetValue<float>(8, 0);
        alt_lat.y_min = lon_lat.y_min = res->GetValue<float>(9, 0);
        alt_lat.y_max = lon_lat.y_max = res->GetValue<float>(10, 0);
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
            lon_alt.y_major_ticks[2 - i] = std::format("{:.1f}", lon_alt.y_min + t * (lon_alt.y_max - lon_alt.y_min));
            alt_hist.y_major_ticks[2 - i] = std::format("{:.1f}", alt_hist.y_min + t * (alt_hist.y_max - alt_hist.y_min));
            alt_lat.x_major_ticks[i] = std::format("{:.1f}", alt_lat.x_min + t * (alt_lat.x_max - alt_lat.x_min));
        }

        // extracting info from sql query output
        size_t chunk_i = 0; // global index
        while (auto chunk = res->Fetch())
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

        // populating the vbo
        glUnmapBuffer(GL_ARRAY_BUFFER);
        glBindBuffer(GL_ARRAY_BUFFER, 0);

        // rendering the points
        Render();
    }
    else
        status = "Not enough data to plot with current selection";
}

void State::Histogram(duckdb::unique_ptr<duckdb::MaterializedQueryResult> &res)
{
    if (graphics.sources > 1)
    {
        // hist vbo
        glBindBuffer(GL_ARRAY_BUFFER, graphics.hist_vbo);
        glBufferData(GL_ARRAY_BUFFER, 200 * 2 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
        float *hist_ptr = (float *)glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);

        // axis updates
        alt_hist.x_min = res->GetValue<float>(2, 0);
        alt_hist.x_max = res->GetValue<float>(3, 0);
        for (int i = 0; i < 3; i++)
        {
            float t = 0.2f + i * 0.3f;
            alt_hist.x_major_ticks[i] = std::format("{:.1f}", alt_hist.x_min + t * (alt_hist.x_max - alt_hist.x_min));
        }

        size_t chunk_i = 0; // global index
        while (auto chunk = res->Fetch())
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
    }

    glBindFramebuffer(GL_FRAMEBUFFER, alt_hist.fbo);
    glViewport(0, 0, alt_hist.width, alt_hist.height);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glBindVertexArray(graphics.hist_vao);
    glBindBuffer(GL_ARRAY_BUFFER, graphics.hist_vbo);
    glVertexAttribPointer(0, 1, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void *)(sizeof(float)));
    glEnableVertexAttribArray(1);
    glUseProgram(graphics.line_shader);
    glm::mat4 proj = glm::ortho(alt_hist.x_min, alt_hist.x_max, alt_hist.y_max, alt_hist.y_min, -1.0f, 1.0f);
    glUniformMatrix4fv(glGetUniformLocation(graphics.line_shader, "projection"), 1, GL_FALSE, glm::value_ptr(proj));
    glDrawArrays(GL_LINE_STRIP, 0, 200);
    glBindVertexArray(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void State::ProcessColor(duckdb::unique_ptr<duckdb::MaterializedQueryResult> &res)
{
    if (res->RowCount() == graphics.sources)
    {
        if (graphics.sources > 1) // to prevent axis collapse from min and max being equal
        {
            // using pointer to change values in memory
            glBindBuffer(GL_ARRAY_BUFFER, graphics.vbo);
            float *vhf_ptr = (float *)glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
            size_t chunk_i = 0; // global index
            while (auto chunk = res->Fetch())
            {
                auto &color_vec = chunk->data[0];
                float *color_data = duckdb::FlatVector::GetData<float>(color_vec);
                size_t row_count = chunk->size();
                for (size_t i = 0; i < row_count; i++)
                {
                    vhf_ptr[((chunk_i + i)) * 5 + 4] = color_data[i];
                }
                chunk_i += row_count;
            }
            glUnmapBuffer(GL_ARRAY_BUFFER);
            glBindBuffer(GL_ARRAY_BUFFER, 0);

            //  rendering the points
            Render();
        }
    }
    else
        status = std::string("Color by ") + graphics.colormap.by_options[graphics.colormap.by_index] + " failed";
}

void State::Render()
{
    if (graphics.sources > 1)
    {
        auto render = [&](Plot &plot_type, int x_offset, int y_offset)
        {
            glBindFramebuffer(GL_FRAMEBUFFER, plot_type.fbo);
            glViewport(0, 0, plot_type.width, plot_type.height);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            if (x_offset == 1 && y_offset == 2) // lon lat plot special: map, features, stations
            {
                glBindVertexArray(graphics.map.vao);
                glUseProgram(graphics.line_shader);
                glm::mat4 proj = glm::ortho(plot_type.x_min, plot_type.x_max, plot_type.y_max, plot_type.y_min, -1.0f, 1.0f);
                glUniformMatrix4fv(glGetUniformLocation(graphics.line_shader, "projection"), 1, GL_FALSE, glm::value_ptr(proj));
                glDrawArrays(GL_LINES, 0, graphics.map.sizes[graphics.map.index]);
                glBindVertexArray(0);
            }
            glBindVertexArray(plot_type.vao);
            // glBindBuffer(GL_ARRAY_BUFFER, graphics.vbo);
            // glVertexAttribPointer(0, 1, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void *)(x_offset * sizeof(float)));
            // glEnableVertexAttribArray(0);
            // glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void *)(y_offset * sizeof(float)));
            // glEnableVertexAttribArray(1);
            // glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void *)(4 * sizeof(float)));
            // glEnableVertexAttribArray(2);
            glUseProgram(graphics.vhf_shader);
            glEnable(GL_PROGRAM_POINT_SIZE);
            glm::mat4 proj = glm::ortho(plot_type.x_min, plot_type.x_max, plot_type.y_max, plot_type.y_min, -1.0f, 1.0f);
            glUniformMatrix4fv(glGetUniformLocation(graphics.vhf_shader, "projection"), 1, GL_FALSE, glm::value_ptr(proj));
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, graphics.colormap.texture);
            glUniform1i(glGetUniformLocation(graphics.vhf_shader, "colormaps"), 0);
            glUniform1i(glGetUniformLocation(graphics.vhf_shader, "cmap_index"), graphics.colormap.index);
            glDrawArrays(GL_POINTS, 0, anime.animating ? anime.sources : graphics.sources);
            glBindVertexArray(0);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        };

        render(time_alt, 0, 3);
        render(lon_alt, 1, 3);
        render(lon_lat, 1, 2);
        render(alt_lat, 3, 2);
        status = "Plotted " + std::to_string(graphics.sources) + " sources with " + graphics.colormap.options[graphics.colormap.index] + " colormap in " + std::to_string(timer.End()) + "ms";
    }
}

void State::Frame()
{
    if (anime.animating && graphics.sources > 1)
    {
        float elapsed = (float)anime.Elapsed() / anime.duration_ms;
        if (elapsed <= 1)
        {
            anime.sources = (size_t)(std::min(elapsed, 1.0f) * graphics.sources);
            Render();
        }
        else
        {
            anime.End();
            Render(); // final render to ensure everything is plotted
        }
    }
}

void State::Clear()
{
    // clearing all data in state.
    return;
}