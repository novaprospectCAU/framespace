#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <emscripten.h>
#include <webgpu/webgpu.h>

namespace {

WGPUInstance g_instance = nullptr;
WGPUDevice g_device = nullptr;
WGPUQueue g_queue = nullptr;
WGPUAdapter g_adapter = nullptr;
WGPUSurface g_surface = nullptr;
WGPUSurfaceConfiguration g_surface_config = WGPU_SURFACE_CONFIGURATION_INIT;
bool g_initialized = false;

WGPUStringView make_str_view(const char* s) {
  WGPUStringView v = WGPU_STRING_VIEW_INIT;
  v.data = s;
  v.length = WGPU_STRLEN;
  return v;
}

const char* msg_or_empty(WGPUStringView v) {
  return (v.data != nullptr) ? v.data : "";
}

void on_device_lost(WGPUDevice const*, WGPUDeviceLostReason reason, WGPUStringView message, void*, void*) {
  std::fprintf(stderr, "[WebGPU] Device lost. reason=%d msg=%s\n", static_cast<int>(reason), msg_or_empty(message));
}

void on_uncaptured_error(WGPUDevice const*, WGPUErrorType type, WGPUStringView message, void*, void*) {
  std::fprintf(stderr, "[WebGPU] Uncaptured error. type=%d msg=%s\n", static_cast<int>(type), msg_or_empty(message));
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

void frame() {
  if (!g_initialized) {
    return;
  }

  WGPUSurfaceTexture surface_texture = WGPU_SURFACE_TEXTURE_INIT;
  wgpuSurfaceGetCurrentTexture(g_surface, &surface_texture);
  if (surface_texture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
      surface_texture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) {
    return;
  }

  WGPUTextureView view = wgpuTextureCreateView(surface_texture.texture, nullptr);

  WGPUCommandEncoderDescriptor encoder_desc = WGPU_COMMAND_ENCODER_DESCRIPTOR_INIT;
  encoder_desc.label = make_str_view("frame_encoder");
  WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(g_device, &encoder_desc);

  WGPURenderPassColorAttachment color_attachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
  color_attachment.view = view;
  color_attachment.loadOp = WGPULoadOp_Clear;
  color_attachment.storeOp = WGPUStoreOp_Store;
  color_attachment.clearValue = WGPUColor{0.08, 0.10, 0.14, 1.0};

  WGPURenderPassDescriptor pass_desc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
  pass_desc.colorAttachmentCount = 1;
  pass_desc.colorAttachments = &color_attachment;

  WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &pass_desc);
  wgpuRenderPassEncoderEnd(pass);
  wgpuRenderPassEncoderRelease(pass);

  WGPUCommandBufferDescriptor cmd_desc = WGPU_COMMAND_BUFFER_DESCRIPTOR_INIT;
  cmd_desc.label = make_str_view("frame_cmd");
  WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, &cmd_desc);

  wgpuQueueSubmit(g_queue, 1, &cmd);

  wgpuCommandBufferRelease(cmd);
  wgpuCommandEncoderRelease(encoder);
  wgpuTextureViewRelease(view);
  wgpuTextureRelease(surface_texture.texture);
}

void request_device_callback(WGPURequestDeviceStatus status,
                             WGPUDevice device,
                             WGPUStringView message,
                             void*,
                             void*) {
  if (status != WGPURequestDeviceStatus_Success || device == nullptr) {
    std::fprintf(stderr, "Failed to request WebGPU device: %s\n", msg_or_empty(message));
    return;
  }

  g_device = device;
  g_queue = wgpuDeviceGetQueue(g_device);

  const int width = 1280;
  const int height = 720;

  const WGPUTextureFormat surface_format = choose_surface_format(g_surface, g_adapter);

  g_surface_config.device = g_device;
  g_surface_config.format = surface_format;
  g_surface_config.usage = WGPUTextureUsage_RenderAttachment;
  g_surface_config.width = width;
  g_surface_config.height = height;
  g_surface_config.presentMode = WGPUPresentMode_Fifo;
  g_surface_config.alphaMode = WGPUCompositeAlphaMode_Auto;

  wgpuSurfaceConfigure(g_surface, &g_surface_config);

  g_initialized = true;
  emscripten_set_main_loop(frame, 0, true);
}

void request_adapter_callback(WGPURequestAdapterStatus status,
                              WGPUAdapter adapter,
                              WGPUStringView message,
                              void*,
                              void*) {
  if (status != WGPURequestAdapterStatus_Success || adapter == nullptr) {
    std::fprintf(stderr, "Failed to request WebGPU adapter: %s\n", msg_or_empty(message));
    return;
  }

  g_adapter = adapter;

  WGPUDeviceDescriptor device_desc = WGPU_DEVICE_DESCRIPTOR_INIT;
  device_desc.label = make_str_view("framespace_device");

  WGPUDeviceLostCallbackInfo lost_info = WGPU_DEVICE_LOST_CALLBACK_INFO_INIT;
  lost_info.mode = WGPUCallbackMode_AllowSpontaneous;
  lost_info.callback = on_device_lost;
  device_desc.deviceLostCallbackInfo = lost_info;

  WGPUUncapturedErrorCallbackInfo uncaptured_info = WGPU_UNCAPTURED_ERROR_CALLBACK_INFO_INIT;
  uncaptured_info.callback = on_uncaptured_error;
  device_desc.uncapturedErrorCallbackInfo = uncaptured_info;

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
