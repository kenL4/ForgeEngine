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
    GLuint compute_program;
    GLuint display_program;
    GLuint output_texture;
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

static GLuint link_compute_program(GLuint comp)
{
    GLuint prog = glCreateProgram();
    glAttachShader(prog, comp);
    glLinkProgram(prog);

    glDeleteShader(comp);

    GLint success;
    glGetProgramiv(prog, GL_LINK_STATUS, &success);
    if (!success) {
        char log[512];
        glGetProgramInfoLog(prog, sizeof(log), NULL, log);
        fprintf(stderr, "gl_renderer: compute program link failed:\n%s\n", log);
        glDeleteProgram(prog);
        return 0;
    }

    return prog;
}

static void create_output_texture(struct gl_renderer *r)
{
    if (r->output_texture)
        glDeleteTextures(1, &r->output_texture);

    glGenTextures(1, &r->output_texture);
    glBindTexture(GL_TEXTURE_2D, r->output_texture);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, r->width, r->height);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
}

gl_renderer *gl_renderer_create(int width, int height)
{
    gl_renderer *r = calloc(1, sizeof(*r));
    if (!r)
        return NULL;

    r->width = width;
    r->height = height;

    /* Compute shader for raymarching */
    GLuint comp = compile_shader(GL_COMPUTE_SHADER, "shaders/raymarch.comp");
    if (!comp) {
        free(r);
        return NULL;
    }

    r->compute_program = link_compute_program(comp);
    if (!r->compute_program) {
        free(r);
        return NULL;
    }

    /* Display pass: fullscreen quad samples compute output */
    GLuint vert = compile_shader(GL_VERTEX_SHADER, "shaders/display.vert");
    GLuint frag = compile_shader(GL_FRAGMENT_SHADER, "shaders/display.frag");
    if (!vert || !frag) {
        glDeleteProgram(r->compute_program);
        free(r);
        return NULL;
    }

    r->display_program = link_program(vert, frag);
    if (!r->display_program) {
        glDeleteProgram(r->compute_program);
        free(r);
        return NULL;
    }

    create_output_texture(r);

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
    if (r->output_texture)
        glDeleteTextures(1, &r->output_texture);
    if (r->compute_program)
        glDeleteProgram(r->compute_program);
    if (r->display_program)
        glDeleteProgram(r->display_program);
    free(r);
}

void gl_renderer_draw(gl_renderer *r, float time_s, const camera_t *cam)
{
    if (!r || !r->ok)
        return;

    /* Compute pass: raymarch into output texture */
    glUseProgram(r->compute_program);
    glUniform2f(glGetUniformLocation(r->compute_program, "u_resolution"),
                (float)r->width, (float)r->height);
    glUniform1f(glGetUniformLocation(r->compute_program, "u_time"), time_s);

    if (cam) {
        float fwd[3], right[3], up[3];
        camera_basis(cam, fwd, right, up);
        glUniform3fv(glGetUniformLocation(r->compute_program, "u_camera_pos"), 1, cam->pos);
        glUniform3fv(glGetUniformLocation(r->compute_program, "u_camera_forward"), 1, fwd);
        glUniform3fv(glGetUniformLocation(r->compute_program, "u_camera_right"), 1, right);
        glUniform3fv(glGetUniformLocation(r->compute_program, "u_camera_up"), 1, up);
    }

    glBindImageTexture(0, r->output_texture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);
    glDispatchCompute((r->width + 7) / 8, (r->height + 7) / 8, 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

    /* Display pass: fullscreen quad samples output */
    glClearColor(0.1f, 0.1f, 0.15f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(r->display_program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, r->output_texture);
    glUniform1i(glGetUniformLocation(r->display_program, "u_image"), 0);

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
    create_output_texture(r);
}

bool gl_renderer_reload_shaders(gl_renderer *r)
{
    if (!r || !r->ok)
        return false;

    GLuint comp = compile_shader(GL_COMPUTE_SHADER, "shaders/raymarch.comp");
    if (!comp)
        return false;

    GLuint new_comp = link_compute_program(comp);
    if (!new_comp)
        return false;

    GLuint vert = compile_shader(GL_VERTEX_SHADER, "shaders/display.vert");
    GLuint frag = compile_shader(GL_FRAGMENT_SHADER, "shaders/display.frag");
    if (!vert || !frag) {
        glDeleteProgram(new_comp);
        return false;
    }

    GLuint new_disp = link_program(vert, frag);
    if (!new_disp) {
        glDeleteProgram(new_comp);
        return false;
    }

    glDeleteProgram(r->compute_program);
    glDeleteProgram(r->display_program);
    r->compute_program = new_comp;
    r->display_program = new_disp;
    fprintf(stderr, "Shaders reloaded successfully.\n");
    return true;
}

bool gl_renderer_ok(const gl_renderer *r)
{
    return r && r->ok;
}
