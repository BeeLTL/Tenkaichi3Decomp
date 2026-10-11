/*
 * Browser build: OpenGL functions the GL back end (gs/gs_gl.c) uses that WebGL 2 only has as extensions, so that
 * Emscripten's GL layer does not offer them under these names:
 *   glEnablei / glDisablei / glColorMaski  <- OES_draw_buffers_indexed (blend and write mask per colour attachment:
 *                                             the second attachment holds the GS alpha byte, unblended)
 *   glClipControl                          <- EXT_clip_control (depth stored as z / w, as the Vulkan back end has it)
 * and whether a WebGL extension is there (EXT_depth_clamp, for glEnable(GL_DEPTH_CLAMP)). Port_WebGLProc returns
 * NULL for a function whose extension the browser lacks; gs_gl.c then takes its own fallbacks.
 */
#include <emscripten.h>
#include <string.h>

/* (GLctx is Emscripten's current WebGL context) */
EM_JS(int, web_gl_ext, (const char *name), {
    return (typeof GLctx !== 'undefined' && GLctx && GLctx.getExtension(UTF8ToString(name))) ? 1 : 0;
});
EM_JS(void, web_gl_enablei, (unsigned cap, unsigned index, int on), {
    const e = GLctx.getExtension('OES_draw_buffers_indexed');
    if (on) e.enableiOES(cap, index); else e.disableiOES(cap, index);
});
EM_JS(void, web_gl_colormaski, (unsigned index, int r, int g, int b, int a), {
    GLctx.getExtension('OES_draw_buffers_indexed').colorMaskiOES(index, !!r, !!g, !!b, !!a);
});
EM_JS(void, web_gl_clipcontrol, (unsigned origin, unsigned depth), {
    GLctx.getExtension('EXT_clip_control').clipControlEXT(origin, depth);  // (GL's enum values are the extension's)
});

/* The GL signatures (GLenum / GLuint are unsigned int, GLboolean unsigned char). */
static void w_enablei(unsigned cap, unsigned index) { web_gl_enablei(cap, index, 1); }
static void w_disablei(unsigned cap, unsigned index) { web_gl_enablei(cap, index, 0); }
static void w_colormaski(unsigned index, unsigned char r, unsigned char g, unsigned char b, unsigned char a) {
    web_gl_colormaski(index, r, g, b, a);
}
static void w_clipcontrol(unsigned origin, unsigned depth) { web_gl_clipcontrol(origin, depth); }

void *Port_WebGLProc(const char *name) {
    if (strcmp(name, "glEnablei") == 0 || strcmp(name, "glDisablei") == 0 || strcmp(name, "glColorMaski") == 0) {
        if (!web_gl_ext("OES_draw_buffers_indexed")) {
            return NULL;
        }
        return strcmp(name, "glEnablei") == 0 ? (void *)w_enablei : strcmp(name, "glDisablei") == 0 ? (void *)w_disablei
                                                                                                  : (void *)w_colormaski;
    }
    if (strcmp(name, "glClipControl") == 0) {
        return web_gl_ext("EXT_clip_control") ? (void *)w_clipcontrol : NULL;
    }
    return NULL;
}

int Port_WebGLExtension(const char *name) { return web_gl_ext(name); }
