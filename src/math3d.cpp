#include "math3d.h"

#include <cmath>

Vec3 vec3_add(Vec3 a, Vec3 b) {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vec3 vec3_sub(Vec3 a, Vec3 b) {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3 vec3_scale(Vec3 v, float s) {
  return {v.x * s, v.y * s, v.z * s};
}

float vec3_dot(Vec3 a, Vec3 b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3 vec3_cross(Vec3 a, Vec3 b) {
  return {
      a.y * b.z - a.z * b.y,
      a.z * b.x - a.x * b.z,
      a.x * b.y - a.y * b.x,
  };
}

Vec3 vec3_normalize(Vec3 v) {
  const float len = std::sqrt(vec3_dot(v, v));
  if (len < 1e-6f) {
    return {0.0f, 0.0f, 0.0f};
  }
  return {v.x / len, v.y / len, v.z / len};
}

Mat4 mat4_identity() {
  Mat4 out{};
  out.m[0] = 1.0f;
  out.m[5] = 1.0f;
  out.m[10] = 1.0f;
  out.m[15] = 1.0f;
  return out;
}

Mat4 mat4_mul(Mat4 a, Mat4 b) {
  Mat4 out{};
  for (int c = 0; c < 4; ++c) {
    for (int r = 0; r < 4; ++r) {
      out.m[c * 4 + r] =
          a.m[0 * 4 + r] * b.m[c * 4 + 0] +
          a.m[1 * 4 + r] * b.m[c * 4 + 1] +
          a.m[2 * 4 + r] * b.m[c * 4 + 2] +
          a.m[3 * 4 + r] * b.m[c * 4 + 3];
    }
  }
  return out;
}

Mat4 mat4_translation(Vec3 t) {
  Mat4 out = mat4_identity();
  out.m[12] = t.x;
  out.m[13] = t.y;
  out.m[14] = t.z;
  return out;
}

Mat4 mat4_scale(float x, float y, float z) {
  Mat4 out{};
  out.m[0] = x;
  out.m[5] = y;
  out.m[10] = z;
  out.m[15] = 1.0f;
  return out;
}

Mat4 mat4_rotation_y(float rad) {
  Mat4 out = mat4_identity();
  const float c = std::cos(rad);
  const float s = std::sin(rad);
  out.m[0] = c;
  out.m[2] = -s;
  out.m[8] = s;
  out.m[10] = c;
  return out;
}

Mat4 mat4_perspective_rh_zo(float fovy, float aspect, float z_near, float z_far) {
  Mat4 out{};
  const float f = 1.0f / std::tan(fovy * 0.5f);
  out.m[0] = f / aspect;
  out.m[5] = f;
  out.m[10] = z_far / (z_near - z_far);
  out.m[11] = -1.0f;
  out.m[14] = (z_far * z_near) / (z_near - z_far);
  return out;
}

Mat4 mat4_look_at_rh(Vec3 eye, Vec3 target, Vec3 up) {
  const Vec3 f = vec3_normalize(vec3_sub(target, eye));
  const Vec3 s = vec3_normalize(vec3_cross(f, up));
  const Vec3 u = vec3_cross(s, f);

  Mat4 out = mat4_identity();
  out.m[0] = s.x;
  out.m[1] = s.y;
  out.m[2] = s.z;
  out.m[4] = u.x;
  out.m[5] = u.y;
  out.m[6] = u.z;
  out.m[8] = -f.x;
  out.m[9] = -f.y;
  out.m[10] = -f.z;
  out.m[12] = -vec3_dot(s, eye);
  out.m[13] = -vec3_dot(u, eye);
  out.m[14] = vec3_dot(f, eye);
  return out;
}
