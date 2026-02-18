#include "gl_renderer.h"

#include <GL/glew.h>
#include <SDL2/SDL.h>
#include <math.h>

#include <stdbool.h>
#include <stdio.h>

#define WIDTH 1600
#define HEIGHT 900

#define CAMERA_SPEED 4.0f
#define MOUSE_SENSITIVITY 0.002f

static void camera_update(camera_t *cam, float dt, const Uint8 *keys, int mouse_dx, int mouse_dy)
{
    cam->yaw += (float)mouse_dx * MOUSE_SENSITIVITY;
    cam->pitch -= (float)mouse_dy * MOUSE_SENSITIVITY;
    if (cam->pitch > 1.57f) cam->pitch = 1.57f;
    if (cam->pitch < -1.57f) cam->pitch = -1.57f;

    float cy = cosf(cam->yaw), sy = sinf(cam->yaw);
    float cp = cosf(cam->pitch), sp = sinf(cam->pitch);
    float fwd[3] = { cp * sy, sp, -cp * cy };
    float right[3] = { cy, 0, sy };

    float move[3] = { 0, 0, 0 };
    if (keys[SDL_SCANCODE_W]) { move[0] += fwd[0]; move[1] += fwd[1]; move[2] += fwd[2]; }
    if (keys[SDL_SCANCODE_S]) { move[0] -= fwd[0]; move[1] -= fwd[1]; move[2] -= fwd[2]; }
    if (keys[SDL_SCANCODE_D]) { move[0] += right[0]; move[2] += right[2]; }
    if (keys[SDL_SCANCODE_A]) { move[0] -= right[0]; move[2] -= right[2]; }
    if (keys[SDL_SCANCODE_SPACE]) move[1] += 1.0f;
    if (keys[SDL_SCANCODE_LSHIFT]) move[1] -= 1.0f;

    float len = sqrtf(move[0]*move[0] + move[1]*move[1] + move[2]*move[2]);
    if (len > 0.0001f) {
        len = CAMERA_SPEED * dt / len;
        cam->pos[0] += move[0] * len;
        cam->pos[1] += move[1] * len;
        cam->pos[2] += move[2] * len;
    }
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    if (SDL_Init(SDL_INIT_VIDEO) != 0)
    {
        fprintf(stderr, "Error: Failed to initialise SDL: %s\n", SDL_GetError());
        return 1;
    }

    /* Request OpenGL 4.3 Core context (required for compute shaders) */
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    SDL_Window *window = SDL_CreateWindow(
        "Forge Engine",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        WIDTH, HEIGHT,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE
    );

    if (!window)
    {
        fprintf(stderr, "Error: Failed to open window: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    if (!gl_context)
    {
        fprintf(stderr, "Error: Failed to create OpenGL context: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    SDL_GL_MakeCurrent(window, gl_context);
    SDL_GL_SetSwapInterval(1);  /* VSync */

    /* Initialize GLEW for OpenGL function loading */
    glewExperimental = GL_TRUE;
    GLenum glew_err = glewInit();
    if (glew_err != GLEW_OK)
    {
        fprintf(stderr, "Error: GLEW init failed: %s\n", glewGetErrorString(glew_err));
        SDL_GL_DeleteContext(gl_context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    gl_renderer *renderer = gl_renderer_create(WIDTH, HEIGHT);
    if (!renderer || !gl_renderer_ok(renderer))
    {
        fprintf(stderr, "Error: Failed to create OpenGL renderer\n");
        if (renderer)
            gl_renderer_destroy(renderer);
        SDL_GL_DeleteContext(gl_context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    glViewport(0, 0, WIDTH, HEIGHT);

    camera_t camera = { 0 };
    camera.pos[0] = 0; camera.pos[1] = 0; camera.pos[2] = 0;
    camera.yaw = 0; camera.pitch = 0;

    SDL_SetRelativeMouseMode(SDL_TRUE);

    bool running = true;
    Uint32 last_time = SDL_GetTicks();
    Uint32 last_fps_time = last_time;
    int frame_count = 0;
    int mouse_dx = 0, mouse_dy = 0;

    while (running)
    {
        mouse_dx = 0;
        mouse_dy = 0;

        SDL_Event e;
        while (SDL_PollEvent(&e))
        {
            switch (e.type)
            {
                case SDL_QUIT:
                    running = false;
                    break;
                case SDL_KEYDOWN:
                    if (e.key.keysym.sym == SDLK_r && !e.key.repeat)
                        gl_renderer_reload_shaders(renderer);
                    if (e.key.keysym.sym == SDLK_ESCAPE)
                        SDL_SetRelativeMouseMode(SDL_GetRelativeMouseMode() ? SDL_FALSE : SDL_TRUE);
                    break;
                case SDL_MOUSEMOTION:
                    mouse_dx = e.motion.xrel;
                    mouse_dy = e.motion.yrel;
                    break;
                case SDL_WINDOWEVENT:
                    if (e.window.event == SDL_WINDOWEVENT_RESIZED)
                        gl_renderer_resize(renderer, e.window.data1, e.window.data2);
                    break;
            }
        }

        Uint32 now = SDL_GetTicks();
        float dt = (float)(now - last_time) / 1000.0f;
        last_time = now;
        if (dt > 0.1f) dt = 0.1f;

        const Uint8 *keys = SDL_GetKeyboardState(NULL);
        camera_update(&camera, dt, keys, mouse_dx, mouse_dy);

        gl_renderer_draw(renderer, (float)now / 1000.0f, &camera);
        SDL_GL_SwapWindow(window);

        frame_count++;
        if (now - last_fps_time >= 1000)
        {
            printf("FPS: %d\n", frame_count);
            frame_count = 0;
            last_fps_time = now;
        }
    }

    gl_renderer_destroy(renderer);
    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
