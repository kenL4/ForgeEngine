#version 330 core

in vec2 v_uv;

out vec4 frag_color;

uniform vec2 u_resolution;
uniform float u_time;
uniform vec3 u_camera_pos;
uniform vec3 u_camera_forward;
uniform vec3 u_camera_right;
uniform vec3 u_camera_up;

/* ---- Shading ---- */

vec3 lambert(vec3 pos, vec3 normal, vec3 light_pos, vec3 base_color)
{
    vec3 d = normalize(light_pos - pos);
    float intensity = max(0.0, dot(d, normal));
    return intensity * base_color;
}

/* SDF result with material color for per-primitive coloring */
struct SDFHit {
    float d;
    vec3 color;
};

/* ---- SDF Primitives ---- */

float sdf_sphere(vec3 p, float radius)
{
    return length(p) - radius;
}

float sdf_box(vec3 p, vec3 b)
{
    vec3 q = abs(p) - b;
    return length(max(q,0.0)) + min(max(q.x,max(q.y,q.z)),0.0);
}

/* Primitive variants that return SDFHit (distance + color) */
SDFHit sdf_sphere_color(vec3 p, float radius, vec3 color)
{
    return SDFHit(sdf_sphere(p, radius), color);
}

SDFHit sdf_box_color(vec3 p, vec3 b, vec3 color)
{
    return SDFHit(sdf_box(p, b), color);
}

/* ---- SDF Operations ---- */

// Taken directly from Inigo Quilez' legendary article on SDF functions
float opUnion( float a, float b )
{
    return min(a,b);
}
float opSubtraction( float a, float b )
{
    return max(-a,b);
}
float opIntersection( float a, float b )
{
    return max(a,b);
}
float opXor( float a, float b )
{
    return max(min(a,b),-max(a,b));
}

/* Ops that propagate material color (use color of "winning" primitive) */
SDFHit opUnion(SDFHit a, SDFHit b)
{
    return (a.d < b.d) ? a : b;
}
SDFHit opSubtraction(SDFHit a, SDFHit b)
{
    return SDFHit(max(a.d, -b.d), a.color);  /* carve b from a; outer shape's color */
}
SDFHit opIntersection(SDFHit a, SDFHit b)
{
    return SDFHit(max(a.d, b.d), b.color);
}

float opSmoothUnion( float a, float b, float k )
{
    k *= 4.0;
    float h = max(k-abs(a-b),0.0);
    return min(a, b) - h*h*0.25/k;
}
float opSmoothSubtraction( float a, float b, float k )
{
    return -opSmoothUnion(a,-b,k);
}
float opSmoothIntersection( float a, float b, float k )
{
    return -opSmoothUnion(-a,-b,k);
}

/* Smooth ops with color blending (blend factor from polynomial smooth min) */
SDFHit opSmoothUnion(SDFHit a, SDFHit b, float k)
{
    k *= 4.0;
    float h = max(k - abs(a.d - b.d), 0.0);
    float d = min(a.d, b.d) - h*h*0.25/k;
    float t = clamp(0.5 + 0.5*(b.d - a.d)/k, 0.0, 1.0);
    vec3 col = mix(a.color, b.color, t);
    return SDFHit(d, col);
}
SDFHit opSmoothSubtraction(SDFHit a, SDFHit b, float k)
{
    /* carve b from a: smooth max(a, -b) = -smooth_min(-a, b) */
    SDFHit neg_a = SDFHit(-a.d, a.color);
    SDFHit u = opSmoothUnion(neg_a, b, k);
    return SDFHit(-u.d, a.color);
}
SDFHit opSmoothIntersection(SDFHit a, SDFHit b, float k)
{
    SDFHit neg_a = SDFHit(-a.d, a.color);
    SDFHit neg_b = SDFHit(-b.d, b.color);
    SDFHit u = opSmoothUnion(neg_a, neg_b, k);
    return SDFHit(-u.d, u.color);
}

/* Scene SDF: combine primitives with per-primitive colors */
SDFHit scene_sdf(vec3 pos)
{
    vec3 sphere_centre = vec3(0.0, sin(u_time), -3.0);
    vec3 box_centre = vec3(cos(u_time) - 2.0, 0.0, -3.0);
    vec3 box_dims = vec3(1.0, 0.5, 0.5);

    SDFHit plane = sdf_box_color(pos - vec3(0.0, -2.0, 0.0), vec3(100., 0.5, 100.0), vec3(0.35, 0.35, 0.4));
    SDFHit sphere = sdf_sphere_color(pos - sphere_centre, 1.0, vec3(1.0, 0.25, 0.25));
    SDFHit box = sdf_box_color(pos - box_centre, box_dims, vec3(0.25, 0.55, 1.0));

    SDFHit res = opSmoothSubtraction(sphere, box, 0.05);
    res = opUnion(res, box);
    // res = opUnion(res, sphere);
    res = opUnion(res, plane);
    return res;
}

/* Numeric normal via finite differences - works for any SDF */
vec3 calc_normal(vec3 p)
{
    const float eps = 0.0001;
    vec3 n;
    n.x = scene_sdf(p + vec3(eps, 0.0, 0.0)).d - scene_sdf(p - vec3(eps, 0.0, 0.0)).d;
    n.y = scene_sdf(p + vec3(0.0, eps, 0.0)).d - scene_sdf(p - vec3(0.0, eps, 0.0)).d;
    n.z = scene_sdf(p + vec3(0.0, 0.0, eps)).d - scene_sdf(p - vec3(0.0, 0.0, eps)).d;
    return normalize(n);
}

/* Raymarching: returns (hit, hit_pos, hit_normal, hit_color) */
void raymarch(vec3 origin, vec3 dir, out bool hit, out vec3 hit_pos, out vec3 hit_normal, out vec3 hit_color)
{
    hit = false;
    hit_pos = vec3(0.0);
    hit_normal = vec3(0.0, 1.0, 0.0);
    hit_color = vec3(1.0);

    const int MAX_STEPS = 128;
    const float threshold = 0.001;
    const float max_dist = 100.0;

    float dist = 0.0;
    for (int step = 0; step < MAX_STEPS; step++)
    {
        vec3 p = origin + dist * dir;
        SDFHit h = scene_sdf(p);

        if (h.d < threshold)
        {
            hit = true;
            hit_pos = p;
            hit_normal = calc_normal(p);
            hit_color = h.color;
            return;
        }

        dist += h.d;
        if (dist > max_dist)
            break;
    }
}

/* Shadow ray: returns 0 = full shadow, 1 = no shadow (soft penumbra in between) */
float shadow_ray(vec3 origin, vec3 dir, float max_dist)
{
    const int MAX_STEPS = 64;
    const float threshold = 0.001;
    const float k = 32.0;  /* soft shadow sharpness: higher = harder shadows */

    float dist = 0.0;
    float soft = 1.0;

    for (int step = 0; step < MAX_STEPS; step++)
    {
        if (dist >= max_dist)
            return soft;

        vec3 p = origin + dist * dir;
        float d = scene_sdf(p).d;

        if (d < threshold)
            return 0.0;

        soft = min(soft, k * d / max(dist, 0.001));
        dist += d;
    }
    return soft;
}

void main()
{
    vec2 resolution = u_resolution;
    float aspect = resolution.x / resolution.y;

    /* Ray from pixel - camera basis from SDL-controlled camera */
    float u = (gl_FragCoord.x + 0.5) / resolution.x;
    float v = (gl_FragCoord.y + 0.5) / resolution.y;
    u = 2.0 * u - 1.0;
    v = 2.0 * v - 1.0;
    u *= aspect;

    vec3 origin = u_camera_pos;
    vec3 dir = normalize(u * u_camera_right + v * u_camera_up + u_camera_forward);

    bool hit;
    vec3 hit_pos;
    vec3 hit_normal;
    vec3 hit_color;
    raymarch(origin, dir, hit, hit_pos, hit_normal, hit_color);

    if (hit)
    {
        vec3 light_pos = vec3(5., 10., 3.);
        vec3 to_light = light_pos - hit_pos;
        float light_dist = length(to_light);
        vec3 shadow_origin = hit_pos + hit_normal * 0.001;
        vec3 shadow_dir = normalize(to_light);
        float shadow = shadow_ray(shadow_origin, shadow_dir, light_dist - 0.002);

        vec3 col = lambert(hit_pos, hit_normal, light_pos, hit_color) * shadow;
        frag_color = vec4(col, 1.0);
    }
    else
    {
        frag_color = vec4(0.15, 0.15, 0.2, 1.0);
    }
}
