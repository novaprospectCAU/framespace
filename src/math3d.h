#pragma once

struct Vec3 {
  float x;
  float y;
  float z;
};

struct Mat4 {
  float m[16];
};

Vec3 vec3_add(Vec3 a, Vec3 b);
Vec3 vec3_sub(Vec3 a, Vec3 b);
Vec3 vec3_scale(Vec3 v, float s);
float vec3_dot(Vec3 a, Vec3 b);
Vec3 vec3_cross(Vec3 a, Vec3 b);
Vec3 vec3_normalize(Vec3 v);

Mat4 mat4_identity();
Mat4 mat4_mul(Mat4 a, Mat4 b);
Mat4 mat4_translation(Vec3 t);
Mat4 mat4_scale(float x, float y, float z);
Mat4 mat4_rotation_y(float rad);
Mat4 mat4_perspective_rh_zo(float fovy, float aspect, float z_near, float z_far);
Mat4 mat4_look_at_rh(Vec3 eye, Vec3 target, Vec3 up);
