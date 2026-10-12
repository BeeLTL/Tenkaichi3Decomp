/*
 * Browser build: the settings are the page's (port/web/settings.js), not a window drawn over the game. This file is
 * the engine's side of ui.h for that build, in place of ui.cpp (Dear ImGui, which the browser build does not
 * compile): the canvas shows the game alone.
 *
 * The page reaches the engine through the exported Bt3Ui_* functions below. Reading is immediate. A setting or an
 * action is queued and applied by the game's own thread between two frames: the queue is announced with an SDL user
 * event, which GsDraw_PollEvents hands to Ui_Event like any other event. So a change never lands in the middle of a
 * frame, and an action that waits (reading a picture back from WebGPU) runs where the game may wait. Bindings and a
 * player's controller are plain values gs_input.c reads when it polls: they are written at once, as ui.cpp does.
 *
 * What each setting is, and where it is kept and saved, stays where the native build has it: PortVideo
 * (GsGpu_GetSettings / GsGpu_SetSettings), gs_input.c's bindings, gsInterp, the clash option, the unlock request.
 */
#include <SDL3/SDL.h>
#include <emscripten.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gs/ui.h"

extern int gsInterp;                     /* gs_draw.c: smooth motion 0..3; -1 before the first frame */
extern unsigned gGsFrame;                /* gs_core.c */
extern unsigned gPortLiveBlanks;         /* plat_stub.c */
extern unsigned gInterpShown;            /* gs_draw.c: in-between pictures shown (smooth motion) since the start */
extern int gGsMainFbp;                   /* gs_sony.c: the shown frame buffer, -1 before the first swap */
extern volatile int gPortUnlockAll, gPortUnlockDone; /* headless.c: the game's own "unlock everything" */
extern void *gSaveData; /* the game's save in memory, NULL until one is loaded (a game pointer: 32 bits, as wasm's) */
extern unsigned GsWgpu_DeviceErrors(void); /* gs_wgpu.c */
int GsGpu_ReadFrame(uint32_t fbp, uint8_t *rgba);
int Port_ClashSquare(void);
void Port_ClashSquareSet(int on);
int Port_ClashSquareX(void);
void Port_ClashSquareXSet(int quarters);
void Port_SettingSave(const char *name, int value);
void Port_SettingsWrite(void);
int Port_Setting(const char *name, int def);
int Port_GameSpeed(void);               /* plat_stub.c: the player's own game speed, percent (never used online) */
void Port_GameSpeedSet(int percent);
void Port_AudioRefresh(void);           /* snd_adx.c: playing streams follow the volumes and the speed at once */
extern int gPortMusicFollows;           /* snd_adx.c: 1 = looping music follows the game speed; -1 before it is read */

/* The picture GsGpu_ReadFrame gives: the GS's own resolution. */
#define FRAME_W 512
#define FRAME_H 448
/* A setting value the page cannot have asked for: the queue's entries for actions carry their argument instead. */
#define UI_QUEUE_SIZE 64

/* ---- the page's side (Module.bt3Ui, given to createBt3 by port/web/index.html) ------------------------------- */

/* A fault in the page's panel must not end the game: it is reported (the log, and a notice on the page) and the
   engine goes on. */
EM_JS(void, page_call, (const char *what, int arg), {
    const name = UTF8ToString(what);
    if (!Module.bt3Ui || typeof Module.bt3Ui[name] !== 'function') {
        err(`bt3: the page has no settings handler "${name}"`);
        return;
    }
    try {
        Module.bt3Ui[name](arg);
    } catch (e) {
        err(`bt3: settings handler "${name}" of the page failed: ${e && e.stack || e}`);
        try { Module.bt3Ui.notice(`The settings panel failed (${name}): see the log.`); } catch (e2) { /* (reported above) */ }
    }
});

EM_JS(void, page_notice, (const char *text), {
    if (!Module.bt3Ui) { err('bt3: notice (no page handler): ' + UTF8ToString(text)); return; }
    Module.bt3Ui.notice(UTF8ToString(text));
});

EM_JS(void, page_screenshot, (const uint8_t *rgba, int w, int h), {
    if (!Module.bt3Ui) { err('bt3: screenshot taken, but the page has no handler'); return; }
    Module.bt3Ui.screenshot(HEAPU8.slice(rgba, rgba + w * h * 4), w, h);
});

/* ---- settings ------------------------------------------------------------------------------------------------- */

/* The PortVideo fields, each read and written through the native build's own pair (which also saves it). */
#define VIDEO_FIELD(field)                                                                                          \
    static int get_##field(void) { PortVideo v; GsGpu_GetSettings(&v); return v.field; }                          \
    static void set_##field(int x) { PortVideo v; GsGpu_GetSettings(&v); v.field = x; GsGpu_SetSettings(&v); }
VIDEO_FIELD(scale)
VIDEO_FIELD(aspectMilli)
VIDEO_FIELD(smooth2d)
VIDEO_FIELD(fxOff)
VIDEO_FIELD(glow)
VIDEO_FIELD(music)
VIDEO_FIELD(effects)
VIDEO_FIELD(voices)
VIDEO_FIELD(stage)
VIDEO_FIELD(hudLayout)
VIDEO_FIELD(frameSkip)
VIDEO_FIELD(texPack)
static int get_texPackCount(void) { PortVideo v; GsGpu_GetSettings(&v); return v.texPackCount; }

/* Smooth motion (in-between pictures, gs_draw.c) in the browser, measured with port/tests/bench/present_rate.js on the
   demo battle (2026-10-12, RTX 5070 Ti, 200 Hz display; BROWSER_PORT_STATUS.md has the tables). Each in-between picture
   replays the whole frame. WebGPU at "60" (one per tick, ~5 ms each): 59 pictures a second reach the screen, evenly
   (longest gap 34 ms, as without), game speed 100 %. Every other case shows more pictures only by leaving real ticks
   out to stay in time (frame skipping), which makes longer gaps than without (47 to 53 ms against 36): WebGPU at 120 /
   240, and WebGL 2 at any setting (~11 ms per in-between picture; without frame skipping the game slows to 90 %).
   So: WebGPU up to "60", WebGL 2 none. */
int Web_InterpLimit(const char *renderer) {
    return strcmp(renderer, "webgpu") == 0 ? 1 : 0;
}

static int get_interp(void) { return gsInterp < 0 ? 0 : gsInterp; }
static int get_interp_limit(void) { return GsDraw_InterpLimit(); }
static void set_interp(int x) {
    if (gsInterp < 0) { /* (its buffers are made with the first frame: gs_draw.c) */
        fprintf(stderr, "bt3: smooth motion can be set once the first frame is drawn\n");
        return;
    }
    if (x > GsDraw_InterpLimit()) {
        fprintf(stderr, "bt3: smooth motion %d is above what this renderer shows here (%d)\n", x, GsDraw_InterpLimit());
        return;
    }
    gsInterp = x < 0 ? 0 : x;
    Port_SettingSave("interp", gsInterp);
    Port_SettingsWrite();
}
/* (as ui.cpp's Audio tab and Cheats tab change them) */
static int get_music_follows(void) { return gPortMusicFollows < 0 ? Port_Setting("music_follows_speed", 0) != 0 : gPortMusicFollows; }
static void set_music_follows(int x) {
    gPortMusicFollows = x != 0;
    Port_SettingSave("music_follows_speed", gPortMusicFollows);
    Port_SettingsWrite();
}
static void set_game_speed(int x) {
    Port_GameSpeedSet(x);
    Port_AudioRefresh();
    Port_SettingsWrite();
}
static void set_clash_square(int x) { Port_ClashSquareSet(x); }
static void set_clash_square_x(int x) { Port_ClashSquareXSet(x); Port_SettingsWrite(); }
static int get_unlock_done(void) { return gPortUnlockDone; }
/* (the unlock is carried out only with a save loaded: headless.c drops the request otherwise) */
static int get_save_loaded(void) { return gSaveData != NULL; }
static int get_frame(void) { return (int)gGsFrame; }
static int get_vblanks(void) { return (int)gPortLiveBlanks; }
static int get_interp_shown(void) { return (int)gInterpShown; }
static int get_webgpu_errors(void) { return (int)GsWgpu_DeviceErrors(); }
static int get_actions(void) { return PORT_ACTIONS; }
static int get_pad_buttons(void) { return PORT_PAD_BUTTONS; }

/* Names as the settings file has them (plat_settings.c) where there is one. */
static const struct {
    const char *name;
    int (*get)(void);
    void (*set)(int); /* NULL: read only */
} kSettings[] = {
    {"scale", get_scale, set_scale},
    {"aspect_milli", get_aspectMilli, set_aspectMilli},
    {"smooth_2d", get_smooth2d, set_smooth2d},
    {"fx_off", get_fxOff, set_fxOff},
    {"glow", get_glow, set_glow},
    {"music", get_music, set_music},
    {"effects", get_effects, set_effects},
    {"voices", get_voices, set_voices},
    {"stage_sounds", get_stage, set_stage},
    {"music_follows_speed", get_music_follows, set_music_follows},
    {"hud_layout", get_hudLayout, set_hudLayout},
    {"frame_skip", get_frameSkip, set_frameSkip},
    {"game_speed", Port_GameSpeed, set_game_speed},
    {"texture_pack", get_texPack, set_texPack},
    {"texture_pack_count", get_texPackCount, NULL},
    {"interp", get_interp, set_interp},
    {"interp_limit", get_interp_limit, NULL},
    {"clash_square", Port_ClashSquare, set_clash_square},
    {"clash_square_x", Port_ClashSquareX, set_clash_square_x},
    {"unlock_done", get_unlock_done, NULL},
    {"save_loaded", get_save_loaded, NULL},
    {"frame", get_frame, NULL},
    {"vblanks", get_vblanks, NULL},
    {"interp_shown", get_interp_shown, NULL},
    {"webgpu_errors", get_webgpu_errors, NULL},
    {"actions", get_actions, NULL},
    {"pad_buttons", get_pad_buttons, NULL},
};
#define SETTING_COUNT ((int)(sizeof(kSettings) / sizeof(kSettings[0])))

static int setting_index(const char *name) {
    int i;
    for (i = 0; i < SETTING_COUNT; i++) {
        if (strcmp(kSettings[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

/* ---- the queue of changes ------------------------------------------------------------------------------------- */

typedef enum { REQ_SETTING, REQ_UNLOCK_ALL, REQ_RESET_CONTROLS, REQ_SCREENSHOT } RequestKind;
static struct {
    RequestKind kind;
    int index, value;
} sQueue[UI_QUEUE_SIZE];
static int sQueued;
static Uint32 sEventType; /* the SDL user event that says the queue has something */

static int queue_push(RequestKind kind, int index, int value) {
    SDL_Event ev;
    if (sEventType == 0 || sQueued == UI_QUEUE_SIZE) {
        fprintf(stderr, "bt3: settings change refused (%s)\n", sEventType == 0 ? "the game has not started" : "too many at once");
        return 0;
    }
    sQueue[sQueued].kind = kind;
    sQueue[sQueued].index = index;
    sQueue[sQueued].value = value;
    if (sQueued++ == 0) {
        SDL_zero(ev);
        ev.type = sEventType;
        SDL_PushEvent(&ev);
    }
    return 1;
}

static void screenshot(void) {
    uint8_t *rgba = malloc(FRAME_W * FRAME_H * 4);
    if (gGsMainFbp >= 0 && GsGpu_ReadFrame((uint32_t)gGsMainFbp, rgba)) {
        page_screenshot(rgba, FRAME_W, FRAME_H);
    } else {
        page_notice("No picture to take yet.");
    }
    free(rgba);
}

static void queue_apply(void) {
    int i;
    for (i = 0; i < sQueued; i++) {
        switch (sQueue[i].kind) {
        case REQ_SETTING: kSettings[sQueue[i].index].set(sQueue[i].value); break;
        case REQ_UNLOCK_ALL: gPortUnlockAll = 1; break; /* (carried out by the game at its next vertical blank) */
        case REQ_RESET_CONTROLS: PortInput_ResetDefaults(sQueue[i].value); PortInput_Save(); break;
        case REQ_SCREENSHOT: screenshot(); break;
        }
    }
    sQueued = 0;
    page_call("changed", 0);
}

/* ---- bindings: the next key or controller button is taken by the engine itself (SDL's names and codes) -------- */

enum { CAPTURE_NONE, CAPTURE_KEY, CAPTURE_PAD };
static int sCapKind, sCapPlayer, sCapAction;

/* the row PORT_ACTIONS is the key / button that opens the settings (one for the program, not a player's) */
static int *binding(int kind, int player, int action) {
    if (action == PORT_ACTIONS) {
        return kind == CAPTURE_KEY ? PortInput_OverlayKey() : PortInput_OverlayPad();
    }
    return kind == CAPTURE_KEY ? &PortInput_Keys(player)[action] : &PortInput_PadButtons(player)[action];
}

static void capture_end(void) {
    sCapKind = CAPTURE_NONE;
    gPortInputCapture = 0;
    page_call("captureDone", 0);
}

static void bind(int value) {
    if (sCapAction == PORT_ACTIONS && sCapKind == CAPTURE_KEY && value <= 0) {
        value = SDL_SCANCODE_F1; /* (cleared: F1 again, so that a keyboard always has a way in) */
    }
    if (sCapAction == PORT_ACTIONS && sCapKind == CAPTURE_PAD && value >= PORT_PAD_LT) {
        return; /* (a trigger cannot open the settings: a button only) */
    }
    *binding(sCapKind, sCapPlayer, sCapAction) = value;
    PortInput_Save();
}

/* Esc keeps the binding, Delete or Backspace clears it. */
static int capture_event(const SDL_Event *ev) {
    if (ev->type == SDL_EVENT_KEY_DOWN) {
        SDL_Scancode sc = ev->key.scancode;
        if (sc == SDL_SCANCODE_DELETE || sc == SDL_SCANCODE_BACKSPACE) {
            bind(sCapKind == CAPTURE_KEY ? 0 : -1);
            capture_end();
        } else if (sc == SDL_SCANCODE_ESCAPE) {
            capture_end();
        } else if (sCapKind == CAPTURE_KEY) {
            bind((int)sc);
            capture_end();
        }
        return 1;
    }
    if (ev->type == SDL_EVENT_KEY_UP) {
        return 1;
    }
    if (sCapKind == CAPTURE_PAD && ev->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
        bind((int)ev->gbutton.button);
        capture_end();
        return 1;
    }
    if (sCapKind == CAPTURE_PAD && ev->type == SDL_EVENT_GAMEPAD_AXIS_MOTION && ev->gaxis.value > SDL_JOYSTICK_AXIS_MAX / 2 &&
        (ev->gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || ev->gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)) {
        bind(ev->gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER ? PORT_PAD_LT : PORT_PAD_RT);
        capture_end();
        return 1;
    }
    return 0;
}

/* ---- ui.h --------------------------------------------------------------------------------------------------- */

int Ui_Init(SDL_Window *window, SDL_GPUDevice *device, void *gl_context) {
    (void)window; (void)device; (void)gl_context;
    sEventType = SDL_RegisterEvents(1);
    if (sEventType == 0) {
        fprintf(stderr, "bt3: no SDL event for the page's settings: %s\n", SDL_GetError());
        return 0;
    }
    page_call("ready", 0);
    return 1;
}

int Ui_Event(const SDL_Event *ev) {
    if (sEventType != 0 && ev->type == sEventType) {
        queue_apply();
        return 1;
    }
    return sCapKind != CAPTURE_NONE && capture_event(ev);
}

/* The engine's settings key (F1, START + SELECT, or the bound one): the page opens or closes its panel. */
void Ui_Toggle(void) {
    page_call("toggle", 0);
}

/* The OpenGL back end calls this after its present: the browser build draws nothing over the game. */
void Ui_DrawGL(void) {
}

/* Escape outside the settings (gs_draw.c asks before the program closes): a browser tab is closed by the browser, not
   by the game. The page keeps Escape from the game (settings.js); should one arrive, it says so instead of ending the
   page's game. 1 = answered here (nothing closes). */
int Ui_QuitAsk(void) {
    page_notice("The game cannot be quit from inside a browser: close the tab to leave it.");
    return 1;
}

/* gs_draw.c applies a change of V-sync to both desktop renderers (GsGl_Vsync, and this, the Vulkan one's). The browser
   build has no Vulkan renderer and the page offers no V-sync: a browser shows a page's pictures at the display's
   refresh whatever the setting. */
void GsGpu_Vsync(int on) {
    fprintf(stderr, "bt3: V-sync %d not applied: a browser shows pictures at the display's refresh\n", on);
}

void Port_UiNotice(const char *text) {
    page_notice(text != NULL ? text : "");
}

/* ---- the page's interface ------------------------------------------------------------------------------------ */

/* A setting's value; -1 and a message for an unknown name. */
EMSCRIPTEN_KEEPALIVE int Bt3Ui_Get(const char *name) {
    int i = setting_index(name);
    if (i < 0) {
        fprintf(stderr, "bt3: unknown setting %s\n", name);
        return -1;
    }
    return kSettings[i].get();
}

/* 1 = queued (applied between the next two frames); 0 = unknown, read only, or refused (with a message). */
EMSCRIPTEN_KEEPALIVE int Bt3Ui_Set(const char *name, int value) {
    int i = setting_index(name);
    if (i < 0 || kSettings[i].set == NULL) {
        fprintf(stderr, "bt3: setting %s %s\n", name, i < 0 ? "is unknown" : "cannot be changed");
        return 0;
    }
    return queue_push(REQ_SETTING, i, value);
}

/* "unlock_all", "reset_controls" (arg: player), "screenshot". */
EMSCRIPTEN_KEEPALIVE int Bt3Ui_Action(const char *name, int arg) {
    if (strcmp(name, "unlock_all") == 0) { return queue_push(REQ_UNLOCK_ALL, 0, 0); }
    if (strcmp(name, "reset_controls") == 0) { return queue_push(REQ_RESET_CONTROLS, 0, arg & 1); }
    if (strcmp(name, "screenshot") == 0) { return queue_push(REQ_SCREENSHOT, 0, 0); }
    fprintf(stderr, "bt3: unknown settings action %s\n", name);
    return 0;
}

/* The panel is open: neither the keyboard nor a controller reaches the game (gs_input.c), as with the native window. */
EMSCRIPTEN_KEEPALIVE void Bt3Ui_SetOpen(int open) {
    if (!open && sCapKind != CAPTURE_NONE) {
        capture_end();
    }
    gPortOverlayOpen = open != 0;
}

EMSCRIPTEN_KEEPALIVE const char *Bt3Ui_ActionLabel(int action) {
    return action == PORT_ACTIONS ? "Open the settings" : PortInput_ActionLabel(action);
}

/* kind 1: the key (SDL scancode, 0 none); kind 2: the controller source (SDL_GamepadButton, PORT_PAD_LT / _RT, -1 none). */
EMSCRIPTEN_KEEPALIVE int Bt3Ui_Binding(int kind, int player, int action) {
    return *binding(kind, player & 1, action);
}

EMSCRIPTEN_KEEPALIVE const char *Bt3Ui_KeyName(int scancode) {
    return scancode > 0 ? SDL_GetScancodeName((SDL_Scancode)scancode) : "";
}

/* Takes the next key (kind 1) or controller button (kind 2) as the binding; the page puts the focus on the canvas,
   where SDL listens. Ui_Event takes it and the page hears captureDone. */
EMSCRIPTEN_KEEPALIVE void Bt3Ui_CaptureBegin(int kind, int player, int action) {
    sCapKind = kind == CAPTURE_PAD ? CAPTURE_PAD : CAPTURE_KEY;
    sCapPlayer = player & 1;
    sCapAction = action < 0 ? 0 : action > PORT_ACTIONS ? PORT_ACTIONS : action;
    gPortInputCapture = 1;
}

EMSCRIPTEN_KEEPALIVE void Bt3Ui_CaptureCancel(void) {
    if (sCapKind != CAPTURE_NONE) {
        capture_end();
    }
}

/* Which connected controller a player uses: -1 none, 0 = the first, ... */
EMSCRIPTEN_KEEPALIVE int Bt3Ui_PadSlot(int player) {
    return *PortInput_PadSlot(player & 1);
}

EMSCRIPTEN_KEEPALIVE void Bt3Ui_PadSlotSet(int player, int slot) {
    *PortInput_PadSlot(player & 1) = slot < -1 ? -1 : slot;
    PortInput_Save();
}

EMSCRIPTEN_KEEPALIVE int Bt3Ui_GamepadCount(void) {
    int count = 0;
    SDL_free(SDL_GetGamepads(&count));
    return count;
}

/* The name of connected controller i (a static buffer, valid until the next call). */
EMSCRIPTEN_KEEPALIVE const char *Bt3Ui_GamepadName(int i) {
    static char name[128];
    int count = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&count);
    const char *n = ids != NULL && i >= 0 && i < count ? SDL_GetGamepadNameForID(ids[i]) : NULL;
    snprintf(name, sizeof(name), "%s", n != NULL ? n : "");
    SDL_free(ids);
    return name;
}
