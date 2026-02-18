#pragma once

#include <stdbool.h>

typedef struct gl_renderer gl_renderer;

/* Camera state for the raymarcher. */
typedef struct {
    float pos[3];   /* x, y, z */
    float yaw;      /* radians, rotation about Y (left/right) */
    float pitch;    /* radians, rotation about X (up/down) */
} camera_t;

/* Create and initialize the OpenGL renderer. Returns NULL on failure. */
gl_renderer *gl_renderer_create(int width, int height);

/* Destroy the renderer and free resources. */
void gl_renderer_destroy(gl_renderer *r);

/* Draw a frame. Call once per frame. time_s: seconds since program start. */
void gl_renderer_draw(gl_renderer *r, float time_s, const camera_t *cam);

/* Resize the viewport. Call when window is resized. */
void gl_renderer_resize(gl_renderer *r, int width, int height);

/* Reload shaders from disk. Returns true on success; on failure keeps old shaders. */
bool gl_renderer_reload_shaders(gl_renderer *r);

/* Return true if the renderer is valid. */
bool gl_renderer_ok(const gl_renderer *r);
