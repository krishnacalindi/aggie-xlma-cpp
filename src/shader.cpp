#include "shader.h"

void Shader::_link_shader(GLuint &program, const char *vert_src, const char *frag_src)
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

void Shader::Initialize()
{
    // vhf shader (default size: 1)
    const char *vhf_vert = R"(
#version 330 core
layout(location = 0) in float x_pos;
layout(location = 1) in float y_pos;
layout(location = 2) in float value;
uniform mat4 projection;
uniform float vhf_size;
out float v_value;
void main() {
    gl_Position = projection * vec4(x_pos, y_pos, 0.0, 1.0);
    gl_PointSize = vhf_size;
    v_value = value;
}
)";

    const char *vhf_frag = R"(
#version 330 core
in float v_value;
out vec4 FragColor;
uniform sampler2D colormaps;
uniform int cmap_index;
void main() {
    if (length(gl_PointCoord - vec2(0.5)) > 0.5)
        discard;
    float y = (float(cmap_index) + 0.5) / 5.0;
    FragColor = texture(colormaps, vec2(v_value, y));
    FragColor.a = 1.0;
}
)";
    _link_shader(vhf, vhf_vert, vhf_frag);
    UpdateVHFShader(1);

    // line shader (default color: white)
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
    FragColor = vec4(1.0, 1.0, 1.0, 1.0);
}
)";
    _link_shader(line, line_vert, line_frag);

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
    _link_shader(stations, sta_vert, sta_frag);

    // entln shader
    const char *entln_vert = R"(
#version 330 core
layout(location = 0) in float x_pos;
layout(location = 1) in float y_pos;
layout(location = 2) in float l_type;
layout(location = 3) in float charge;

uniform mat4 projection;

out float v_type;
out float v_charge;

void main() {
    gl_Position = projection * vec4(x_pos, y_pos, 0.0, 1.0);
    gl_PointSize = 1.0;
    v_type = l_type;
    v_charge = charge;
}
)";

    const char *entln_frag = R"(
#version 330 core
in float v_type;
in float v_charge;
out vec4 FragColor;

void main() {
    vec2 p = gl_PointCoord - vec2(0.5);

    if (v_type < 0.5) {
        float edge  =  p.y + 0.35;
        float left  =  p.x + (p.y + 0.35) * 0.577;
        float right = -p.x + (p.y + 0.35) * 0.577;
        if (edge < -0.15 || left < 0.0 || right < 0.0)
            discard;
    } else {
        float d1 = abs(p.x - p.y);
        float d2 = abs(p.x + p.y);
        float thickness = 0.08;
        if (d1 > thickness && d2 > thickness)
            discard;
        if (abs(p.x) > 0.45 || abs(p.y) > 0.45)
            discard;
    }

    FragColor = v_charge > 0.0
        ? vec4(1.0, 0.0, 0.0, 1.0)
        : vec4(0.0, 0.0, 1.0, 1.0);
}
)";
    _link_shader(entln, entln_vert, entln_frag);
}

void Shader::UpdateVHFShader(int size)
{
    glUseProgram(vhf);
    glUniform1f(glGetUniformLocation(vhf, "vhf_size"), (float)size);
}
