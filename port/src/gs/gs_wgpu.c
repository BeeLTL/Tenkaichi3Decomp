/*
 * Graphics Synthesizer, WebGPU back end (the browser build only; Dawn's webgpu.h for Emscripten).
 *
 * The same model as the Vulkan back end (gs_gpu.c): gs_draw.c records the frame; this file owns the GPU objects and
 * at the end of the frame uploads what is new, replays the recorded list, and presents. The design (resources, binding
 * layouts, what does not map directly from OpenGL / Vulkan) is in BROWSER_PORT_STATUS.md, "WebGPU back end: design".
 *
 * Waiting (for the adapter, the device, a buffer to map) uses wgpuInstanceWaitAny: the game runs under JSPI, so its
 * thread waits there without blocking the page, as it does for its files.
 */
#ifdef __EMSCRIPTEN__
#include <SDL3/SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <webgpu/webgpu.h>
#include "gs_internal.h"
#include "gs_draw.h"
#include "ui.h"

/* SDL's default canvas (SDL_HINT_EMSCRIPTEN_CANVAS_SELECTOR when the hint is not set): the surface is that canvas. */
#define SDL_DEFAULT_CANVAS_SELECTOR "#canvas"

static SDL_Window *sWindow;
static WGPUInstance sInstance;
static WGPUAdapter sAdapter;
static WGPUDevice sDevice;
static WGPUQueue sQueue;
static WGPUSurface sSurface;
static WGPUTextureFormat sSurfaceFormat;
static uint32_t sSurfaceW, sSurfaceH; /* the size the surface is configured for (0: not yet) */
static WGPULimits sLimits;            /* the device's */
static int sUnclippedDepth;           /* depth-clip-control: depth clamped instead of clipped, as SDL GPU and GL do it */
static int sBcTextures;               /* texture-compression-bc: texture packs' BC1 / BC2 / BC3 replacements */

/* Errors the device reports (validation, out of memory, internal) and a lost device: counted and logged, never hidden.
   The tests read the count (bench.js). */
static unsigned sDeviceErrors;
static int sDeviceLost, sDeviceLostReported;

static WGPUStringView string_view(const char *s) {
    WGPUStringView v = {s, WGPU_STRLEN};
    return v;
}

static int string_length(WGPUStringView s) {
    return s.data == NULL ? 0 : (int)(s.length == WGPU_STRLEN ? strlen(s.data) : s.length);
}

/* Waits for a future (the game's thread suspends: JSPI). 1 = it completed. */
static int wait_for(WGPUFuture future) {
    WGPUFutureWaitInfo info = {future, 0};
    return wgpuInstanceWaitAny(sInstance, 1, &info, UINT64_MAX) == WGPUWaitStatus_Success && info.completed;
}

/* ---- callbacks ------------------------------------------------------------------------------------------------ */

static void on_adapter(WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView message, void *result, void *unused) {
    (void)unused;
    if (status == WGPURequestAdapterStatus_Success) {
        *(WGPUAdapter *)result = adapter;
    } else {
        fprintf(stderr, "bt3: webgpu: no adapter: %.*s\n", string_length(message), message.data);
    }
}

static void on_device(WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message, void *result, void *unused) {
    (void)unused;
    if (status == WGPURequestDeviceStatus_Success) {
        *(WGPUDevice *)result = device;
    } else {
        fprintf(stderr, "bt3: webgpu: no device: %.*s\n", string_length(message), message.data);
    }
}

static const char *error_name(WGPUErrorType type) {
    switch (type) {
    case WGPUErrorType_Validation: return "validation";
    case WGPUErrorType_OutOfMemory: return "out of memory";
    case WGPUErrorType_Internal: return "internal";
    default: return "unknown";
    }
}

static void on_device_error(WGPUDevice const *device, WGPUErrorType type, WGPUStringView message, void *unused1, void *unused2) {
    (void)device; (void)unused1; (void)unused2;
    sDeviceErrors++;
    fprintf(stderr, "bt3: webgpu: %s error: %.*s\n", error_name(type), string_length(message), message.data);
}

static void on_device_lost(WGPUDevice const *device, WGPUDeviceLostReason reason, WGPUStringView message, void *unused1, void *unused2) {
    (void)device; (void)unused1; (void)unused2;
    if (reason == WGPUDeviceLostReason_Destroyed || reason == WGPUDeviceLostReason_CallbackCancelled) {
        return; /* (released on purpose, or the program ending) */
    }
    sDeviceLost = 1;
    fprintf(stderr, "bt3: webgpu: the device was lost (reason %d): %.*s\n", (int)reason, string_length(message), message.data);
}

/* ---- device and surface --------------------------------------------------------------------------------------- */

static void print_adapter(void) {
    WGPUAdapterInfo info;
    memset(&info, 0, sizeof(info));
    if (wgpuAdapterGetInfo(sAdapter, &info) == WGPUStatus_Success) {
        fprintf(stderr, "bt3: webgpu adapter: %.*s %.*s %.*s %.*s\n", string_length(info.vendor), info.vendor.data,
                string_length(info.architecture), info.architecture.data, string_length(info.device), info.device.data,
                string_length(info.description), info.description.data);
        wgpuAdapterInfoFreeMembers(info);
    }
}

static int device_create(void) {
    WGPUFeatureName features[3];
    size_t featureCount = 0;
    WGPUDeviceDescriptor desc;

    sUnclippedDepth = wgpuAdapterHasFeature(sAdapter, WGPUFeatureName_DepthClipControl);
    sBcTextures = wgpuAdapterHasFeature(sAdapter, WGPUFeatureName_TextureCompressionBC);
    if (sUnclippedDepth) {
        features[featureCount++] = WGPUFeatureName_DepthClipControl;
    } else {
        fprintf(stderr, "bt3: webgpu: no depth-clip-control: geometry beyond the far plane is cut instead of clamped "
                        "(differs from the OpenGL and Vulkan back ends)\n");
    }
    if (sBcTextures) {
        features[featureCount++] = WGPUFeatureName_TextureCompressionBC;
    }
    memset(&desc, 0, sizeof(desc));
    desc.label = string_view("bt3");
    desc.requiredFeatureCount = featureCount;
    desc.requiredFeatures = features;
    desc.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
    desc.deviceLostCallbackInfo.callback = on_device_lost;
    desc.uncapturedErrorCallbackInfo.callback = on_device_error;
    {
        WGPURequestDeviceCallbackInfo cb = {NULL, WGPUCallbackMode_WaitAnyOnly, on_device, &sDevice, NULL};
        if (!wait_for(wgpuAdapterRequestDevice(sAdapter, &desc, cb)) || sDevice == NULL) {
            return 0;
        }
    }
    sLimits.nextInChain = NULL;
    wgpuDeviceGetLimits(sDevice, &sLimits);
    sQueue = wgpuDeviceGetQueue(sDevice);
    return 1;
}

static int surface_create(void) {
    const char *hint = SDL_GetHint(SDL_HINT_EMSCRIPTEN_CANVAS_SELECTOR);
    WGPUEmscriptenSurfaceSourceCanvasHTMLSelector canvas;
    WGPUSurfaceDescriptor desc;
    WGPUSurfaceCapabilities caps;

    memset(&canvas, 0, sizeof(canvas));
    canvas.chain.sType = WGPUSType_EmscriptenSurfaceSourceCanvasHTMLSelector;
    canvas.selector = string_view(hint != NULL ? hint : SDL_DEFAULT_CANVAS_SELECTOR);
    memset(&desc, 0, sizeof(desc));
    desc.nextInChain = &canvas.chain;
    sSurface = wgpuInstanceCreateSurface(sInstance, &desc);
    if (sSurface == NULL) {
        return 0;
    }
    memset(&caps, 0, sizeof(caps));
    if (wgpuSurfaceGetCapabilities(sSurface, sAdapter, &caps) != WGPUStatus_Success || caps.formatCount == 0) {
        fprintf(stderr, "bt3: webgpu: the canvas offers no surface format\n");
        return 0;
    }
    sSurfaceFormat = caps.formats[0]; /* the first is the preferred one (getPreferredCanvasFormat) */
    wgpuSurfaceCapabilitiesFreeMembers(caps);
    return 1;
}

/* The surface at the window's size in pixels; configured again when that changes. */
static void surface_fit(void) {
    int w, h;
    WGPUSurfaceConfiguration config;

    SDL_GetWindowSizeInPixels(sWindow, &w, &h);
    if (w <= 0 || h <= 0 || ((uint32_t)w == sSurfaceW && (uint32_t)h == sSurfaceH)) {
        return;
    }
    memset(&config, 0, sizeof(config));
    config.device = sDevice;
    config.format = sSurfaceFormat;
    config.usage = WGPUTextureUsage_RenderAttachment;
    config.width = (uint32_t)w;
    config.height = (uint32_t)h;
    config.alphaMode = WGPUCompositeAlphaMode_Opaque;
    config.presentMode = WGPUPresentMode_Fifo; /* (the only mode a canvas has; the game paces itself) */
    wgpuSurfaceConfigure(sSurface, &config);
    sSurfaceW = (uint32_t)w;
    sSurfaceH = (uint32_t)h;
}

static int wg_init(void) {
    static const WGPUInstanceFeatureName instanceFeatures[] = {WGPUInstanceFeatureName_TimedWaitAny};
    WGPUInstanceDescriptor desc;

    sWindow = GsDraw_WindowCreate(0); /* the shared window, without an OpenGL context: the canvas gets a WebGPU one */
    if (sWindow == NULL) {
        return 0;
    }
    memset(&desc, 0, sizeof(desc));
    desc.requiredFeatureCount = sizeof(instanceFeatures) / sizeof(instanceFeatures[0]);
    desc.requiredFeatures = instanceFeatures;
    sInstance = wgpuCreateInstance(&desc);
    if (sInstance == NULL) {
        fprintf(stderr, "bt3: webgpu: not available in this browser\n");
        return 0;
    }
    {
        WGPURequestAdapterOptions options;
        WGPURequestAdapterCallbackInfo cb = {NULL, WGPUCallbackMode_WaitAnyOnly, on_adapter, &sAdapter, NULL};
        memset(&options, 0, sizeof(options));
        options.powerPreference = WGPUPowerPreference_HighPerformance;
        if (!wait_for(wgpuInstanceRequestAdapter(sInstance, &options, cb)) || sAdapter == NULL) {
            return 0;
        }
    }
    print_adapter();
    if (!device_create() || !surface_create()) {
        return 0;
    }
    surface_fit();
    fprintf(stderr, "bt3: webgpu: surface %ux%u, depth clamp %s, BC textures %s, uniform offset alignment %u\n", sSurfaceW,
            sSurfaceH, sUnclippedDepth ? "yes" : "NO", sBcTextures ? "yes" : "no", sLimits.minUniformBufferOffsetAlignment);
    return 1;
}

/* ---- the frame ------------------------------------------------------------------------------------------------ */

static void wg_frame_end(void) {
    WGPUSurfaceTexture surfaceTexture;
    WGPUTextureView view;
    WGPUCommandEncoder encoder;
    WGPURenderPassColorAttachment color;
    WGPURenderPassDescriptor pass;
    WGPURenderPassEncoder rp;
    WGPUCommandBuffer commands;

    GsDraw_PollEvents();
    if (sDeviceLost) {
        if (!sDeviceLostReported) {
            fprintf(stderr, "bt3: webgpu: nothing more is drawn (the device is lost; reload the page)\n");
            sDeviceLostReported = 1;
        }
        GsDraw_FrameReset();
        return;
    }
    surface_fit();
    memset(&surfaceTexture, 0, sizeof(surfaceTexture));
    wgpuSurfaceGetCurrentTexture(sSurface, &surfaceTexture);
    if (surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
        surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) {
        fprintf(stderr, "bt3: webgpu: no surface texture this frame (status %d)\n", (int)surfaceTexture.status);
        GsDraw_FrameReset();
        return;
    }
    view = wgpuTextureCreateView(surfaceTexture.texture, NULL);
    encoder = wgpuDeviceCreateCommandEncoder(sDevice, NULL);
    memset(&color, 0, sizeof(color));
    color.view = view;
    color.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    color.loadOp = WGPULoadOp_Clear;
    color.storeOp = WGPUStoreOp_Store;
    color.clearValue.a = 1.0; /* black around the picture */
    memset(&pass, 0, sizeof(pass));
    pass.colorAttachmentCount = 1;
    pass.colorAttachments = &color;
    rp = wgpuCommandEncoderBeginRenderPass(encoder, &pass);
    wgpuRenderPassEncoderEnd(rp);
    wgpuRenderPassEncoderRelease(rp);
    commands = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuQueueSubmit(sQueue, 1, &commands);
    wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(encoder);
    wgpuTextureViewRelease(view);
    wgpuTextureRelease(surfaceTexture.texture);
    /* (no wgpuSurfacePresent: a canvas shows its texture when the page gets control back, at the vertical blank) */
    GsDraw_FrameReset();
}

/* ---- not implemented yet (each one stops the game with its name; removed as it is implemented) ---------------- */

static void not_yet(const char *what) {
    fprintf(stderr, "bt3: not implemented yet in the WebGPU back end: %s\n", what);
    abort();
}
static void wg_scale_changed(void) { not_yet("scaleChanged"); }
static GsTex wg_white_tex(void) { not_yet("whiteTex"); return 0; }
static GsTex wg_tex_create(uint32_t w, uint32_t h, int format, int levels) { (void)w; (void)h; (void)format; (void)levels; not_yet("texCreate"); return 0; }
static int wg_tex_upload(GsTex t, int level, uint32_t w, uint32_t h, int format, const void *px, uint32_t bytes) {
    (void)t; (void)level; (void)w; (void)h; (void)format; (void)px; (void)bytes;
    not_yet("texUpload");
    return 0;
}
static void wg_tex_destroy(GsTex t) { (void)t; not_yet("texDestroy"); }
static int wg_tex_slots_left(void) { not_yet("texSlotsLeft"); return 0; }
static void wg_target_ensure(int i) { (void)i; not_yet("targetEnsure"); }
static void wg_target_drop(int i) { (void)i; not_yet("targetDrop"); }
static GsTex wg_target_color(int i) { (void)i; not_yet("targetColor"); return 0; }
static GsTex wg_target_aux(int i) { (void)i; not_yet("targetAux"); return 0; }
static GsTex wg_target_depth(int i) { (void)i; not_yet("targetDepth"); return 0; }
static int wg_pipe_get(uint32_t key) { (void)key; not_yet("pipeGet"); return 0; }
static int wg_target_read(int i, uint8_t *rgba) { (void)i; (void)rgba; not_yet("targetRead"); return 0; }
static void wg_target_copy(int src, int dst) { (void)src; (void)dst; not_yet("targetCopy"); }

GsBackend sWebGpuBackend = {
    "webgpu",
    wg_init,
    wg_frame_end,
    wg_scale_changed,
    wg_white_tex,
    wg_tex_create,
    wg_tex_upload,
    wg_tex_destroy,
    wg_tex_slots_left,
    wg_target_ensure,
    wg_target_drop,
    wg_target_color,
    wg_target_aux,
    wg_target_depth,
    wg_pipe_get,
    wg_target_read,
    wg_target_copy,
};
#endif
