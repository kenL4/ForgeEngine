#include "gl_renderer.h"

#include <GL/glew.h>
#include <SDL2/SDL.h>
#include <math.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void camera_basis(const camera_t *cam, float fwd[3], float right[3], float up[3])
{
    float cy = cosf(cam->yaw), sy = sinf(cam->yaw);
    float cp = cosf(cam->pitch), sp = sinf(cam->pitch);
    fwd[0] = cp * sy;
    fwd[1] = sp;
    fwd[2] = -cp * cy;

    float world_up[3] = {0, 1, 0};
    right[0] = fwd[1] * world_up[2] - fwd[2] * world_up[1];
    right[1] = fwd[2] * world_up[0] - fwd[0] * world_up[2];
    right[2] = fwd[0] * world_up[1] - fwd[1] * world_up[0];
    float rlen = 1.0f / sqrtf(right[0]*right[0] + right[1]*right[1] + right[2]*right[2]);
    right[0] *= rlen; right[1] *= rlen; right[2] *= rlen;

    up[0] = right[1] * fwd[2] - right[2] * fwd[1];
    up[1] = right[2] * fwd[0] - right[0] * fwd[2];
    up[2] = right[0] * fwd[1] - right[1] * fwd[0];
}

struct gl_renderer {
    int width;
    int height;
    GLuint program;
    GLuint vao;
    GLuint vbo;
    bool ok;
};

static char *load_file(const char *path)
{
    SDL_RWops *f = SDL_RWFromFile(path, "rb");
    if (!f)
        return NULL;

    Sint64 size = SDL_RWsize(f);
    if (size <= 0) {
        SDL_RWclose(f);
        return NULL;
    }

    char *buf = malloc((size_t)size + 1);
    if (!buf) {
        SDL_RWclose(f);
        return NULL;
    }

    size_t n = SDL_RWread(f, buf, 1, (size_t)size);
    SDL_RWclose(f);
    buf[n] = '\0';
    return buf;
}

static GLuint compile_shader(GLenum type, const char *path)
{
    char *src = load_file(path);
    if (!src) {
        fprintf(stderr, "gl_renderer: failed to load shader %s\n", path);
        return 0;
    }

    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, (const GLchar *const *)&src, NULL);
    free(src);

    glCompileShader(shader);

    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[512];
        glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        fprintf(stderr, "gl_renderer: shader %s compile failed:\n%s\n", path, log);
        glDeleteShader(shader);
        return 0;
    }

    return shader;
}

static GLuint link_program(GLuint vert, GLuint frag)
{
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vert);
    glAttachShader(prog, frag);
    glLinkProgram(prog);

    glDeleteShader(vert);
    glDeleteShader(frag);

    GLint success;
    glGetProgramiv(prog, GL_LINK_STATUS, &success);
    if (!success) {
        char log[512];
        glGetProgramInfoLog(prog, sizeof(log), NULL, log);
        fprintf(stderr, "gl_renderer: program link failed:\n%s\n", log);
        glDeleteProgram(prog);
        return 0;
    }

    return prog;
}

gl_renderer *gl_renderer_create(int width, int height)
{
    gl_renderer *r = calloc(1, sizeof(*r));
    if (!r)
        return NULL;

    r->width = width;
    r->height = height;

    /* Compile and link shaders */
    GLuint vert = compile_shader(GL_VERTEX_SHADER, "shaders/raymarch.vert");
    GLuint frag = compile_shader(GL_FRAGMENT_SHADER, "shaders/raymarch.frag");
    if (!vert || !frag) {
        free(r);
        return NULL;
    }

    r->program = link_program(vert, frag);
    if (!r->program) {
        free(r);
        return NULL;
    }

    /* Fullscreen quad: two triangles, NDC coordinates */
    float vertices[] = {
        -1.0f, -1.0f,
         1.0f, -1.0f,
        -1.0f,  1.0f,
         1.0f,  1.0f,
    };

    glGenVertexArrays(1, &r->vao);
    glGenBuffers(1, &r->vbo);

    glBindVertexArray(r->vao);
    glBindBuffer(GL_ARRAY_BUFFER, r->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    r->ok = true;
    return r;
}

void gl_renderer_destroy(gl_renderer *r)
{
    if (!r)
        return;
    if (r->vao)
        glDeleteVertexArrays(1, &r->vao);
    if (r->vbo)
        glDeleteBuffers(1, &r->vbo);
    if (r->program)
        glDeleteProgram(r->program);
    free(r);
}

void gl_renderer_draw(gl_renderer *r, float time_s, const camera_t *cam)
{
    if (!r || !r->ok)
        return;

    glClearColor(0.1f, 0.1f, 0.15f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(r->program);
    glUniform2f(glGetUniformLocation(r->program, "u_resolution"),
                (float)r->width, (float)r->height);
    glUniform1f(glGetUniformLocation(r->program, "u_time"), time_s);

    if (cam) {
        float fwd[3], right[3], up[3];
        camera_basis(cam, fwd, right, up);
        glUniform3fv(glGetUniformLocation(r->program, "u_camera_pos"), 1, cam->pos);
        glUniform3fv(glGetUniformLocation(r->program, "u_camera_forward"), 1, fwd);
        glUniform3fv(glGetUniformLocation(r->program, "u_camera_right"), 1, right);
        glUniform3fv(glGetUniformLocation(r->program, "u_camera_up"), 1, up);
    }

    glViewport(0, 0, r->width, r->height);
    glBindVertexArray(r->vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
}

void gl_renderer_resize(gl_renderer *r, int width, int height)
{
    if (!r)
        return;
    r->width = width;
    r->height = height;
}

bool gl_renderer_reload_shaders(gl_renderer *r)
{
    if (!r || !r->ok)
        return false;

    GLuint vert = compile_shader(GL_VERTEX_SHADER, "shaders/raymarch.vert");
    GLuint frag = compile_shader(GL_FRAGMENT_SHADER, "shaders/raymarch.frag");
    if (!vert || !frag)
        return false;

    GLuint new_prog = link_program(vert, frag);
    if (!new_prog)
        return false;

    glDeleteProgram(r->program);
    r->program = new_prog;
    fprintf(stderr, "Shaders reloaded successfully.\n");
    return true;
}

bool gl_renderer_ok(const gl_renderer *r)
{
    return r && r->ok;
}
