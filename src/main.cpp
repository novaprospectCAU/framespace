#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <emscripten.h>
#include <emscripten/html5.h>
#include <webgpu/webgpu.h>

#include "math3d.h"

namespace {

struct Vertex {
  float px;
  float py;
  float pz;
  float r;
  float g;
  float b;
};

struct Uniforms {
  float mvp[16];
  float tint[4];
};

struct PhotoSnapshot {
  uint32_t id;
  Vec3 position;
  float yaw;
  float pitch;
  double timestamp_ms;
};

struct PlacedPhoto {
  bool active;
  uint32_t shot_id;
  Vec3 position;
  float yaw;
  float scale;
};

constexpr int kMaxPlacedPhotos = 64;

constexpr Vertex kCubeVertices[] = {
    {-1.0f, -1.0f, -1.0f, 0.96f, 0.36f, 0.31f},
    {1.0f, -1.0f, -1.0f, 0.98f, 0.69f, 0.26f},
    {1.0f, 1.0f, -1.0f, 0.98f, 0.91f, 0.37f},
    {-1.0f, 1.0f, -1.0f, 0.64f, 0.90f, 0.39f},
    {-1.0f, -1.0f, 1.0f, 0.33f, 0.80f, 0.93f},
    {1.0f, -1.0f, 1.0f, 0.45f, 0.58f, 0.97f},
    {1.0f, 1.0f, 1.0f, 0.76f, 0.48f, 0.94f},
    {-1.0f, 1.0f, 1.0f, 0.92f, 0.44f, 0.82f},
};

constexpr uint16_t kCubeIndices[] = {
    0, 1, 2, 2, 3, 0,
    1, 5, 6, 6, 2, 1,
    5, 4, 7, 7, 6, 5,
    4, 0, 3, 3, 7, 4,
    3, 2, 6, 6, 7, 3,
    4, 5, 1, 1, 0, 4,
};

constexpr Vertex kPhotoFrameVertices[] = {
    {-0.75f, -0.50f, 0.0f, 1.0f, 1.0f, 1.0f},
    {0.75f, -0.50f, 0.0f, 1.0f, 1.0f, 1.0f},
    {0.75f, 0.50f, 0.0f, 1.0f, 1.0f, 1.0f},
    {-0.75f, 0.50f, 0.0f, 1.0f, 1.0f, 1.0f},
};

constexpr uint16_t kPhotoFrameIndices[] = {
    0, 1, 2, 2, 3, 0,
};

constexpr char kShaderWGSL[] = R"(
struct Uniforms {
  mvp : mat4x4<f32>,
  tint : vec4<f32>,
};

@group(0) @binding(0)
var<uniform> ubo : Uniforms;

struct VSIn {
  @location(0) position : vec3<f32>,
  @location(1) color : vec3<f32>,
};

struct VSOut {
  @builtin(position) pos : vec4<f32>,
  @location(0) color : vec3<f32>,
};

@vertex
fn vs_main(in : VSIn) -> VSOut {
  var out : VSOut;
  out.pos = ubo.mvp * vec4<f32>(in.position, 1.0);
  out.color = in.color * ubo.tint.rgb;
  return out;
}

@fragment
fn fs_main(in : VSOut) -> @location(0) vec4<f32> {
  return vec4<f32>(in.color, 1.0);
}
)";

WGPUInstance g_instance = nullptr;
WGPUDevice g_device = nullptr;
WGPUQueue g_queue = nullptr;
WGPUAdapter g_adapter = nullptr;
WGPUSurface g_surface = nullptr;
WGPUTextureFormat g_surface_format = WGPUTextureFormat_BGRA8Unorm;
WGPUSurfaceConfiguration g_surface_config = WGPU_SURFACE_CONFIGURATION_INIT;

WGPUBuffer g_cube_vertex_buffer = nullptr;
WGPUBuffer g_cube_index_buffer = nullptr;
WGPUBuffer g_photo_vertex_buffer = nullptr;
WGPUBuffer g_photo_index_buffer = nullptr;
WGPUBuffer g_uniform_buffer = nullptr;
WGPUBindGroupLayout g_bind_group_layout = nullptr;
WGPUBindGroup g_bind_group = nullptr;
WGPUPipelineLayout g_pipeline_layout = nullptr;
WGPURenderPipeline g_pipeline = nullptr;
WGPUTexture g_depth_texture = nullptr;
WGPUTextureView g_depth_view = nullptr;

int g_canvas_width = 1280;
int g_canvas_height = 720;

double g_last_time_ms = 0.0;
float g_accum_time = 0.0f;

Vec3 g_camera_pos{0.0f, 1.2f, 4.0f};
float g_camera_yaw = -1.5707963f;
float g_camera_pitch = 0.0f;

bool g_key_w = false;
bool g_key_a = false;
bool g_key_s = false;
bool g_key_d = false;
bool g_key_shift = false;

uint32_t g_photo_capture_count = 0;
PhotoSnapshot g_last_snapshot{};
bool g_has_snapshot = false;
PlacedPhoto g_placed_photos[kMaxPlacedPhotos]{};

Mat4 g_last_vp{};

bool g_initialized = false;

WGPUStringView make_str_view(const char* s) {
  WGPUStringView v = WGPU_STRING_VIEW_INIT;
  v.data = s;
  v.length = WGPU_STRLEN;
  return v;
}

Vec3 camera_forward() {
  const float cp = std::cos(g_camera_pitch);
  return vec3_normalize({
      std::cos(g_camera_yaw) * cp,
      std::sin(g_camera_pitch),
      std::sin(g_camera_yaw) * cp,
  });
}

void create_depth_buffer() {
  if (g_depth_view) {
    wgpuTextureViewRelease(g_depth_view);
    g_depth_view = nullptr;
  }
  if (g_depth_texture) {
    wgpuTextureRelease(g_depth_texture);
    g_depth_texture = nullptr;
  }

  WGPUTextureDescriptor depth_desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
  depth_desc.label = make_str_view("depth_texture");
  depth_desc.usage = WGPUTextureUsage_RenderAttachment;
  depth_desc.dimension = WGPUTextureDimension_2D;
  depth_desc.size.width = static_cast<uint32_t>(g_canvas_width);
  depth_desc.size.height = static_cast<uint32_t>(g_canvas_height);
  depth_desc.size.depthOrArrayLayers = 1;
  depth_desc.format = WGPUTextureFormat_Depth24Plus;
  depth_desc.mipLevelCount = 1;
  depth_desc.sampleCount = 1;

  g_depth_texture = wgpuDeviceCreateTexture(g_device, &depth_desc);
  g_depth_view = wgpuTextureCreateView(g_depth_texture, nullptr);
}

void recreate_surface_if_needed() {
  int width = 0;
  int height = 0;
  emscripten_get_canvas_element_size("#canvas", &width, &height);
  if (width <= 0 || height <= 0) {
    return;
  }

  if (width == g_canvas_width && height == g_canvas_height) {
    return;
  }

  g_canvas_width = width;
  g_canvas_height = height;
  g_surface_config.width = static_cast<uint32_t>(g_canvas_width);
  g_surface_config.height = static_cast<uint32_t>(g_canvas_height);
  wgpuSurfaceConfigure(g_surface, &g_surface_config);
  create_depth_buffer();
}

WGPUTextureFormat choose_surface_format(WGPUSurface surface, WGPUAdapter adapter) {
  WGPUSurfaceCapabilities caps = WGPU_SURFACE_CAPABILITIES_INIT;
  if (wgpuSurfaceGetCapabilities(surface, adapter, &caps) != WGPUStatus_Success || caps.formatCount == 0) {
    return WGPUTextureFormat_BGRA8Unorm;
  }

  WGPUTextureFormat chosen = caps.formats[0];
  for (size_t i = 0; i < caps.formatCount; ++i) {
    if (caps.formats[i] == WGPUTextureFormat_BGRA8Unorm) {
      chosen = WGPUTextureFormat_BGRA8Unorm;
      break;
    }
  }

  wgpuSurfaceCapabilitiesFreeMembers(caps);
  return chosen;
}

void create_pipeline_resources() {
  WGPUBufferDescriptor vb_desc = WGPU_BUFFER_DESCRIPTOR_INIT;
  vb_desc.label = make_str_view("cube_vertex_buffer");
  vb_desc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
  vb_desc.size = sizeof(kCubeVertices);
  g_cube_vertex_buffer = wgpuDeviceCreateBuffer(g_device, &vb_desc);
  wgpuQueueWriteBuffer(g_queue, g_cube_vertex_buffer, 0, kCubeVertices, sizeof(kCubeVertices));

  WGPUBufferDescriptor ib_desc = WGPU_BUFFER_DESCRIPTOR_INIT;
  ib_desc.label = make_str_view("cube_index_buffer");
  ib_desc.usage = WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst;
  ib_desc.size = sizeof(kCubeIndices);
  g_cube_index_buffer = wgpuDeviceCreateBuffer(g_device, &ib_desc);
  wgpuQueueWriteBuffer(g_queue, g_cube_index_buffer, 0, kCubeIndices, sizeof(kCubeIndices));

  WGPUBufferDescriptor pvb_desc = WGPU_BUFFER_DESCRIPTOR_INIT;
  pvb_desc.label = make_str_view("photo_vertex_buffer");
  pvb_desc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
  pvb_desc.size = sizeof(kPhotoFrameVertices);
  g_photo_vertex_buffer = wgpuDeviceCreateBuffer(g_device, &pvb_desc);
  wgpuQueueWriteBuffer(g_queue, g_photo_vertex_buffer, 0, kPhotoFrameVertices, sizeof(kPhotoFrameVertices));

  WGPUBufferDescriptor pib_desc = WGPU_BUFFER_DESCRIPTOR_INIT;
  pib_desc.label = make_str_view("photo_index_buffer");
  pib_desc.usage = WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst;
  pib_desc.size = sizeof(kPhotoFrameIndices);
  g_photo_index_buffer = wgpuDeviceCreateBuffer(g_device, &pib_desc);
  wgpuQueueWriteBuffer(g_queue, g_photo_index_buffer, 0, kPhotoFrameIndices, sizeof(kPhotoFrameIndices));

  WGPUBufferDescriptor ub_desc = WGPU_BUFFER_DESCRIPTOR_INIT;
  ub_desc.label = make_str_view("camera_uniform_buffer");
  ub_desc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
  ub_desc.size = sizeof(Uniforms);
  g_uniform_buffer = wgpuDeviceCreateBuffer(g_device, &ub_desc);

  WGPUBindGroupLayoutEntry bgl_entry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
  bgl_entry.binding = 0;
  bgl_entry.visibility = WGPUShaderStage_Vertex;
  bgl_entry.buffer = WGPU_BUFFER_BINDING_LAYOUT_INIT;
  bgl_entry.buffer.type = WGPUBufferBindingType_Uniform;
  bgl_entry.buffer.minBindingSize = sizeof(Uniforms);

  WGPUBindGroupLayoutDescriptor bgl_desc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
  bgl_desc.label = make_str_view("camera_bgl");
  bgl_desc.entryCount = 1;
  bgl_desc.entries = &bgl_entry;
  g_bind_group_layout = wgpuDeviceCreateBindGroupLayout(g_device, &bgl_desc);

  WGPUPipelineLayoutDescriptor pl_desc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
  pl_desc.label = make_str_view("main_pipeline_layout");
  pl_desc.bindGroupLayoutCount = 1;
  pl_desc.bindGroupLayouts = &g_bind_group_layout;
  g_pipeline_layout = wgpuDeviceCreatePipelineLayout(g_device, &pl_desc);

  WGPUBindGroupEntry bg_entry = WGPU_BIND_GROUP_ENTRY_INIT;
  bg_entry.binding = 0;
  bg_entry.buffer = g_uniform_buffer;
  bg_entry.offset = 0;
  bg_entry.size = sizeof(Uniforms);

  WGPUBindGroupDescriptor bg_desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
  bg_desc.label = make_str_view("camera_bg");
  bg_desc.layout = g_bind_group_layout;
  bg_desc.entryCount = 1;
  bg_desc.entries = &bg_entry;
  g_bind_group = wgpuDeviceCreateBindGroup(g_device, &bg_desc);

  WGPUShaderSourceWGSL wgsl_desc = WGPU_SHADER_SOURCE_WGSL_INIT;
  wgsl_desc.code = make_str_view(kShaderWGSL);

  WGPUShaderModuleDescriptor shader_desc = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
  shader_desc.label = make_str_view("main_shader");
  shader_desc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&wgsl_desc);

  WGPUShaderModule shader = wgpuDeviceCreateShaderModule(g_device, &shader_desc);

  WGPUVertexAttribute attrs[2] = {WGPU_VERTEX_ATTRIBUTE_INIT, WGPU_VERTEX_ATTRIBUTE_INIT};
  attrs[0].format = WGPUVertexFormat_Float32x3;
  attrs[0].offset = 0;
  attrs[0].shaderLocation = 0;
  attrs[1].format = WGPUVertexFormat_Float32x3;
  attrs[1].offset = 3 * sizeof(float);
  attrs[1].shaderLocation = 1;

  WGPUVertexBufferLayout vbuf_layout = WGPU_VERTEX_BUFFER_LAYOUT_INIT;
  vbuf_layout.arrayStride = sizeof(Vertex);
  vbuf_layout.stepMode = WGPUVertexStepMode_Vertex;
  vbuf_layout.attributeCount = 2;
  vbuf_layout.attributes = attrs;

  WGPUColorTargetState color_target = WGPU_COLOR_TARGET_STATE_INIT;
  color_target.format = g_surface_format;
  color_target.writeMask = WGPUColorWriteMask_All;

  WGPUFragmentState frag_state = WGPU_FRAGMENT_STATE_INIT;
  frag_state.module = shader;
  frag_state.entryPoint = make_str_view("fs_main");
  frag_state.targetCount = 1;
  frag_state.targets = &color_target;

  WGPUDepthStencilState depth_state = WGPU_DEPTH_STENCIL_STATE_INIT;
  depth_state.format = WGPUTextureFormat_Depth24Plus;
  depth_state.depthWriteEnabled = WGPUOptionalBool_True;
  depth_state.depthCompare = WGPUCompareFunction_Less;

  WGPURenderPipelineDescriptor pipe_desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
  pipe_desc.label = make_str_view("main_pipeline");
  pipe_desc.layout = g_pipeline_layout;
  pipe_desc.vertex.module = shader;
  pipe_desc.vertex.entryPoint = make_str_view("vs_main");
  pipe_desc.vertex.bufferCount = 1;
  pipe_desc.vertex.buffers = &vbuf_layout;
  pipe_desc.primitive = WGPU_PRIMITIVE_STATE_INIT;
  pipe_desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
  pipe_desc.primitive.frontFace = WGPUFrontFace_CCW;
  pipe_desc.primitive.cullMode = WGPUCullMode_None;
  pipe_desc.multisample = WGPU_MULTISAMPLE_STATE_INIT;
  pipe_desc.multisample.count = 1;
  pipe_desc.depthStencil = &depth_state;
  pipe_desc.fragment = &frag_state;

  g_pipeline = wgpuDeviceCreateRenderPipeline(g_device, &pipe_desc);
  wgpuShaderModuleRelease(shader);

  create_depth_buffer();
}

void update_camera(float dt_sec) {
  const Vec3 forward = camera_forward();
  const Vec3 right = vec3_normalize(vec3_cross(forward, Vec3{0.0f, 1.0f, 0.0f}));

  Vec3 move{0.0f, 0.0f, 0.0f};
  if (g_key_w) move = vec3_add(move, forward);
  if (g_key_s) move = vec3_sub(move, forward);
  if (g_key_d) move = vec3_add(move, right);
  if (g_key_a) move = vec3_sub(move, right);

  if (vec3_dot(move, move) > 0.0f) {
    move = vec3_normalize(move);
  }

  const float speed = g_key_shift ? 7.0f : 3.5f;
  g_camera_pos = vec3_add(g_camera_pos, vec3_scale(move, speed * dt_sec));
}

void update_view_projection() {
  const float aspect = static_cast<float>(g_canvas_width) / static_cast<float>(g_canvas_height);
  const Mat4 proj = mat4_perspective_rh_zo(60.0f * 3.14159265f / 180.0f, aspect, 0.1f, 200.0f);
  const Vec3 fwd = camera_forward();
  const Mat4 view = mat4_look_at_rh(g_camera_pos, vec3_add(g_camera_pos, fwd), Vec3{0.0f, 1.0f, 0.0f});
  g_last_vp = mat4_mul(proj, view);
}

void write_uniform(Mat4 model, float tr, float tg, float tb) {
  const Mat4 mvp = mat4_mul(g_last_vp, model);
  Uniforms u{};
  std::memcpy(u.mvp, mvp.m, sizeof(mvp.m));
  u.tint[0] = tr;
  u.tint[1] = tg;
  u.tint[2] = tb;
  u.tint[3] = 1.0f;
  wgpuQueueWriteBuffer(g_queue, g_uniform_buffer, 0, &u, sizeof(u));
}

void draw_mesh(WGPURenderPassEncoder pass,
               WGPUBuffer vertex_buffer,
               size_t vertex_size,
               WGPUBuffer index_buffer,
               size_t index_size,
               uint32_t index_count,
               Mat4 model,
               float tr,
               float tg,
               float tb) {
  write_uniform(model, tr, tg, tb);
  wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vertex_buffer, 0, vertex_size);
  wgpuRenderPassEncoderSetIndexBuffer(pass, index_buffer, WGPUIndexFormat_Uint16, 0, index_size);
  wgpuRenderPassEncoderDrawIndexed(pass, index_count, 1, 0, 0, 0);
}

Vec3 shot_tint(uint32_t shot_id) {
  const float t = static_cast<float>(shot_id) * 0.37f;
  return {
      0.55f + 0.45f * std::sin(t + 0.0f),
      0.55f + 0.45f * std::sin(t + 2.1f),
      0.55f + 0.45f * std::sin(t + 4.2f),
  };
}

void capture_photo_snapshot() {
  g_photo_capture_count += 1;
  g_last_snapshot.id = g_photo_capture_count;
  g_last_snapshot.position = g_camera_pos;
  g_last_snapshot.yaw = g_camera_yaw;
  g_last_snapshot.pitch = g_camera_pitch;
  g_last_snapshot.timestamp_ms = emscripten_get_now();
  g_has_snapshot = true;

  std::fprintf(stdout, "[Capture] shot=%u pos=(%.2f, %.2f, %.2f) yaw=%.2f pitch=%.2f\n",
               g_last_snapshot.id,
               g_last_snapshot.position.x,
               g_last_snapshot.position.y,
               g_last_snapshot.position.z,
               g_last_snapshot.yaw,
               g_last_snapshot.pitch);

  EM_ASM({
    const shotId = $0;
    if (window.__framespaceAddSnapshotFromCanvas) {
      window.__framespaceAddSnapshotFromCanvas(shotId);
    }
  }, static_cast<int>(g_last_snapshot.id));
}

void place_selected_snapshot() {
  const int selected_shot = EM_ASM_INT({
    return window.__framespaceGetSelectedShotId ? window.__framespaceGetSelectedShotId() : 0;
  });
  if (selected_shot <= 0) {
    std::fprintf(stdout, "[Place] skipped: no selected snapshot\n");
    return;
  }
  const int shot_exists = EM_ASM_INT({
    if (!window.__framespaceHasShotId) return 0;
    return window.__framespaceHasShotId($0) ? 1 : 0;
  }, selected_shot);
  if (!shot_exists) {
    std::fprintf(stdout, "[Place] skipped: selected snapshot no longer exists (id=%d)\n", selected_shot);
    return;
  }

  int free_slot = -1;
  for (int i = 0; i < kMaxPlacedPhotos; ++i) {
    if (!g_placed_photos[i].active) {
      free_slot = i;
      break;
    }
  }
  if (free_slot < 0) {
    std::fprintf(stdout, "[Place] skipped: photo slots are full\n");
    return;
  }

  const Vec3 forward = camera_forward();
  const Vec3 pos = vec3_add(g_camera_pos, vec3_scale(forward, 2.8f));

  g_placed_photos[free_slot].active = true;
  g_placed_photos[free_slot].shot_id = static_cast<uint32_t>(selected_shot);
  g_placed_photos[free_slot].position = pos;
  g_placed_photos[free_slot].yaw = g_camera_yaw + 3.14159265f;
  g_placed_photos[free_slot].scale = 1.0f;

  std::fprintf(stdout, "[Place] shot=%u slot=%d pos=(%.2f, %.2f, %.2f)\n",
               g_placed_photos[free_slot].shot_id,
               free_slot,
               g_placed_photos[free_slot].position.x,
               g_placed_photos[free_slot].position.y,
               g_placed_photos[free_slot].position.z);
}

extern "C" {
EMSCRIPTEN_KEEPALIVE void framespace_trigger_capture() {
  capture_photo_snapshot();
}

EMSCRIPTEN_KEEPALIVE void framespace_trigger_place() {
  place_selected_snapshot();
}
}

EM_BOOL on_key_down(int, const EmscriptenKeyboardEvent* e, void*) {
  if (std::strcmp(e->code, "KeyW") == 0) g_key_w = true;
  if (std::strcmp(e->code, "KeyA") == 0) g_key_a = true;
  if (std::strcmp(e->code, "KeyS") == 0) g_key_s = true;
  if (std::strcmp(e->code, "KeyD") == 0) g_key_d = true;
  if (std::strcmp(e->code, "ShiftLeft") == 0 || std::strcmp(e->code, "ShiftRight") == 0) g_key_shift = true;
  if (std::strcmp(e->code, "KeyP") == 0 && !e->repeat) capture_photo_snapshot();
  if (std::strcmp(e->code, "KeyE") == 0 && !e->repeat) place_selected_snapshot();
  return EM_TRUE;
}

EM_BOOL on_key_up(int, const EmscriptenKeyboardEvent* e, void*) {
  if (std::strcmp(e->code, "KeyW") == 0) g_key_w = false;
  if (std::strcmp(e->code, "KeyA") == 0) g_key_a = false;
  if (std::strcmp(e->code, "KeyS") == 0) g_key_s = false;
  if (std::strcmp(e->code, "KeyD") == 0) g_key_d = false;
  if (std::strcmp(e->code, "ShiftLeft") == 0 || std::strcmp(e->code, "ShiftRight") == 0) g_key_shift = false;
  return EM_TRUE;
}

EM_BOOL on_mouse_move(int, const EmscriptenMouseEvent* e, void*) {
  EmscriptenPointerlockChangeEvent lock_status{};
  if (!emscripten_get_pointerlock_status(&lock_status) || !lock_status.isActive) {
    return EM_TRUE;
  }

  const float sens = 0.0025f;
  g_camera_yaw += static_cast<float>(e->movementX) * sens;
  g_camera_pitch -= static_cast<float>(e->movementY) * sens;

  const float limit = 1.553343f;
  if (g_camera_pitch > limit) g_camera_pitch = limit;
  if (g_camera_pitch < -limit) g_camera_pitch = -limit;
  return EM_TRUE;
}

EM_BOOL on_click(int, const EmscriptenMouseEvent*, void*) {
  emscripten_request_pointerlock("#canvas", false);
  return EM_TRUE;
}

void register_input_callbacks() {
  emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, true, on_key_down);
  emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, true, on_key_up);
  emscripten_set_mousemove_callback("#canvas", nullptr, true, on_mouse_move);
  emscripten_set_click_callback("#canvas", nullptr, true, on_click);
}

void frame() {
  if (!g_initialized) {
    return;
  }

  const double now_ms = emscripten_get_now();
  float dt_sec = 1.0f / 60.0f;
  if (g_last_time_ms > 0.0) {
    dt_sec = static_cast<float>((now_ms - g_last_time_ms) * 0.001);
  }
  if (dt_sec > 0.05f) {
    dt_sec = 0.05f;
  }
  g_last_time_ms = now_ms;
  g_accum_time += dt_sec;

  recreate_surface_if_needed();
  update_camera(dt_sec);
  update_view_projection();

  WGPUSurfaceTexture surface_texture = WGPU_SURFACE_TEXTURE_INIT;
  wgpuSurfaceGetCurrentTexture(g_surface, &surface_texture);
  if (surface_texture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
      surface_texture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) {
    return;
  }

  WGPUTextureView color_view = wgpuTextureCreateView(surface_texture.texture, nullptr);

  WGPUCommandEncoderDescriptor encoder_desc = WGPU_COMMAND_ENCODER_DESCRIPTOR_INIT;
  encoder_desc.label = make_str_view("frame_encoder");
  WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(g_device, &encoder_desc);

  WGPURenderPassColorAttachment color_attachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
  color_attachment.view = color_view;
  color_attachment.loadOp = WGPULoadOp_Clear;
  color_attachment.storeOp = WGPUStoreOp_Store;
  color_attachment.clearValue = WGPUColor{0.06, 0.08, 0.11, 1.0};

  WGPURenderPassDepthStencilAttachment depth_attachment = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
  depth_attachment.view = g_depth_view;
  depth_attachment.depthLoadOp = WGPULoadOp_Clear;
  depth_attachment.depthStoreOp = WGPUStoreOp_Store;
  depth_attachment.depthClearValue = 1.0f;
  depth_attachment.depthReadOnly = WGPU_FALSE;

  WGPURenderPassDescriptor pass_desc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
  pass_desc.colorAttachmentCount = 1;
  pass_desc.colorAttachments = &color_attachment;
  pass_desc.depthStencilAttachment = &depth_attachment;

  WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &pass_desc);
  wgpuRenderPassEncoderSetPipeline(pass, g_pipeline);
  wgpuRenderPassEncoderSetBindGroup(pass, 0, g_bind_group, 0, nullptr);

  const Mat4 cube_model = mat4_rotation_y(g_accum_time * 0.7f);
  draw_mesh(pass,
            g_cube_vertex_buffer,
            sizeof(kCubeVertices),
            g_cube_index_buffer,
            sizeof(kCubeIndices),
            static_cast<uint32_t>(sizeof(kCubeIndices) / sizeof(kCubeIndices[0])),
            cube_model,
            1.0f,
            1.0f,
            1.0f);

  for (int i = 0; i < kMaxPlacedPhotos; ++i) {
    if (!g_placed_photos[i].active) {
      continue;
    }

    const Mat4 t = mat4_translation(g_placed_photos[i].position);
    const Mat4 r = mat4_rotation_y(g_placed_photos[i].yaw);
    const Mat4 s = mat4_scale(g_placed_photos[i].scale, g_placed_photos[i].scale, 1.0f);
    const Mat4 model = mat4_mul(t, mat4_mul(r, s));
    const Vec3 tint = shot_tint(g_placed_photos[i].shot_id);

    draw_mesh(pass,
              g_photo_vertex_buffer,
              sizeof(kPhotoFrameVertices),
              g_photo_index_buffer,
              sizeof(kPhotoFrameIndices),
              static_cast<uint32_t>(sizeof(kPhotoFrameIndices) / sizeof(kPhotoFrameIndices[0])),
              model,
              tint.x,
              tint.y,
              tint.z);
  }

  wgpuRenderPassEncoderEnd(pass);
  wgpuRenderPassEncoderRelease(pass);

  WGPUCommandBufferDescriptor cmd_desc = WGPU_COMMAND_BUFFER_DESCRIPTOR_INIT;
  cmd_desc.label = make_str_view("frame_cmd");
  WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, &cmd_desc);
  wgpuQueueSubmit(g_queue, 1, &cmd);

  wgpuCommandBufferRelease(cmd);
  wgpuCommandEncoderRelease(encoder);
  wgpuTextureViewRelease(color_view);
  wgpuTextureRelease(surface_texture.texture);
}

void request_device_callback(WGPURequestDeviceStatus status,
                             WGPUDevice device,
                             WGPUStringView message,
                             void*,
                             void*) {
  if (status != WGPURequestDeviceStatus_Success || device == nullptr) {
    std::fprintf(stderr, "Failed to request WebGPU device: %s\n", message.data ? message.data : "");
    return;
  }

  g_device = device;
  g_queue = wgpuDeviceGetQueue(g_device);

  emscripten_set_canvas_element_size("#canvas", g_canvas_width, g_canvas_height);

  g_surface_format = choose_surface_format(g_surface, g_adapter);
  g_surface_config.device = g_device;
  g_surface_config.format = g_surface_format;
  g_surface_config.usage = WGPUTextureUsage_RenderAttachment;
  g_surface_config.width = static_cast<uint32_t>(g_canvas_width);
  g_surface_config.height = static_cast<uint32_t>(g_canvas_height);
  g_surface_config.presentMode = WGPUPresentMode_Fifo;
  g_surface_config.alphaMode = WGPUCompositeAlphaMode_Auto;
  wgpuSurfaceConfigure(g_surface, &g_surface_config);

  create_pipeline_resources();
  register_input_callbacks();

  g_initialized = true;
  emscripten_set_main_loop(frame, 0, true);
}

void request_adapter_callback(WGPURequestAdapterStatus status,
                              WGPUAdapter adapter,
                              WGPUStringView message,
                              void*,
                              void*) {
  if (status != WGPURequestAdapterStatus_Success || adapter == nullptr) {
    std::fprintf(stderr, "Failed to request WebGPU adapter: %s\n", message.data ? message.data : "");
    return;
  }

  g_adapter = adapter;

  WGPUDeviceDescriptor device_desc = WGPU_DEVICE_DESCRIPTOR_INIT;
  device_desc.label = make_str_view("framespace_device");

  WGPURequestDeviceCallbackInfo cb = WGPU_REQUEST_DEVICE_CALLBACK_INFO_INIT;
  cb.mode = WGPUCallbackMode_AllowSpontaneous;
  cb.callback = request_device_callback;

  wgpuAdapterRequestDevice(adapter, &device_desc, cb);
}

bool init_webgpu() {
  g_instance = wgpuCreateInstance(nullptr);
  if (!g_instance) {
    std::fprintf(stderr, "Failed to create WebGPU instance\n");
    return false;
  }

  WGPUEmscriptenSurfaceSourceCanvasHTMLSelector canvas_source =
      WGPU_EMSCRIPTEN_SURFACE_SOURCE_CANVAS_HTML_SELECTOR_INIT;
  canvas_source.selector = make_str_view("#canvas");

  WGPUSurfaceDescriptor surface_desc = WGPU_SURFACE_DESCRIPTOR_INIT;
  surface_desc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&canvas_source);

  g_surface = wgpuInstanceCreateSurface(g_instance, &surface_desc);
  if (!g_surface) {
    std::fprintf(stderr, "Failed to create WebGPU surface\n");
    return false;
  }

  WGPURequestAdapterOptions adapter_opts = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
  adapter_opts.compatibleSurface = g_surface;
  adapter_opts.powerPreference = WGPUPowerPreference_HighPerformance;

  WGPURequestAdapterCallbackInfo cb = WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT;
  cb.mode = WGPUCallbackMode_AllowSpontaneous;
  cb.callback = request_adapter_callback;

  wgpuInstanceRequestAdapter(g_instance, &adapter_opts, cb);
  return true;
}

}  // namespace

int main() {
  if (!init_webgpu()) {
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
