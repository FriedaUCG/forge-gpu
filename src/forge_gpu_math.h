#ifndef SDLGPU_FORGE_GPU_MATH_H
#define SDLGPU_FORGE_GPU_MATH_H

#include "forge_gpu_internal.h"

static inline Mat4 mat4_identity(void)
{
    Mat4 result;
    SDL_zero(result);
    result.m[0] = 1.0f;
    result.m[5] = 1.0f;
    result.m[10] = 1.0f;
    result.m[15] = 1.0f;
    return result;
}

static inline Mat4 mat4_multiply(Mat4 a, Mat4 b)
{
    Mat4 result;
    SDL_zero(result);
    for (int col = 0; col < 4; col += 1) {
        for (int row = 0; row < 4; row += 1) {
            float sum = 0.0f;
            for (int k = 0; k < 4; k += 1) {
                sum += a.m[k * 4 + row] * b.m[col * 4 + k];
            }
            result.m[col * 4 + row] = sum;
        }
    }
    return result;
}

static inline Mat4 mat4_rotate_x(float angle)
{
    Mat4 result = mat4_identity();
    const float c = SDL_cosf(angle);
    const float s = SDL_sinf(angle);
    result.m[5] = c;
    result.m[9] = -s;
    result.m[6] = s;
    result.m[10] = c;
    return result;
}

static inline Mat4 mat4_rotate_y(float angle)
{
    Mat4 result = mat4_identity();
    const float c = SDL_cosf(angle);
    const float s = SDL_sinf(angle);
    result.m[0] = c;
    result.m[8] = s;
    result.m[2] = -s;
    result.m[10] = c;
    return result;
}

static inline Mat4 mat4_translate(Vec3 translation)
{
    Mat4 result = mat4_identity();
    result.m[12] = translation.x;
    result.m[13] = translation.y;
    result.m[14] = translation.z;
    return result;
}

static inline Mat4 mat4_scale(float scale)
{
    Mat4 result = mat4_identity();
    result.m[0] = scale;
    result.m[5] = scale;
    result.m[10] = scale;
    return result;
}

static inline Mat4 mat4_scale_vec3(Vec3 scale)
{
    Mat4 result = mat4_identity();
    result.m[0] = scale.x;
    result.m[5] = scale.y;
    result.m[10] = scale.z;
    return result;
}

static inline Mat4 mat4_from_forge(ForgeGpuMat4 matrix)
{
    Mat4 result;
    SDL_memcpy(result.m, matrix.m, sizeof(result.m));
    return result;
}

static inline Vec3 vec3_add(Vec3 a, Vec3 b)
{
    Vec3 result = { a.x + b.x, a.y + b.y, a.z + b.z };
    return result;
}

static inline Vec3 vec3_sub(Vec3 a, Vec3 b)
{
    Vec3 result = { a.x - b.x, a.y - b.y, a.z - b.z };
    return result;
}

static inline Vec3 vec3_scale(Vec3 v, float scale)
{
    Vec3 result = { v.x * scale, v.y * scale, v.z * scale };
    return result;
}

static inline float vec3_dot(Vec3 a, Vec3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static inline Vec3 vec3_cross(Vec3 a, Vec3 b)
{
    Vec3 result = {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
    return result;
}

static inline Vec3 vec3_normalize(Vec3 v)
{
    const float length = SDL_sqrtf(vec3_dot(v, v));
    if (length <= 0.0f) {
        Vec3 zero = { 0.0f, 0.0f, 0.0f };
        return zero;
    }
    Vec3 result = { v.x / length, v.y / length, v.z / length };
    return result;
}

static inline Vec3 vec3_lerp(Vec3 a, Vec3 b, float t)
{
    return vec3_add(a, vec3_scale(vec3_sub(b, a), t));
}

static inline Quat quat_create(float w, float x, float y, float z)
{
    Quat result = { w, x, y, z };
    return result;
}

static inline float quat_dot(Quat a, Quat b)
{
    return a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z;
}

static inline Quat quat_normalize(Quat q)
{
    const float length = SDL_sqrtf(quat_dot(q, q));

    if (length <= 0.0f) {
        return quat_create(1.0f, 0.0f, 0.0f, 0.0f);
    }
    return quat_create(q.w / length, q.x / length, q.y / length, q.z / length);
}

static inline Quat quat_multiply(Quat a, Quat b)
{
    return quat_create(
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w);
}

static inline Quat quat_from_axis_angle(Vec3 axis, float angle)
{
    const Vec3 normalized = vec3_normalize(axis);
    const float half_angle = angle * 0.5f;
    const float s = SDL_sinf(half_angle);

    if (vec3_dot(normalized, normalized) <= 0.0f) {
        return quat_create(1.0f, 0.0f, 0.0f, 0.0f);
    }
    return quat_normalize(quat_create(
        SDL_cosf(half_angle),
        normalized.x * s,
        normalized.y * s,
        normalized.z * s));
}

static inline Quat quat_negate(Quat q)
{
    return quat_create(-q.w, -q.x, -q.y, -q.z);
}

static inline Quat quat_slerp(Quat a, Quat b, float t)
{
    float d;

    a = quat_normalize(a);
    b = quat_normalize(b);
    d = quat_dot(a, b);

    if (d < 0.0f) {
        b = quat_negate(b);
        d = -d;
    }
    if (d > 1.0f) {
        d = 1.0f;
    }

    if (d > 0.9995f) {
        return quat_normalize(quat_create(
            a.w + t * (b.w - a.w),
            a.x + t * (b.x - a.x),
            a.y + t * (b.y - a.y),
            a.z + t * (b.z - a.z)));
    }

    {
        const float theta = SDL_acosf(d);
        const float sin_theta = SDL_sinf(theta);
        const float wa = SDL_sinf((1.0f - t) * theta) / sin_theta;
        const float wb = SDL_sinf(t * theta) / sin_theta;

        return quat_create(
            wa * a.w + wb * b.w,
            wa * a.x + wb * b.x,
            wa * a.y + wb * b.y,
            wa * a.z + wb * b.z);
    }
}

static inline Mat4 quat_to_mat4(Quat q)
{
    const float xx = q.x * q.x;
    const float yy = q.y * q.y;
    const float zz = q.z * q.z;
    const float xy = q.x * q.y;
    const float xz = q.x * q.z;
    const float yz = q.y * q.z;
    const float wx = q.w * q.x;
    const float wy = q.w * q.y;
    const float wz = q.w * q.z;
    Mat4 result = mat4_identity();

    result.m[0] = 1.0f - 2.0f * (yy + zz);
    result.m[1] = 2.0f * (xy + wz);
    result.m[2] = 2.0f * (xz - wy);
    result.m[4] = 2.0f * (xy - wz);
    result.m[5] = 1.0f - 2.0f * (xx + zz);
    result.m[6] = 2.0f * (yz + wx);
    result.m[8] = 2.0f * (xz + wy);
    result.m[9] = 2.0f * (yz - wx);
    result.m[10] = 1.0f - 2.0f * (xx + yy);
    return result;
}

static inline Quat quat_from_mat4(Mat4 m)
{
    const float r00 = m.m[0];
    const float r11 = m.m[5];
    const float r22 = m.m[10];
    const float trace = r00 + r11 + r22;
    float w;
    float x;
    float y;
    float z;

    if (trace > 0.0f) {
        const float s = SDL_sqrtf(trace + 1.0f) * 2.0f;
        w = s * 0.25f;
        x = (m.m[6] - m.m[9]) / s;
        y = (m.m[8] - m.m[2]) / s;
        z = (m.m[1] - m.m[4]) / s;
    } else if (r00 > r11 && r00 > r22) {
        const float s = SDL_sqrtf(1.0f + r00 - r11 - r22) * 2.0f;
        w = (m.m[6] - m.m[9]) / s;
        x = s * 0.25f;
        y = (m.m[4] + m.m[1]) / s;
        z = (m.m[8] + m.m[2]) / s;
    } else if (r11 > r22) {
        const float s = SDL_sqrtf(1.0f + r11 - r00 - r22) * 2.0f;
        w = (m.m[8] - m.m[2]) / s;
        x = (m.m[4] + m.m[1]) / s;
        y = s * 0.25f;
        z = (m.m[9] + m.m[6]) / s;
    } else {
        const float s = SDL_sqrtf(1.0f + r22 - r00 - r11) * 2.0f;
        w = (m.m[1] - m.m[4]) / s;
        x = (m.m[8] + m.m[2]) / s;
        y = (m.m[9] + m.m[6]) / s;
        z = s * 0.25f;
    }
    return quat_create(w, x, y, z);
}

static inline Vec4 vec4_create(float x, float y, float z, float w)
{
    Vec4 result = { x, y, z, w };
    return result;
}

static inline Vec3 vec3_perspective_divide(Vec4 clip)
{
    const float inv_w = clip.w != 0.0f ? 1.0f / clip.w : 1.0f;
    Vec3 result = { clip.x * inv_w, clip.y * inv_w, clip.z * inv_w };
    return result;
}

static inline Quat quat_from_euler(float yaw, float pitch, float roll)
{
    const float cy = SDL_cosf(yaw * 0.5f);
    const float sy = SDL_sinf(yaw * 0.5f);
    const float cp = SDL_cosf(pitch * 0.5f);
    const float sp = SDL_sinf(pitch * 0.5f);
    const float cr = SDL_cosf(roll * 0.5f);
    const float sr = SDL_sinf(roll * 0.5f);
    Quat result = {
        cy * cp * cr + sy * sp * sr,
        cy * sp * cr + sy * cp * sr,
        sy * cp * cr - cy * sp * sr,
        cy * cp * sr - sy * sp * cr
    };
    return result;
}

static inline Vec3 quat_forward(Quat q)
{
    Vec3 result = {
        -(2.0f * (q.x * q.z + q.w * q.y)),
        -(2.0f * (q.y * q.z - q.w * q.x)),
        -(1.0f - 2.0f * (q.x * q.x + q.y * q.y))
    };
    return result;
}

static inline Vec3 quat_right(Quat q)
{
    Vec3 result = {
        1.0f - 2.0f * (q.y * q.y + q.z * q.z),
        2.0f * (q.x * q.y + q.w * q.z),
        2.0f * (q.x * q.z - q.w * q.y)
    };
    return result;
}

static inline Vec3 quat_up(Quat q)
{
    Vec3 result = {
        2.0f * (q.x * q.y - q.w * q.z),
        1.0f - 2.0f * (q.x * q.x + q.z * q.z),
        2.0f * (q.y * q.z + q.w * q.x)
    };
    return result;
}

static inline Mat4 mat4_view_from_quat(Vec3 position, Quat orientation)
{
    const Vec3 right = quat_right(orientation);
    const Vec3 up = quat_up(orientation);
    const Vec3 forward = quat_forward(orientation);
    Mat4 result = {
        {
            right.x,      up.x,     -forward.x, 0.0f,
            right.y,      up.y,     -forward.y, 0.0f,
            right.z,      up.z,     -forward.z, 0.0f,
            0.0f,         0.0f,      0.0f,      1.0f
        }
    };

    result.m[12] = -vec3_dot(right, position);
    result.m[13] = -vec3_dot(up, position);
    result.m[14] = vec3_dot(forward, position);
    return result;
}

static inline Mat4 mat4_look_at(Vec3 eye, Vec3 target, Vec3 up)
{
    const Vec3 forward = vec3_normalize(vec3_sub(target, eye));
    const Vec3 right = vec3_normalize(vec3_cross(forward, up));
    const Vec3 up_prime = vec3_cross(right, forward);
    Mat4 result = mat4_identity();

    result.m[0] = right.x;
    result.m[1] = up_prime.x;
    result.m[2] = -forward.x;
    result.m[4] = right.y;
    result.m[5] = up_prime.y;
    result.m[6] = -forward.y;
    result.m[8] = right.z;
    result.m[9] = up_prime.z;
    result.m[10] = -forward.z;
    result.m[12] = -vec3_dot(right, eye);
    result.m[13] = -vec3_dot(up_prime, eye);
    result.m[14] = vec3_dot(forward, eye);
    return result;
}

static inline Vec4 mat4_multiply_vec4(Mat4 m, Vec4 v)
{
    return vec4_create(
        m.m[0] * v.x + m.m[4] * v.y + m.m[8] * v.z + m.m[12] * v.w,
        m.m[1] * v.x + m.m[5] * v.y + m.m[9] * v.z + m.m[13] * v.w,
        m.m[2] * v.x + m.m[6] * v.y + m.m[10] * v.z + m.m[14] * v.w,
        m.m[3] * v.x + m.m[7] * v.y + m.m[11] * v.z + m.m[15] * v.w);
}

static inline Mat4 mat4_perspective(float fov_y_radians, float aspect, float near_plane, float far_plane)
{
    Mat4 result;
    const float tan_half_fov = SDL_tanf(fov_y_radians * 0.5f);

    SDL_zero(result);
    result.m[0] = 1.0f / (aspect * tan_half_fov);
    result.m[5] = 1.0f / tan_half_fov;
    result.m[10] = far_plane / (near_plane - far_plane);
    result.m[11] = -1.0f;
    result.m[14] = -(far_plane * near_plane) / (far_plane - near_plane);
    return result;
}

static inline Mat4 mat4_orthographic(float left, float right, float bottom, float top, float near_plane, float far_plane)
{
    Mat4 result;
    SDL_zero(result);
    result.m[0] = 2.0f / (right - left);
    result.m[5] = 2.0f / (top - bottom);
    result.m[10] = 1.0f / (near_plane - far_plane);
    result.m[12] = -(right + left) / (right - left);
    result.m[13] = -(top + bottom) / (top - bottom);
    result.m[14] = near_plane / (near_plane - far_plane);
    result.m[15] = 1.0f;
    return result;
}

static inline Mat4 mat4_inverse(Mat4 m)
{
    const float m0 = m.m[0], m1 = m.m[1], m2 = m.m[2], m3 = m.m[3];
    const float m4 = m.m[4], m5 = m.m[5], m6 = m.m[6], m7 = m.m[7];
    const float m8 = m.m[8], m9 = m.m[9], m10 = m.m[10], m11 = m.m[11];
    const float m12 = m.m[12], m13 = m.m[13], m14 = m.m[14], m15 = m.m[15];
    const float A = m10 * m15 - m11 * m14;
    const float B = m6 * m15 - m7 * m14;
    const float C = m6 * m11 - m7 * m10;
    const float D = m2 * m15 - m3 * m14;
    const float E = m2 * m11 - m3 * m10;
    const float F = m2 * m7 - m3 * m6;
    Mat4 result;
    float det;
    float inv_det;

    result.m[0] = m5 * A - m9 * B + m13 * C;
    result.m[1] = -m1 * A + m9 * D - m13 * E;
    result.m[2] = m1 * B - m5 * D + m13 * F;
    result.m[3] = -m1 * C + m5 * E - m9 * F;

    det = m0 * result.m[0] + m4 * result.m[1] + m8 * result.m[2] + m12 * result.m[3];
    if (det == 0.0f) {
        return mat4_identity();
    }

    result.m[4] = -m4 * A + m8 * B - m12 * C;
    result.m[5] = m0 * A - m8 * D + m12 * E;
    result.m[6] = -m0 * B + m4 * D - m12 * F;
    result.m[7] = m0 * C - m4 * E + m8 * F;

    {
        const float G = m9 * m15 - m11 * m13;
        const float H = m5 * m15 - m7 * m13;
        const float I = m5 * m11 - m7 * m9;
        const float J = m1 * m15 - m3 * m13;
        const float K = m1 * m11 - m3 * m9;
        const float L = m1 * m7 - m3 * m5;

        result.m[8] = m4 * G - m8 * H + m12 * I;
        result.m[9] = -m0 * G + m8 * J - m12 * K;
        result.m[10] = m0 * H - m4 * J + m12 * L;
        result.m[11] = -m0 * I + m4 * K - m8 * L;
    }

    {
        const float M = m9 * m14 - m10 * m13;
        const float N = m5 * m14 - m6 * m13;
        const float O = m5 * m10 - m6 * m9;
        const float P = m1 * m14 - m2 * m13;
        const float Q = m1 * m10 - m2 * m9;
        const float R = m1 * m6 - m2 * m5;

        result.m[12] = -m4 * M + m8 * N - m12 * O;
        result.m[13] = m0 * M - m8 * P + m12 * Q;
        result.m[14] = -m0 * N + m4 * P - m12 * R;
        result.m[15] = m0 * O - m4 * Q + m8 * R;
    }

    inv_det = 1.0f / det;
    for (int i = 0; i < 16; i += 1) {
        result.m[i] *= inv_det;
    }
    return result;
}

static inline Uint32 forge_gpu_hash_wang(Uint32 key)
{
    key = (key ^ 61u) ^ (key >> 16);
    key *= 9u;
    key ^= key >> 4;
    key *= 0x27d4eb2du;
    key ^= key >> 15;
    return key;
}

static inline float forge_gpu_hash_to_float(Uint32 h)
{
    return (float)(h >> 8) * (1.0f / 16777216.0f);
}

static inline float forge_gpu_hash_to_sfloat(Uint32 h)
{
    return forge_gpu_hash_to_float(h) * 2.0f - 1.0f;
}

static inline Uint32 forge_gpu_hash3d(Uint32 x, Uint32 y, Uint32 z)
{
    return forge_gpu_hash_wang(x ^ forge_gpu_hash_wang(y ^ forge_gpu_hash_wang(z)));
}

static inline float forge_gpu_noise_fade(float t)
{
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

static inline float forge_gpu_noise_grad2d(Uint32 hash, float dx, float dy)
{
    switch (hash & 3u) {
    case 0u:
        return dx + dy;
    case 1u:
        return -dx + dy;
    case 2u:
        return dx - dy;
    default:
        return -dx - dy;
    }
}

static inline float forge_gpu_noise_perlin2d(float x, float y, Uint32 seed)
{
    const int ix = (int)SDL_floorf(x);
    const int iy = (int)SDL_floorf(y);
    const float fx = x - (float)ix;
    const float fy = y - (float)iy;
    const float u = forge_gpu_noise_fade(fx);
    const float v = forge_gpu_noise_fade(fy);
    const Uint32 h00 = forge_gpu_hash3d((Uint32)ix, (Uint32)iy, seed);
    const Uint32 h10 = forge_gpu_hash3d((Uint32)(ix + 1), (Uint32)iy, seed);
    const Uint32 h01 = forge_gpu_hash3d((Uint32)ix, (Uint32)(iy + 1), seed);
    const Uint32 h11 = forge_gpu_hash3d((Uint32)(ix + 1), (Uint32)(iy + 1), seed);
    const float g00 = forge_gpu_noise_grad2d(h00, fx, fy);
    const float g10 = forge_gpu_noise_grad2d(h10, fx - 1.0f, fy);
    const float g01 = forge_gpu_noise_grad2d(h01, fx, fy - 1.0f);
    const float g11 = forge_gpu_noise_grad2d(h11, fx - 1.0f, fy - 1.0f);
    const float x0 = g00 + u * (g10 - g00);
    const float x1 = g01 + u * (g11 - g01);
    return x0 + v * (x1 - x0);
}

static inline float forge_gpu_noise_fbm2d(float x, float y, Uint32 seed, int octaves, float lacunarity, float persistence)
{
    float sum = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float max_amplitude = 0.0f;

    if (octaves <= 0) {
        return 0.0f;
    }

    for (int i = 0; i < octaves; i += 1) {
        sum += amplitude * forge_gpu_noise_perlin2d(x * frequency, y * frequency, seed + (Uint32)i);
        max_amplitude += amplitude;
        frequency *= lacunarity;
        amplitude *= persistence;
    }

    return sum / max_amplitude;
}

static inline void forge_gpu_generate_normalized_fbm_heightmap(
    float *out,
    int size,
    Uint32 seed,
    float frequency,
    int octaves,
    float lacunarity,
    float persistence,
    float range_min,
    float flat_range_fallback)
{
    float min_h = 1e30f;
    float max_h = -1e30f;

    for (int y = 0; y < size; y += 1) {
        for (int x = 0; x < size; x += 1) {
            const float nx = ((float)x / (float)(size - 1)) * frequency;
            const float ny = ((float)y / (float)(size - 1)) * frequency;
            const float h = forge_gpu_noise_fbm2d(
                nx,
                ny,
                seed,
                octaves,
                lacunarity,
                persistence);
            out[y * size + x] = h;
            min_h = SDL_min(min_h, h);
            max_h = SDL_max(max_h, h);
        }
    }

    {
        float range = max_h - min_h;
        if (range < range_min) {
            range = flat_range_fallback;
        }
        const float inv_range = 1.0f / range;

        for (int i = 0; i < size * size; i += 1) {
            out[i] = (out[i] - min_h) * inv_range;
        }
    }
}

#endif /* SDLGPU_FORGE_GPU_MATH_H */
