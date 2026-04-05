#ifndef SHADER_H
#define SHADER_H

#include <GL/glew.h>
#include <string>

struct Shader
{
    GLuint vhf, line, stations, entln;

    // vhf: shader for vhf sources
    // line: shader for map and histogram
    // stations: shader for lma stations
    // entln: shader for entln datas

    void Initialize();                  // initailizes all shaders
    void UpdateVHFShader(int size);     // helper for updating vhf shader size
    void UpdateLineShader(float color); // helper for updating line shader color

private:
    void _link_shader(GLuint &program, const char *vert_src, const char *frag_src); // links shader orograms with their glsl
};

#endif