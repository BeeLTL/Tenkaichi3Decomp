#include "common.h"
#include "menu/menu_e.h"
#include "sys/pad.h"
#include "sys/save.h"

#ifdef PORT
/*
 * PC only: "Tenkaichi Budokai Online", the main menu's item 11 (menu_a.c). Duel_Main (menu_g_b.c) runs this screen
 * with gPortDuelRole 6. Its own look for online play, after the mockups of online_design: every picture, piece,
 * letter and button is its own (tbo/ui/*.png, drawn by the renderer at full resolution; see "the pictures" below).
 *
 * The hub lists nine modes. What runs today is what the room-code lobby of port/src/gs/net.c can do (the same as
 * the Dragon Net Battle window, without the window):
 *   Create Room   the host's rules, then a room code to give the other player;
 *   Join Room     a pad keypad (the PC keyboard types too) for the other player's code;
 *   the search    a Dragon Radar while the two games find each other; square: "Train While Waiting" leaves for the
 *                 Ultimate Training menu with the room still open (net.c polls it each blank and starts the match
 *                 from wherever the game is when the other player comes);
 *   Profile       the name the other player sees and the defaults of the connection.
 * Quick Match, Ranked Match, Friends, Tournament, Replays and Leaderboard need the matchmaking service to do more
 * than room codes (a public queue, player ids, ratings): they are listed, marked SOON, and say what they will be.
 * Once both players are there the session is the game's own versus mode (character select, the fight, results).
 */
extern u64 *Dma_BeginDirect(void);
extern void Dma_EndDirect(u64 *end);

/* A colour for the shapes and the letters' tints: 0..255 a channel. */
static u32 Mm_Rgba(s32 r, s32 g, s32 b, s32 a) {
    return (u32)r | ((u32)g << 8) | ((u32)b << 16) | ((u32)a << 24);
}

/* Any four-cornered plain shape, in the screen's 512 x 448 (corners in the order of a strip: top left, top right,
   bottom left, bottom right); the radar is drawn of these. */
static void Mm_Quad(f32 x0, f32 y0, f32 x1, f32 y1, f32 x2, f32 y2, f32 x3, f32 y3, u32 c0, u32 c1, u32 c2, u32 c3) {
    u64 *p = Dma_BeginDirect();

    p[0] = 4 | (1ULL << 15) | (1ULL << 60);
    p[1] = 0xE;
    p += 2;
    p[0] = 0x01BF000001FF0000ULL;
    p[1] = 0x40;
    p += 2;
    p[0] = 0x44;
    p[1] = 0x42;
    p += 2;
    p[0] = 0x30000;
    p[1] = 0x47;
    p += 2;
    p[0] = 0x4C;
    p[1] = 0x00;
    p += 2;
    p[0] = 4 | (1ULL << 15) | (1ULL << 58) | (2ULL << 60);
    p[1] = 0x51;
    p += 2;
    p[0] = (u64)c0 | (0x3F800000ULL << 32);
    p[1] = (u64)((s32)(x0 * 16.0f) + 0x7000) | ((u64)((s32)(y0 * 16.0f) + 0x7200) << 16);
    p += 2;
    p[0] = (u64)c1 | (0x3F800000ULL << 32);
    p[1] = (u64)((s32)(x1 * 16.0f) + 0x7000) | ((u64)((s32)(y1 * 16.0f) + 0x7200) << 16);
    p += 2;
    p[0] = (u64)c2 | (0x3F800000ULL << 32);
    p[1] = (u64)((s32)(x2 * 16.0f) + 0x7000) | ((u64)((s32)(y2 * 16.0f) + 0x7200) << 16);
    p += 2;
    p[0] = (u64)c3 | (0x3F800000ULL << 32);
    p[1] = (u64)((s32)(x3 * 16.0f) + 0x7000) | ((u64)((s32)(y3 * 16.0f) + 0x7200) << 16);
    p += 2;
    Dma_EndDirect(p);
}

/* The PC keyboard (port/src/gs/gs_input.c): on a keypad it types (a character at a time: 8 backspace, 13 enter,
   27 escape (back), 0 none) and the arrow keys and the controller move on it. On the other screens it is the pad,
   and Esc asks to close the game, as everywhere. */
extern void PortTbo_TextMode(int on);
extern int PortTbo_TextRead(void);
extern void PortTbo_Copy(const char *text); /* onto the clipboard ("Copy Code") */
#define Port_TextMode PortTbo_TextMode
#define Port_TextRead PortTbo_TextRead
static void Port_EscQuit(s32 on) {
    (void)on;
}
static s32 Port_EscTake(void) {
    return 0;
}

/* The fighters' names (Train While Waiting's choice), by character id. */
static const char *const sTbNames[] = {
    "Goku (Early)",
    "Goku (Med)",
    "Goku (Med) Super Saiyan",
    "Goku (End)",
    "Goku (End) Super Saiyan",
    "Goku (End) Super Saiyan 2",
    "Goku (End) Super Saiyan 3",
    "Goku (GT)",
    "Goku (GT) Super Saiyan",
    "Goku (GT) Super Saiyan 3",
    "Goku (GT) Super Saiyan 4",
    "Kid Goku",
    "Great Ape",
    "Kid Gohan",
    "Teen Gohan",
    "Teen Gohan Super Saiyan",
    "Teen Gohan Super Saiyan 2",
    "Gohan",
    "Gohan Super Saiyan",
    "Gohan Super Saiyan 2",
    "Great Saiyaman",
    "Ultimate Gohan",
    "Piccolo (Early)",
    "Piccolo (End)",
    "Nail",
    "Krillin",
    "Yamcha",
    "Tien",
    "Chiaotzu",
    "Vegeta (Scouter)",
    "Vegeta (Scouter) Great Ape",
    "Vegeta",
    "Vegeta Super Saiyan",
    "Super Vegeta",
    "Vegeta (2nd Form)",
    "Vegeta (2nd Form) Super Saiyan",
    "Vegeta (2nd Form) Super Saiyan 2",
    "Majin Vegeta",
    "Vegeta (2nd Form) Super Saiyan 4",
    "Trunks (Sword)",
    "Trunks (Sword) Super Saiyan",
    "Trunks",
    "Trunks Super Saiyan",
    "Super Trunks",
    "Kid Trunks",
    "Kid Trunks Super Saiyan",
    "Goten",
    "Goten Super Saiyan",
    "Gotenks",
    "Gotenks Super Saiyan",
    "Gotenks Super Saiyan 3",
    "Vegito",
    "Super Vegito",
    "Super Gogeta",
    "Gogeta Super Saiyan 4",
    "Mr Satan",
    "Videl",
    "Great Saiyaman 2",
    "Supreme Kai",
    "Kibito Kai",
    "Yajirobe",
    "Paikuhan",
    "Tapion",
    "Pan",
    "Uub",
    "Majuub",
    "Master Roshi",
    "Master Roshi Full Power",
    "Grandpa Gohan",
    "Namu",
    "Hatchan",
    "King Vegeta",
    "King Vegeta Great Ape",
    "Bardock",
    "Bardock Great Ape",
    "Celipa",
    "Celipa Great Ape",
    "Raditz",
    "Raditz Great Ape",
    "Saibamen",
    "Nappa",
    "Nappa Great Ape",
    "Zarbon",
    "Zarbon Transformed",
    "Dodoria",
    "Cui",
    "Ginyu",
    "Recoome",
    "Burter",
    "Jeice",
    "Guldo",
    "Frieza 1st Form",
    "Frieza 2nd Form",
    "Frieza 3rd Form",
    "Frieza Final Form",
    "Frieza 100% Power",
    "Mecha Frieza",
    "King Cold",
    "Appule",
    "Frieza Soldier",
    "Android 16",
    "Android 17",
    "Android 18",
    "Android 19",
    "Android 2",
    "Cell 1st Form",
    "Cell 2nd Form",
    "Perfect Cell",
    "Super Perfect Cell",
    "Cell Jr",
    "Babidi",
    "Dabura",
    "Majin Buu",
    "Evil Buu",
    "Super Buu",
    "Super Buu Gotenks Absorbed",
    "Super Buu Gohan Absorbed",
    "Kid Buu",
    "Garlic Jr",
    "Super Garlic Jr",
    "Wheelo",
    "Turles",
    "Turles Great Ape",
    "Slug",
    "Slug Giant",
    "Sauza",
    "Cooler",
    "Cooler Final Form",
    "Metal Cooler",
    "Android 13",
    "Android 13 Fusion",
    "Broly",
    "Broly Super Saiyan",
    "Broly Legendary Super Saiyan",
    "Zangya",
    "Bojack",
    "Bojack 100% Power",
    "Janemba",
    "Super Janemba",
    "Hildegarn",
    "Baby Vegeta",
    "Super Baby 1",
    "Super Baby 2",
    "Super Baby Great Ape",
    "Super 17",
    "Nuova Shenron",
    "Shin Shenron",
    "Omega Shenron",
    "Taopaipai",
    "Cyborg Tao",
    "General Blue",
    "Akuman",
    "Pilaf Machine",
    "Pilaf Machine Fusion",
    "Tambourine",
    "Demon King Piccolo",
    "Arale",
    "Chichi",
    "Spopovich",
    "Future Gohan",
    "Future Gohan Super Saiyan",
};
#include "menu/tbo_art.h"
#include "menu/tbo_sizes.h"

extern void Job_Run(void);
extern void Gfx_BeginFrame(void);
extern void Gfx_EndFrame(s32 arg);
extern void Dma_Flush(void);
extern void Dma_ResetBuffers(void);
extern void File_Stub264D90(void);
extern void ColorFade_StartIn(s32 r, s32 g, s32 b, s32 frames);
extern void ColorFade_StartOut(s32 r, s32 g, s32 b, s32 frames);
extern void ColorFade_Update(void);
extern void ColorFade_Draw(void);
extern s32 ColorFade_IsInDone(void);
extern s32 ColorFade_IsOutDone(void);
extern s32 sceGsSyncPath(s32 mode, u16 timeout);

/* port/src/gs/net.c, state.c, plat_settings.c */
extern void Port_NetOptions(int rollback, int delay);
extern void Port_NetRulesSet(int type, int dp, int time);
extern const char *Port_NetNameGet(void);
extern void Port_NetNameSave(const char *name);
extern int Port_LobbyStartCode(int host, const char *code);
extern int Port_LobbyPoll(void);
extern void Port_LobbyCancel(void);
extern void Port_LobbyLaunch(void);
extern const char *Port_LobbyCode(void);
extern const char *Port_LobbyError(void);
extern int Port_LobbyOther(void);
extern void Port_LobbyBackground(int on);
extern int Port_LobbyInBackground(void);
extern int Port_RollCan(void);
extern int Port_LobbyStartQuick(int queue);   /* a quick match: 0 one on one, 1 teams (the service's queues S, T) */
extern void Port_LobbyQueuesAsk(void);        /* asks how many wait in each queue */
extern int Port_LobbyQueueWaiting(int queue); /* ... as last heard, -1 if not known */
extern void Port_NetSpeedSet(int percent);
extern int Port_NetSpeedStandard(void);
extern void Port_NetNameSet(const char *name);
extern void Battle_ClearWork(void);
extern void BattleSetup_SetRule(s32 screenMode, s32 mode, s32 bgm, s32 timeLimit, s32 announcer, s32 stage, s32 unk10);
extern void BattleSetup_SetSide(s32 sideNo, s32 control, s32 pad, s32 memberCount, s32 unk1FC, s32 unk200, s32 lead, void *unk);
extern void BattleSetup_SetMember(s32 sideNo, s32 idx, s32 chara, s32 costume, s32 variant, s32 cpuLevel, f32 health, void *items);
extern void BattleSetup_Finish(void);
extern int Port_DataFingerprint(unsigned long long *out); /* plat_fprint.c: 1 known, 0 being worked out, -1 not done */
extern int Port_DataFingerprintOriginal(void);           /* 1 the data the game computes with is the original disc's */
extern const char *Port_DataFingerprintText(void);
extern int Port_Setting(const char *name, int def);
extern void Port_SettingSave(const char *name, int value);
extern void Port_SettingsWrite(void);

/* the screens */
#define TB_HUB 0
#define TB_CREATE 1
#define TB_JOIN 2
#define TB_WAIT 3
#define TB_PROFILE 4
#define TB_NAME 5
#define TB_QUICK 6
#define TB_SPAR 7 /* Train While Waiting: the fighters of the sparring fight */
#define TB_BLOCK 8 /* "Cannot Go Online": this game's data is not the disc's */
#define TB_FIGHTERS 161
#define TB_TOURNAMENT_STAGE 4 /* the World Tournament Stage, at noon */

/* the hub's modes, in the order of the list */
#define HUB_QUICK 0
#define HUB_RANKED 1
#define HUB_CREATE 2
#define HUB_JOIN 3
#define HUB_FRIENDS 4
#define HUB_TOURNAMENT 5
#define HUB_REPLAYS 6
#define HUB_LEADERBOARD 7
#define HUB_PROFILE 8
#define HUB_COUNT 9

/* Create Room's rows */
#define CR_BATTLE 0
#define CR_TIME 1
#define CR_DELAY 2
#define CR_ROLL 3
#define CR_OPEN 4
#define CR_COUNT 5

/* Profile's rows */
#define PR_NAME 0
#define PR_DELAY 1
#define PR_ROLL 2
#define PR_COUNT 3

#define KEY_COLS 9
#define KEY_ROWS 4
#define CODE_LEN 6

typedef struct TboScreen {
    s32 frames;
    s32 mode;
    s32 modeT;
    s32 leaving;
    s32 train;          /* it is left for the Ultimate Training, the room still open */
    s32 note;
    char noteLine[96];
    s32 hubCur;
    f32 hubY;           /* where the hub's cursor plate is drawn: it glides */
    /* the host's choices (kept in the settings) */
    s32 battle;         /* 0 one on one, 1 teams of up to five */
    s32 time;           /* 0..4: 60, 90, 180, 240 seconds, no limit */
    s32 delay;          /* -1 automatic, else frames */
    s32 roll;           /* 0 off, else frames (2..8) */
    s32 crCur;
    s32 prCur;
    /* the keypad */
    char code[CODE_LEN + 1];
    s32 codeLen;
    s32 keyCol;
    s32 keyRow;         /* KEY_ROWS: the row of DELETE / JOIN */
    /* the search */
    s32 host;           /* this side opened the room */
    s32 waitFrom;       /* frames when the search began */
    s32 found;          /* the other player is there: the match is starting */
    s32 waitBack;       /* the screen a cancelled search goes back to */
    /* Quick Match */
    s32 queue;          /* 0 one on one, 1 teams */
    s32 qkCur;
    s32 asked;          /* frames when the queues were last asked about */
    /* Train While Waiting */
    s32 spMe;           /* your fighter */
    s32 spFoe;          /* your sparring partner */
    s32 spCur;
    s32 blockBack;      /* the screen "Cannot Go Online" goes back to */
    s32 typing;         /* the PC keyboard types (circle on a keypad); else its keys are the pad's, as everywhere */
    s32 lower;          /* the name pad shows small letters */
    /* typing the name */
    char typed[20];
    s32 typedLen;
} TboScreen;

static TboScreen *sTb;
static s32 sTbBgKind;  /* what the search left open for "Train While Waiting" is: 0 joining, 1 hosting, 2 a quick match */
static s32 sTbBgQueue;
static s32 sTbBgFrames; /* how long it had been searching when the sparring fight began; the sign counts on from it */
s32 gPortTboSpar;       /* the battle that runs is Train While Waiting's sparring fight (its sign, its pause menu) */
static char sTbBgCode[CODE_LEN + 1];
static void Tb_Prompt(s32 kind, char *label);
static void Tb_Pill(s32 id, f32 x0, f32 y0, f32 x1, f32 y1, s32 alpha);
static void Tb_Text(f32 x, f32 y, char *s, f32 scale, f32 width, s32 style);
static f32 Tb_TextTop(f32 middle, f32 scale);
static void Tb_Align(s32 align);
static f32 Tb_Beat(s32 period);

static char *sTbHubName[HUB_COUNT] = {"Quick Match", "Ranked Match", "Create Room", "Join Room", "Friends", "Tournament", "Replays", "Leaderboard", "Profile & Settings"};
static const s32 sTbHubReady[HUB_COUNT] = {1, 0, 1, 1, 0, 0, 0, 0, 1};
static char *sTbHubSay[HUB_COUNT] = {
    "Fight whoever is searching right now! No time limit, and you can train while you wait.",
    "Fights that move your rank, season by season. That one is coming soon!",
    "Open a room with your own rules, then give your rival its code. No port forwarding needed!",
    "Got a room code from your rival? Type it in and jump right into their room!",
    "Your rivals, who is online, and invites. That one is coming soon!",
    "The Tenkaichi Budokai itself: brackets of 4 to 16 players. That one is coming soon!",
    "Your last matches, saved to watch again. That one is coming soon!",
    "The strongest in the world, among your friends, and with each fighter. Coming soon!",
    "The name your rivals see, and how your connection plays.",
};
static char *sTbTime[5] = {"60 SECONDS", "90 SECONDS", "180 SECONDS", "240 SECONDS", "NO LIMIT"};
static char sTbKeys[KEY_ROWS][KEY_COLS + 1] = {"ABCDEFGHI", "JKLMNOPQR", "STUVWXYZ0", "123456789"};

/* ---------------------------------------------------------------------------------------------- small things */
static void Tb_Copy(char *dst, const char *src, s32 max) {
    s32 i;

    for (i = 0; i < max - 1 && src[i] != 0; i++) {
        dst[i] = src[i];
    }
    dst[i] = 0;
}

static s32 Tb_Len(const char *s) {
    s32 n = 0;

    while (s[n] != 0) {
        n++;
    }
    return n;
}

static f32 Tb_Beat(s32 period) {
    s32 k = sTb->frames % period;

    return (f32)(k * 2 < period ? k * 2 : (period - k) * 2) / (f32)period;
}

static f32 Tb_In(s32 delay) {
    f32 t = (f32)(sTb->modeT - delay) / 16.0f;

    if (t < 0.0f) {
        t = 0.0f;
    }
    if (t > 1.0f) {
        t = 1.0f;
    }
    t = 1.0f - t;
    return 1.0f - t * t * t;
}

static void Tb_Note(char *line) {
    Tb_Copy(sTb->noteLine, line, 96);
    sTb->note = 150;
}

static void Tb_SetMode(s32 mode) {
    sTb->typing = mode == TB_JOIN || mode == TB_NAME; /* the keyboard types on a keypad (elsewhere I is triangle ...) */
    Port_TextMode(sTb->typing);
    sTb->mode = mode;
    sTb->modeT = 0;
}

static s32 Tb_Move(s32 *cur, s32 count) {
    Pad *pad = &gPad[0];
    s32 was = *cur;

    if (pad->gameRepeat & (PADG_DOWN | PADG_DOWN2)) {
        *cur = (*cur + 1) % count;
    } else if (pad->gameRepeat & (PADG_UP | PADG_UP2)) {
        *cur = (*cur + count - 1) % count;
    }
    if (*cur != was) {
        Snd_PlaySe(1, 0);
    }
    return *cur != was;
}

static s32 Tb_Side(void) {
    Pad *pad = &gPad[0];

    if (pad->gameRepeat & (PADG_RIGHT | PADG_RIGHT2)) {
        return 1;
    }
    if (pad->gameRepeat & (PADG_LEFT | PADG_LEFT2)) {
        return -1;
    }
    return 0;
}

static s32 Tb_Wrap(s32 v, s32 lo, s32 hi) {
    return v < lo ? hi : v > hi ? lo : v;
}

static void Tb_SaveChoices(void) {
    Port_SettingSave("tbo_battle", sTb->battle);
    Port_SettingSave("tbo_time", sTb->time);
    Port_SettingSave("net_delay2", sTb->delay);
    Port_SettingSave("net_rollback2", sTb->roll);
    Port_SettingsWrite();
}

static void Tb_LoadChoices(void) {
    sTb->battle = Port_Setting("tbo_battle", 0);
    sTb->time = Port_Setting("tbo_time", 3);
    sTb->delay = Port_Setting("net_delay2", -1);
    sTb->roll = Port_RollCan() ? Port_Setting("net_rollback2", 8) : 0;
    if (sTb->battle < 0 || sTb->battle > 1) {
        sTb->battle = 0;
    }
    if (sTb->time < 0 || sTb->time > 4) {
        sTb->time = 3;
    }
    if (sTb->delay < -1 || sTb->delay > 6) {
        sTb->delay = -1;
    }
    if (sTb->roll < 0 || sTb->roll > 8) {
        sTb->roll = 8;
    }
    sTb->roll &= ~1;
    sTb->queue = Port_Setting("tbo_queue", 0) == 1;
    sTb->spMe = Port_Setting("tbo_spar_me", 0);
    sTb->spFoe = Port_Setting("tbo_spar_foe", 31);
    if (sTb->spMe < 0 || sTb->spMe >= TB_FIGHTERS) {
        sTb->spMe = 0;
    }
    if (sTb->spFoe < 0 || sTb->spFoe >= TB_FIGHTERS) {
        sTb->spFoe = 31;
    }
}

static void Tb_DelayText(char *out, s32 delay) {
    if (delay < 0) {
        sprintf(out, "AUTOMATIC");
    } else {
        sprintf(out, "%d FRAME%s", (int)delay, delay == 1 ? "" : "S");
    }
}

static void Tb_RollText(char *out, s32 roll) {
    if (!Port_RollCan()) {
        sprintf(out, "NOT ON THIS PC");
    } else if (roll <= 0) {
        sprintf(out, "OFF");
    } else {
        sprintf(out, "UP TO %d FRAMES", (int)roll);
    }
}

/* ---------------------------------------------------------------------------------------------- the rule */
/* Online matches are only between two unmodded games. Fighter mods are switched off by themselves while a session
   runs (plat_mods.c); what is checked here is the rest of the data the game computes with, against the original
   disc (the gameplay fingerprint, plat_fprint.c: files added under gamedata/mods count). Returns 1 when a search may
   start; else the note or the "Cannot Go Online" window says why. */
static s32 Tb_Vanilla(void) {
    s32 state = Port_DataFingerprint(NULL);

    if (state == 0) {
        Tb_Note("CHECKING YOUR GAME DATA AGAINST THE DISC... TRY AGAIN IN A MOMENT");
        return 0;
    }
    if (state == 1 && Port_DataFingerprintOriginal() == 1) {
        return 1;
    }
    sTb->blockBack = sTb->mode;
    Snd_PlaySe(1, 2);
    Tb_SetMode(TB_BLOCK);
    return 0;
}

static void Tb_BlockUpdate(s32 live) {
    Pad *pad = &gPad[0];

    Tb_Prompt(0, "Back");
    if (live && (pad->gamePressed & (PADG_CROSS | PADG_CIRCLE | PADG_TRIANGLE))) {
        Snd_PlaySe(1, 2);
        Tb_SetMode(sTb->blockBack);
    }
}

/* ---------------------------------------------------------------------------------------------- the search */
static void Tb_BeginSearch(s32 host) {
    s32 state;

    if (!Tb_Vanilla()) {
        return;
    }
    if (host) {
        Port_NetOptions(sTb->roll, sTb->delay < 0 ? -2 : sTb->delay);
        Port_NetRulesSet(sTb->battle, 0, sTb->time);
        Tb_SaveChoices();
    } else {
        Port_NetOptions(-1, -1); /* joining: the host's choices arrive with its answer */
    }
    Port_NetNameGet(); /* (the saved name is the one sent) */
    state = Port_LobbyStartCode(host, host ? "" : sTb->code);
    if (state < 0) {
        Tb_Note("THE NETWORK COULD NOT BE OPENED ON THIS PC");
        return;
    }
    sTb->host = host;
    sTb->waitFrom = sTb->frames;
    sTb->found = 0;
    sTb->waitBack = sTb->mode;
    Tb_SetMode(TB_WAIT);
}

/* What the search says (each frame on the screens that wait): the rival came (the match starts), or it failed.
   Returns 1 when the screen that called has nothing more to do this frame. */
static s32 Tb_SearchNews(void) {
    s32 state = sTb->found ? 2 : Port_LobbyPoll();

    if (state == 2 && !sTb->found) {
        sTb->found = 1;
        Snd_PlaySe(1, 1);
        Tb_SetMode(TB_WAIT);
        Port_LobbyLaunch(); /* the session begins at the game's next blank, from the game's first moment */
        return 1;
    }
    if (state <= 0 && !sTb->found) {
        if (state == -3) {
            Tb_Note((char *)Port_LobbyError());
        } else if (state == -4) {
            Tb_Note("THE TWO GAMES COULD NOT REACH EACH OTHER, EVEN THROUGH THE RELAY");
        } else if (state < 0) {
            Tb_Note("THE ROOM COULD NOT BE OPENED");
        }
        Port_LobbyCancel();
        Tb_SetMode(sTb->waitBack == TB_WAIT || sTb->waitBack == TB_SPAR ? TB_HUB : sTb->waitBack);
        return 1;
    }
    return sTb->found;
}

static void Tb_WaitUpdate(s32 live) {
    Pad *pad = &gPad[0];

    if (!sTb->found) {
        if (sTb->host == 1 && Port_LobbyCode()[0] != 0) {
            Tb_Prompt(0, "Copy Code");
        }
        Tb_Prompt(1, "Train While Waiting");
        Tb_Prompt(4, "Cancel");
    }
    if (Tb_SearchNews()) {
        return;
    }
    if (!live || sTb->found) {
        return;
    }
    if ((pad->gamePressed & PADG_CROSS) && sTb->host == 1 && Port_LobbyCode()[0] != 0) { /* the code, to paste to the rival */
        PortTbo_Copy(Port_LobbyCode());
        Snd_PlaySe(1, 1);
        Tb_Note("COPIED: PASTE THE CODE TO YOUR RIVAL");
    } else if (pad->gamePressed & PADG_SQUARE) { /* the fighters of the sparring fight first; the search goes on */
        Snd_PlaySe(1, 1);
        sTb->spCur = 2;
        Tb_SetMode(TB_SPAR);
    } else if (pad->gamePressed & (PADG_CIRCLE | PADG_TRIANGLE)) {
        Snd_PlaySe(1, 2);
        Port_LobbyCancel();
        Tb_SetMode(sTb->waitBack == TB_WAIT ? TB_HUB : sTb->waitBack);
    }
}

/* ---------------------------------------------------------------------------------------------- Train While Waiting */
static char *Tb_Fighter(s32 chara) {
    static char name[TB_FIGHTERS][48];
    static u8 got[TB_FIGHTERS];

    if (chara < 0 || chara >= TB_FIGHTERS) {
        return "-";
    }
    if (!got[chara]) {
        sprintf(name[chara], "%.47s", chara < (s32)(sizeof(sTbNames) / sizeof(sTbNames[0])) ? sTbNames[chara] : "?");
        got[chara] = 1;
    }
    return name[chara];
}

static void Tb_SparUpdate(s32 live) {
    Pad *pad = &gPad[0];
    s32 *who;

    Tb_Prompt(3, "Change");
    Tb_Prompt(0, sTb->spCur == 2 ? "Start Training" : "Next");
    Tb_Prompt(2, "Back");
    if (Tb_SearchNews()) {
        return;
    }
    if (!live) {
        return;
    }
    Tb_Move(&sTb->spCur, 3);
    who = sTb->spCur == 0 ? &sTb->spMe : sTb->spCur == 1 ? &sTb->spFoe : NULL;
    if (who != NULL) {
        s32 step = Tb_Side();

        if (pad->gameRepeat & PADG_R1) {
            step = 10;
        } else if (pad->gameRepeat & PADG_L1) {
            step = -10;
        }
        if (step != 0) {
            *who = (*who + step + TB_FIGHTERS * 4) % TB_FIGHTERS;
            Snd_PlaySe(1, 0);
        }
    }
    if (pad->gamePressed & (PADG_CROSS | PADG_START)) {
        if (sTb->spCur == 2 || (pad->gamePressed & PADG_START)) { /* the search stays open: net.c polls it from here on */
            Snd_PlaySe(1, 1);
            Port_SettingSave("tbo_spar_me", sTb->spMe);
            Port_SettingSave("tbo_spar_foe", sTb->spFoe);
            Port_SettingsWrite();
            Port_LobbyBackground(1);
            sTbBgKind = sTb->host;
            sTbBgQueue = sTb->queue;
            sTbBgFrames = sTb->frames - sTb->waitFrom;
            Tb_Copy(sTbBgCode, sTb->code, CODE_LEN + 1);
            sTb->train = 1;
            sTb->leaving = 1;
            ColorFade_StartOut(0, 0, 0, 0x14);
        } else {
            Snd_PlaySe(1, 0);
            sTb->spCur++;
        }
    } else if (pad->gamePressed & PADG_TRIANGLE) {
        Snd_PlaySe(1, 2);
        Tb_SetMode(TB_WAIT);
    }
}

/* ---------------------------------------------------------------------------------------------- Quick Match */
static void Tb_QuickStart(void) {
    s32 state;

    if (!Tb_Vanilla()) {
        return;
    }
    Port_NetSpeedSet(Port_NetSpeedStandard());
    Port_NetRulesSet(sTb->queue, 0, 4); /* (a queue's rules: its battle type, no time limit, the standard speed) */
    Port_NetOptions(Port_RollCan() ? 8 : 0, -2);
    Port_NetNameSet(Port_NetNameGet());
    Port_SettingSave("tbo_queue", sTb->queue);
    Port_SettingsWrite();
    state = Port_LobbyStartQuick(sTb->queue);
    if (state < 0) {
        Tb_Note("THE NETWORK COULD NOT BE OPENED ON THIS PC");
        return;
    }
    sTb->host = 2;
    sTb->waitFrom = sTb->frames;
    sTb->found = 0;
    sTb->waitBack = TB_QUICK;
    Tb_SetMode(TB_WAIT);
}

static void Tb_QuickUpdate(s32 live) {
    Pad *pad = &gPad[0];

    Tb_Prompt(3, "Change");
    Tb_Prompt(0, sTb->qkCur == 1 ? "Search" : "Next");
    Tb_Prompt(2, "Back");
    if (sTb->frames - sTb->asked > 1800) { /* how many wait in each queue, asked again every half minute */
        sTb->asked = sTb->frames;
        Port_LobbyQueuesAsk();
    }
    if (!live) {
        return;
    }
    Tb_Move(&sTb->qkCur, 2);
    if (sTb->qkCur == 0 && Tb_Side() != 0) {
        sTb->queue = !sTb->queue;
        Snd_PlaySe(1, 0);
    }
    if (pad->gamePressed & (PADG_CROSS | PADG_START)) {
        if (sTb->qkCur == 1 || (pad->gamePressed & PADG_START)) {
            Snd_PlaySe(1, 1);
            Tb_QuickStart();
        } else {
            Snd_PlaySe(1, 0);
            sTb->qkCur = 1;
        }
    } else if (pad->gamePressed & PADG_TRIANGLE) {
        Snd_PlaySe(1, 2);
        Tb_SetMode(TB_HUB);
    }
}

/* ---------------------------------------------------------------------------------------------- the hub */
static void Tb_HubUpdate(s32 live) {
    Pad *pad = &gPad[0];

    Tb_Prompt(0, "Enter");
    Tb_Prompt(2, "Back");
    if (!live) {
        return;
    }
    Tb_Move(&sTb->hubCur, HUB_COUNT);
    if (pad->gamePressed & PADG_CROSS) {
        switch (sTb->hubCur) {
        case HUB_QUICK:
            Snd_PlaySe(1, 1);
            sTb->qkCur = 1;
            sTb->asked = sTb->frames - 100000;
            Tb_SetMode(TB_QUICK);
            break;
        case HUB_CREATE:
            Snd_PlaySe(1, 1);
            sTb->crCur = CR_OPEN;
            Tb_SetMode(TB_CREATE);
            break;
        case HUB_JOIN:
            Snd_PlaySe(1, 1);
            sTb->code[0] = 0;
            sTb->codeLen = 0;
            sTb->keyCol = 0;
            sTb->keyRow = 0;
            Tb_SetMode(TB_JOIN);
            break;
        case HUB_PROFILE:
            Snd_PlaySe(1, 1);
            sTb->prCur = 0;
            Tb_SetMode(TB_PROFILE);
            break;
        default:
            Snd_PlaySe(1, 2);
            Tb_Note("NOT OPEN YET: IT NEEDS MORE FROM THE MATCHMAKING SERVICE THAN ROOM CODES");
            break;
        }
    } else if (pad->gamePressed & PADG_TRIANGLE) {
        Snd_PlaySe(1, 2);
        sTb->leaving = 1;
        ColorFade_StartOut(0, 0, 0, 0x14);
    }
}

/* ---------------------------------------------------------------------------------------------- Create Room */
static void Tb_CreateUpdate(s32 live) {
    Pad *pad = &gPad[0];
    s32 side;

    Tb_Prompt(3, "Change");
    Tb_Prompt(0, sTb->crCur == CR_OPEN ? "Open Room" : "Next");
    Tb_Prompt(2, "Back");
    if (!live) {
        return;
    }
    Tb_Move(&sTb->crCur, CR_COUNT);
    side = Tb_Side();
    if (side != 0) {
        switch (sTb->crCur) {
        case CR_BATTLE:
            sTb->battle = !sTb->battle;
            break;
        case CR_TIME:
            sTb->time = Tb_Wrap(sTb->time + side, 0, 4);
            break;
        case CR_DELAY:
            sTb->delay = Tb_Wrap(sTb->delay + side, -1, 6);
            break;
        case CR_ROLL:
            if (Port_RollCan()) {
                sTb->roll = Tb_Wrap(sTb->roll + side * 2, 0, 8);
            }
            break;
        }
        if (sTb->crCur != CR_OPEN) {
            Snd_PlaySe(1, 0);
        }
    }
    if (pad->gamePressed & (PADG_CROSS | PADG_START)) {
        if (sTb->crCur == CR_OPEN || (pad->gamePressed & PADG_START)) {
            Snd_PlaySe(1, 1);
            Tb_BeginSearch(1);
        } else {
            Snd_PlaySe(1, 0);
            sTb->crCur++;
        }
    } else if (pad->gamePressed & PADG_TRIANGLE) {
        Snd_PlaySe(1, 2);
        Tb_SaveChoices();
        Tb_SetMode(TB_HUB);
    }
}

/* ---------------------------------------------------------------------------------------------- the keypads */
/* A pad keypad: rows of nine keys and, under them, a row of wide buttons, each over a run of the columns. Up / down
   / left / right move, cross presses; circle switches the PC keyboard to typing and back. */
typedef struct TbKeyBtn {
    char *label;
    s32 c0;
    s32 c1;
    s32 act; /* TB_KEY_ */
} TbKeyBtn;
#define TB_KEY_DELETE 1
#define TB_KEY_OK 2
#define TB_KEY_CASE 3
#define TB_KEY_SPACE 4

static const TbKeyBtn sTbJoinBtn[2] = {{"DELETE", 0, 3, TB_KEY_DELETE}, {"JOIN", 5, 8, TB_KEY_OK}};
static const TbKeyBtn sTbNameBtn[4] = {{"Aa", 0, 1, TB_KEY_CASE}, {"SPACE", 2, 4, TB_KEY_SPACE}, {"DELETE", 5, 6, TB_KEY_DELETE}, {"DONE", 7, 8, TB_KEY_OK}};
#define NAME_ROWS 5
static char sTbNameKeys[2][NAME_ROWS][KEY_COLS + 1] = {{"ABCDEFGHI", "JKLMNOPQR", "STUVWXYZ0", "123456789", "-_.!?#&*+"},
                                                     {"abcdefghi", "jklmnopqr", "stuvwxyz0", "123456789", "-_.!?#&*+"}};

static s32 Tb_BtnAt(const TbKeyBtn *b, s32 nb, s32 col) {
    s32 k;

    for (k = 0; k < nb; k++) {
        if (col >= b[k].c0 && col <= b[k].c1) {
            return k;
        }
    }
    return 0;
}

/* The cursor's moves. Returns 0, a key's letter, or -(TB_KEY_) for a button, when cross is pressed. */
static s32 Tb_PadUpdate(s32 rows, char (*keys)[KEY_COLS + 1], const TbKeyBtn *b, s32 nb) {
    Pad *pad = &gPad[0];

    if (sTb->keyRow == rows) { /* the buttons' row: a step goes to the next button */
        s32 k = Tb_BtnAt(b, nb, sTb->keyCol);

        if (pad->gameRepeat & (PADG_RIGHT | PADG_RIGHT2)) {
            sTb->keyCol = b[(k + 1) % nb].c0;
            Snd_PlaySe(1, 0);
        } else if (pad->gameRepeat & (PADG_LEFT | PADG_LEFT2)) {
            sTb->keyCol = b[(k + nb - 1) % nb].c0;
            Snd_PlaySe(1, 0);
        }
    } else if (pad->gameRepeat & (PADG_RIGHT | PADG_RIGHT2)) {
        sTb->keyCol = (sTb->keyCol + 1) % KEY_COLS;
        Snd_PlaySe(1, 0);
    } else if (pad->gameRepeat & (PADG_LEFT | PADG_LEFT2)) {
        sTb->keyCol = (sTb->keyCol + KEY_COLS - 1) % KEY_COLS;
        Snd_PlaySe(1, 0);
    }
    if (pad->gameRepeat & (PADG_DOWN | PADG_DOWN2)) {
        sTb->keyRow = (sTb->keyRow + 1) % (rows + 1);
        Snd_PlaySe(1, 0);
    } else if (pad->gameRepeat & (PADG_UP | PADG_UP2)) {
        sTb->keyRow = (sTb->keyRow + rows) % (rows + 1);
        Snd_PlaySe(1, 0);
    }
    if (sTb->keyRow == rows) {
        sTb->keyCol = b[Tb_BtnAt(b, nb, sTb->keyCol)].c0;
    }
    if (pad->gamePressed & PADG_CROSS) {
        return sTb->keyRow < rows ? (u8)keys[sTb->keyRow][sTb->keyCol] : -b[Tb_BtnAt(b, nb, sTb->keyCol)].act;
    }
    return 0;
}

/* Draws a keypad at (x, y): its keys, the cursor and the buttons' row. */
static void Tb_Keypad(f32 x, f32 y, s32 rows, char (*keys)[KEY_COLS + 1], const TbKeyBtn *b, s32 nb) {
    char one[2] = {0, 0};
    s32 r;
    s32 c;
    s32 k;
    f32 by = y + 34.0f * (f32)rows + 4.0f;

    for (r = 0; r < rows; r++) {
        for (c = 0; c < KEY_COLS; c++) {
            f32 kx = x + 36.0f * (f32)c;
            f32 ky = y + 34.0f * (f32)r;
            s32 on = r == sTb->keyRow && c == sTb->keyCol;

            if (on) {
                Tb_Pill(TBO_KIT_SEL, kx - 2.0f, ky - 2.0f, kx + 34.0f, ky + 30.0f, 0x80);
            } else {
                Tb_Pill(TBO_KIT_BAR, kx, ky, kx + 32.0f, ky + 28.0f, 0x80);
            }
            one[0] = keys[r][c];
            Tb_Align(1);
            Tb_Text(kx + 16.0f, Tb_TextTop(ky + 14.0f, 0.72f), one, 0.72f, 0.0f, on ? 1 : 0);
            Tb_Align(0);
        }
    }
    for (k = 0; k < nb; k++) {
        f32 x0 = x + 36.0f * (f32)b[k].c0;
        f32 x1 = x + 36.0f * (f32)b[k].c1 + 32.0f;
        s32 on = sTb->keyRow == rows && sTb->keyCol >= b[k].c0 && sTb->keyCol <= b[k].c1;

        Tb_Pill(on ? TBO_KIT_SEL : b[k].act == TB_KEY_OK ? TBO_KIT_GOLD : TBO_KIT_BAR, x0, by, x1, by + 28.0f, 0x80);
        Tb_Align(1);
        Tb_Text((x0 + x1) * 0.5f, Tb_TextTop(by + 14.0f, 0.6f), b[k].label, 0.6f, x1 - x0 - 12.0f, on ? 1 : b[k].act == TB_KEY_OK ? 2 : 0);
        Tb_Align(0);
    }
}

/* ---------------------------------------------------------------------------------------------- Join Room */
static void Tb_CodeAdd(s32 c) {
    if (c >= 'a' && c <= 'z') {
        c -= 'a' - 'A';
    }
    if (((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) && sTb->codeLen < CODE_LEN) {
        sTb->code[sTb->codeLen++] = (char)c;
        sTb->code[sTb->codeLen] = 0;
        Snd_PlaySe(1, 0);
    }
}

static void Tb_CodeBack(void) {
    if (sTb->codeLen > 0) {
        sTb->code[--sTb->codeLen] = 0;
        Snd_PlaySe(1, 2);
    }
}

static void Tb_JoinTry(void) {
    if (sTb->codeLen == CODE_LEN) {
        Snd_PlaySe(1, 1);
        Tb_BeginSearch(0);
    } else {
        Snd_PlaySe(1, 2);
        Tb_Note("A ROOM CODE HAS SIX LETTERS AND NUMBERS");
    }
}

static void Tb_JoinUpdate(s32 live) {
    Pad *pad = &gPad[0];
    s32 c;

    Tb_Prompt(0, "Enter");
    Tb_Prompt(1, "Delete");
    Tb_Prompt(2, "Back");
    if (!live) {
        return;
    }
    while (sTb->typing && (c = Port_TextRead()) != 0) { /* the PC keyboard: letters, backspace, enter, escape */
        if (c == 13) {
            Tb_JoinTry();
            return;
        } else if (c == 27) { /* Esc: back, as triangle */
            Snd_PlaySe(1, 2);
            Tb_SetMode(TB_HUB);
            return;
        } else if (c == 8) {
            Tb_CodeBack();
        } else {
            Tb_CodeAdd(c);
        }
    }
    c = Tb_PadUpdate(KEY_ROWS, sTbKeys, sTbJoinBtn, 2);
    if (c > 0) {
        Tb_CodeAdd(c);
        if (sTb->codeLen == CODE_LEN) { /* full: the cursor goes to JOIN */
            sTb->keyRow = KEY_ROWS;
            sTb->keyCol = sTbJoinBtn[1].c0;
        }
    } else if (c == -TB_KEY_DELETE) {
        Tb_CodeBack();
    } else if (c == -TB_KEY_OK || (pad->gamePressed & PADG_START)) {
        Tb_JoinTry();
    } else if (pad->gamePressed & PADG_SQUARE) {
        Tb_CodeBack();
    } else if (pad->gamePressed & PADG_TRIANGLE) {
        Snd_PlaySe(1, 2);
        Tb_SetMode(TB_HUB);
    }
}

/* ---------------------------------------------------------------------------------------------- Profile */
static void Tb_ProfileUpdate(s32 live) {
    Pad *pad = &gPad[0];
    s32 side;

    Tb_Prompt(3, "Change");
    Tb_Prompt(0, sTb->prCur == PR_NAME ? "Type Name" : "OK");
    Tb_Prompt(2, "Back");
    if (!live) {
        return;
    }
    Tb_Move(&sTb->prCur, PR_COUNT);
    side = Tb_Side();
    if (side != 0 && sTb->prCur == PR_DELAY) {
        sTb->delay = Tb_Wrap(sTb->delay + side, -1, 6);
        Snd_PlaySe(1, 0);
    } else if (side != 0 && sTb->prCur == PR_ROLL && Port_RollCan()) {
        sTb->roll = Tb_Wrap(sTb->roll + side * 2, 0, 8);
        Snd_PlaySe(1, 0);
    }
    if ((pad->gamePressed & PADG_CROSS) && sTb->prCur == PR_NAME) {
        Snd_PlaySe(1, 1);
        Tb_Copy(sTb->typed, Port_NetNameGet(), 20);
        sTb->typedLen = Tb_Len(sTb->typed);
        Tb_SetMode(TB_NAME);
        sTb->keyRow = 0;
        sTb->keyCol = 0;
    } else if (pad->gamePressed & PADG_TRIANGLE) {
        Snd_PlaySe(1, 2);
        Tb_SaveChoices();
        Tb_SetMode(TB_HUB);
    }
}

static void Tb_NameAdd(s32 c) {
    if (c >= 32 && c < 127 && sTb->typedLen < 16) {
        sTb->typed[sTb->typedLen++] = (char)c;
        sTb->typed[sTb->typedLen] = 0;
        Snd_PlaySe(1, 0);
    }
}

static void Tb_NameBack(void) {
    if (sTb->typedLen > 0) {
        sTb->typed[--sTb->typedLen] = 0;
        Snd_PlaySe(1, 2);
    }
}

static void Tb_NameUpdate(s32 live) {
    Pad *pad = &gPad[0];
    s32 c;
    s32 done = 0;

    Tb_Prompt(0, "Enter");
    Tb_Prompt(1, "Delete");
    Tb_Prompt(2, "Cancel");
    if (!live) {
        return;
    }
    while (sTb->typing && (c = Port_TextRead()) != 0) {
        if (c == 13) {
            done = 1;
        } else if (c == 27) { /* Esc: cancel, as triangle */
            done = -1;
            break;
        } else if (c == 8) {
            Tb_NameBack();
        } else {
            Tb_NameAdd(c);
        }
    }
    if (!done) {
        c = Tb_PadUpdate(NAME_ROWS, sTbNameKeys[sTb->lower], sTbNameBtn, 4);
        if (c > 0) {
            Tb_NameAdd(c);
        } else if (c == -TB_KEY_SPACE) {
            Tb_NameAdd(' ');
        } else if (c == -TB_KEY_CASE) {
            sTb->lower = !sTb->lower;
            Snd_PlaySe(1, 0);
        } else if (c == -TB_KEY_DELETE || (pad->gamePressed & PADG_SQUARE)) {
            Tb_NameBack();
        } else if (c == -TB_KEY_OK || (pad->gamePressed & PADG_START)) {
            done = 1;
        } else if (pad->gamePressed & PADG_TRIANGLE) {
            done = -1;
        }
    }
    if (done == 1) {
        while (sTb->typedLen > 0 && sTb->typed[sTb->typedLen - 1] == ' ') {
            sTb->typed[--sTb->typedLen] = 0;
        }
        if (sTb->typedLen > 0) {
            Port_NetNameSave(sTb->typed);
            Tb_Note("SAVED: YOUR RIVALS WILL SEE THIS NAME");
        }
        Snd_PlaySe(1, 1);
        Tb_SetMode(TB_PROFILE);
    } else if (done == -1) {
        Snd_PlaySe(1, 2);
        Tb_SetMode(TB_PROFILE);
    }
}

/* ---------------------------------------------------------------------------------------------- what is drawn */
/* ---------------------------------------------------------------------------------------------- the pictures */
/* Everything on these screens is drawn from tbo/ui/*.png at the pictures' own resolution (include/menu/tbo_art.h:
   the renderer reads them, port/src/gs/gs_draw.c tbo_texture), on a 4:3 page in the middle of the window so that
   nothing is stretched on a wide one; the tournament grounds fill the whole window behind it. Positions below are
   in the game's 512 x 448 units of that page (a unit is 7:6 wide, hence the 0.857s). */
extern int Port_PageAspectMilli(void); /* port/src/plat_stub.c: the shape the menus are shown in (4:3 with "4:3 in menus", else the window's), width / height * 1000 */

static f32 sTbPx, sTbPy, sTbPw = 512.0f, sTbPh = 448.0f; /* the page on the screen, set once a frame */
static s32 sTbAlign;                                      /* 0 a line starts at its x, 1 is centred on it, 2 ends there */

static void Tb_PageSet(void) {
    f32 aspect = (f32)Port_PageAspectMilli() / 1000.0f;
    f32 page = 4.0f / 3.0f;

    if (aspect >= page) { /* wider: the whole height, the grounds at the sides */
        sTbPh = 448.0f;
        sTbPw = 512.0f * page / aspect;
    } else {
        sTbPw = 512.0f;
        sTbPh = 448.0f * aspect / page;
    }
    sTbPx = (512.0f - sTbPw) * 0.5f;
    sTbPy = (448.0f - sTbPh) * 0.5f;
}

static f32 Tb_X(f32 x) {
    return sTbPx + x * sTbPw / 512.0f;
}

#define TB_BAR_LEFT -30.0f /* the hub's bars start here (off the page: on a 4:3 screen, off its edge) */

/* Where the window's left edge is, in the page's units (0 on a 4:3 window, less on a wider one). */
static f32 Tb_Left(void) {
    return -sTbPx * 512.0f / sTbPw;
}

/* The left of the crest and the title: in line with the bars, but on the screen. */
static f32 Tb_TitleLeft(void) {
    f32 l = Tb_Left() + 6.0f;

    return l > TB_BAR_LEFT ? l : TB_BAR_LEFT;
}

static f32 Tb_Y(f32 y) {
    return sTbPy + y * sTbPh / 448.0f;
}

/* 0..1 of the picture as a UV coordinate: 1024 texels in 1/16ths, held to the register's 14 bits */
static u32 Tb_Uv(f32 t) {
    s32 v = (s32)(t * 1024.0f * 16.0f);

    return v < 0 ? 0 : v > 0x3FFF ? 0x3FFF : (u32)v;
}

/* A picture's part (u, v in 0..1) over a box of the SCREEN, tinted (0x80 per channel = as it is). */
static void Tb_SpriteScreen(s32 id, f32 x0, f32 y0, f32 x1, f32 y1, f32 u0, f32 v0, f32 u1, f32 v1, u32 tint, s32 alpha) {
    u64 *p;

    if (alpha <= 0 || x1 <= x0 || y1 <= y0) {
        return;
    }
    if (alpha > 0x80) {
        alpha = 0x80;
    }
    p = Dma_BeginDirect();
    p[0] = 4 | (1ULL << 15) | (1ULL << 60); /* A+D x 4 */
    p[1] = 0xE;
    p += 2;
    p[0] = 0x44;
    p[1] = 0x42; /* ALPHA_1: blended by the source's alpha */
    p += 2;
    p[0] = 0x60;
    p[1] = 0x14; /* TEX1_1: bilinear */
    p += 2;
    p[0] = 0x30000;
    p[1] = 0x47; /* TEST_1: no alpha test, depth always passes */
    p += 2;
    p[0] = 5;
    p[1] = 0x08; /* CLAMP_1: clamped both ways */
    p += 2;
    p[0] = 1 | (1ULL << 15) | (1ULL << 58) | (8ULL << 60);
    p[1] = 0x535306E1; /* RGBAQ, (A+D: nothing), TEX0_1, PRIM, UV, XYZ2, UV, XYZ2 */
    p += 2;
    p[0] = (u64)(tint & 0xFFFFFF) | ((u64)alpha << 24);
    p[1] = 0;
    p += 2;
    /* TEX0: base 1, width 1, format TBO_PSM, 1024 x 1024, the texture's alpha, modulated, the picture's number where
       the palette address goes */
    p[0] = 1ULL | (1ULL << 14) | ((u64)TBO_PSM << 20) | (10ULL << 26) | (10ULL << 30) | (1ULL << 34) | ((u64)(id & 0x3FFF) << 37);
    p[1] = 0x156; /* PRIM: sprite, textured, blended, UV */
    p += 2;
    p[0] = (u64)Tb_Uv(u0) | ((u64)Tb_Uv(v0) << 16);
    p[1] = (u64)((s32)(x0 * 16.0f) + 0x7000) | ((u64)((s32)(y0 * 16.0f) + 0x7200) << 16);
    p += 2;
    p[0] = (u64)Tb_Uv(u1) | ((u64)Tb_Uv(v1) << 16);
    p[1] = (u64)((s32)(x1 * 16.0f) + 0x7000) | ((u64)((s32)(y1 * 16.0f) + 0x7200) << 16);
    p += 2;
    Dma_EndDirect(p);
}

/* The same over a box of the page. */
static void Tb_Sprite(s32 id, f32 x0, f32 y0, f32 x1, f32 y1, f32 u0, f32 v0, f32 u1, f32 v1, u32 tint, s32 alpha) {
    Tb_SpriteScreen(id, Tb_X(x0), Tb_Y(y0), Tb_X(x1), Tb_Y(y1), u0, v0, u1, v1, tint, alpha);
}

#define TB_PLAIN 0x808080

static f32 Tb_PicW(s32 id) {
    return (f32)sTboSize[id][0];
}

static f32 Tb_PicH(s32 id) {
    return (f32)sTboSize[id][1];
}

/* The tournament grounds over the whole window: the middle of the 16:9 picture, as wide as the window is. */
static void Tb_Backdrop(void) {
    f32 aspect = (f32)Port_PageAspectMilli() / 1000.0f;
    f32 pic = Tb_PicW(TBO_BG) / Tb_PicH(TBO_BG);
    f32 u = aspect < pic ? aspect / pic : 1.0f;
    f32 v = aspect < pic ? 1.0f : pic / aspect;

    Tb_SpriteScreen(TBO_BG, 0.0f, 0.0f, 512.0f, 448.0f, 0.5f - u * 0.5f, 0.5f - v * 0.5f, 0.5f + u * 0.5f, 0.5f + v * 0.5f, TB_PLAIN, 0x80);
}

/* A picture standing on `feet`, centred on cx, `height` units tall (its own shape kept). */
static void Tb_Stand(s32 id, f32 cx, f32 feet, f32 height, s32 alpha) {
    f32 w = height * Tb_PicW(id) / Tb_PicH(id) * 0.857f;

    Tb_Sprite(id, cx - w * 0.5f, feet - height, cx + w * 0.5f, feet, 0.0f, 0.0f, 1.0f, 1.0f, TB_PLAIN, alpha);
}

/* A picture centred on (cx, cy), `height` units tall. */
static void Tb_Icon(s32 id, f32 cx, f32 cy, f32 height, s32 alpha) {
    Tb_Stand(id, cx, cy + height * 0.5f, height, alpha);
}

/* Goten and Trunks in a pose (TBO_HOSTS_*), standing on `feet` around `cx`, `height` units tall; they breathe. */
static void Tb_Hosts(s32 pose, f32 cx, f32 feet, f32 height) {
    Tb_Stand(pose, cx, feet + 2.0f * Tb_Beat(90) - 1.0f, height, 0x80);
}

/* A piece of the kit as a bar: its round ends keep their shape, its middle stretches. */
static void Tb_Pill(s32 id, f32 x0, f32 y0, f32 x1, f32 y1, s32 alpha) {
    f32 w = Tb_PicW(id);
    f32 h = Tb_PicH(id);
    f32 c = (f32)sTboSize[id][2];
    f32 cap = c * (y1 - y0) / h * 0.857f;
    f32 cu = c / w;

    if (cap * 2.0f > x1 - x0) {
        cap = (x1 - x0) * 0.5f;
    }
    Tb_Sprite(id, x0, y0, x0 + cap, y1, 0.0f, 0.0f, cu, 1.0f, TB_PLAIN, alpha);
    Tb_Sprite(id, x0 + cap, y0, x1 - cap, y1, cu, 0.0f, 1.0f - cu, 1.0f, TB_PLAIN, alpha);
    Tb_Sprite(id, x1 - cap, y0, x1, y1, 1.0f - cu, 0.0f, 1.0f, 1.0f, TB_PLAIN, alpha);
}

/* A piece of the kit as a box: its corners keep their shape, its sides and middle stretch. */
static void Tb_Panel(s32 id, f32 x0, f32 y0, f32 x1, f32 y1, s32 alpha) {
    f32 w = Tb_PicW(id);
    f32 h = Tb_PicH(id);
    f32 c = (f32)sTboSize[id][2];
    f32 cy = c * 0.22f;
    f32 cx;
    f32 us[4];
    f32 vs[4];
    f32 xs[4];
    f32 ys[4];
    s32 i;
    s32 j;

    if (cy > (y1 - y0) * 0.34f) {
        cy = (y1 - y0) * 0.34f;
    }
    cx = cy * 0.857f;
    us[0] = 0.0f;
    us[1] = c / w;
    us[2] = 1.0f - c / w;
    us[3] = 1.0f;
    vs[0] = 0.0f;
    vs[1] = c / h;
    vs[2] = 1.0f - c / h;
    vs[3] = 1.0f;
    xs[0] = x0;
    xs[1] = x0 + cx;
    xs[2] = x1 - cx;
    xs[3] = x1;
    ys[0] = y0;
    ys[1] = y0 + cy;
    ys[2] = y1 - cy;
    ys[3] = y1;
    for (j = 0; j < 3; j++) {
        for (i = 0; i < 3; i++) {
            Tb_Sprite(id, xs[i], ys[j], xs[i + 1], ys[j + 1], us[i], vs[j], us[i + 1], vs[j + 1], TB_PLAIN, alpha);
        }
    }
}

/* The shapes of the radar, on the page. */
static void Tb_Quad(f32 x0, f32 y0, f32 x1, f32 y1, f32 x2, f32 y2, f32 x3, f32 y3, u32 c0, u32 c1, u32 c2, u32 c3) {
    Mm_Quad(Tb_X(x0), Tb_Y(y0), Tb_X(x1), Tb_Y(y1), Tb_X(x2), Tb_Y(y2), Tb_X(x3), Tb_Y(y3), c0, c1, c2, c3);
}

static void Tb_Rect(f32 x0, f32 y0, f32 x1, f32 y1, u32 top, u32 bottom) {
    Tb_Quad(x0, y0, x1, y0, x0, y1, x1, y1, top, top, bottom, bottom);
}

/* ---- the letters: lines written with the TrueType fonts of the font list (port/src/gs/tbo_text.c): the headings
   and buttons in the Menu font, the hosts' talk in the Dialogue / UI font (F1 > UI; on "Original" the first font of
   fonts.tsv for each) ---- */
extern int PortTbo_TextId(const char *text, int kind);      /* a line's number for Tb_Sprite, -1: not written */
extern int PortTbo_TextInfo(int id, int what);              /* its measures, in thousandths of its capitals */
extern int PortTbo_TextMeasure(const char *text, int kind); /* how far the pen goes, without keeping it */

static void Tb_Align(s32 align) {
    sTbAlign = align;
}

#define TB_CELL_UNITS 27.5f    /* a line's height at scale 1, in the page's units */
#define TB_CAP_OF_CELL 0.49f   /* its capitals' height, and */
#define TB_BASE_OF_CELL 0.744f /* its baseline, from its top */
#define TB_UNIT_X 0.857f       /* a unit across is 7:6 of a unit down: letters keep their shape */

static f32 Tb_TextWidth(char *str, f32 scale) {
    return (f32)PortTbo_TextMeasure(str, 0) / 1000.0f * TB_CELL_UNITS * scale * TB_CAP_OF_CELL * TB_UNIT_X;
}

/* Where a line of `scale` starts (its y) for its capitals to be centred on `middle`. */
static f32 Tb_TextTop(f32 middle, f32 scale) {
    f32 cell = TB_CELL_UNITS * scale;

    return middle - cell * (TB_BASE_OF_CELL - TB_CAP_OF_CELL * 0.5f);
}

/* Colour of Mm_Rgba (0..255 per channel) as a tint of the white letters (0x80 = as they are). */
static u32 Tb_Tint(u32 c) {
    return ((c & 0xFF) >> 1) | ((((c >> 8) & 0xFF) >> 1) << 8) | ((((c >> 16) & 0xFF) >> 1) << 16);
}

static u32 Tb_Mix(u32 a, u32 b, f32 t) {
    s32 k;
    u32 out = 0;

    for (k = 0; k < 24; k += 8) {
        f32 ca = (f32)((a >> k) & 0xFF);
        f32 cb = (f32)((b >> k) & 0xFF);

        out |= (u32)(ca + (cb - ca) * t) << k;
    }
    return out;
}

/* A line's picture (the line filled above, outlined below) over the page: the pen starting at x, the baseline at
   `base`, capitals `cap` units high, squeezed across by `fit`. bands 0: the outline in `line`; else the fill from
   `top` to `bottom` in that many bands. */
static void Tb_Line(s32 id, f32 x, f32 base, f32 cap, f32 fit, u32 top, u32 bottom, u32 line, s32 bands, s32 alpha) {
    f32 ky = cap / 1000.0f;
    f32 kx = ky * fit * TB_UNIT_X;
    f32 x0 = x - (f32)PortTbo_TextInfo(id, 4) * kx;
    f32 x1 = x0 + (f32)PortTbo_TextInfo(id, 1) * kx;
    f32 y0 = base - (f32)PortTbo_TextInfo(id, 3) * ky;
    f32 h = (f32)PortTbo_TextInfo(id, 2) * ky;
    s32 band;

    if (bands == 0) {
        Tb_Sprite(id, x0, y0, x1, y0 + h, 0.0f, 0.5f, 1.0f, 1.0f, Tb_Tint(line), alpha);
        return;
    }
    for (band = 0; band < bands; band++) {
        f32 t0 = (f32)band / (f32)bands;
        f32 t1 = (f32)(band + 1) / (f32)bands;

        Tb_Sprite(id, x0, y0 + h * t0, x1, y0 + h * t1, 0.0f, t0 * 0.5f, 1.0f, t1 * 0.5f,
                  Tb_Tint(Tb_Mix(top, bottom, ((f32)band + 0.5f) / (f32)bands)), alpha);
    }
}

/* A line of letters: filled from `top` to `bottom` (a gradient), outlined in `line`; squeezed to `width` if it is
   wider (0: no limit). The alpha is the top colour's. */
static void Tb_TextEx(f32 x, f32 y, char *str, f32 scale, f32 width, u32 top, u32 bottom, u32 line) {
    f32 cell = TB_CELL_UNITS * scale;
    f32 cap = cell * TB_CAP_OF_CELL;
    f32 fit = 1.0f;
    f32 w;
    s32 alpha = (s32)((top >> 24) & 0xFF);
    s32 id;

    if (str[0] == 0 || (id = PortTbo_TextId(str, 0)) < 0) {
        return;
    }
    w = (f32)PortTbo_TextInfo(id, 0) / 1000.0f * cap * TB_UNIT_X;
    if (width > 0.0f && w > width) {
        fit = width / w;
        if (fit < 0.6f) {
            fit = 0.6f;
        }
        w *= fit;
    }
    if (sTbAlign == 1) {
        x -= w * 0.5f;
    } else if (sTbAlign == 2) {
        x -= w;
    }
    Tb_Line(id, x, y + cell * TB_BASE_OF_CELL, cap, fit, top, bottom, line, 0, alpha); /* the outline under the fill */
    Tb_Line(id, x, y + cell * TB_BASE_OF_CELL, cap, fit, top, bottom, line, 3, alpha);
}

/* ---- the talk box's letters: capitals 11.5 units high, the baseline 18.25 down (about the size of the main
   menu's own), white with a thin dark edge ---- */
#define TB_MSG_CAP 11.5f
#define TB_MSG_BASE 18.25f

static f32 Tb_MsgWidth(char *str, f32 scale) {
    return (f32)PortTbo_TextMeasure(str, 1) / 1000.0f * TB_MSG_CAP * scale * TB_UNIT_X;
}

static void Tb_Msg(f32 x, f32 y, char *str, f32 scale, s32 alpha) {
    s32 id;

    if (str[0] == 0 || (id = PortTbo_TextId(str, 1)) < 0) {
        return;
    }
    Tb_Line(id, x, y + TB_MSG_BASE * scale, TB_MSG_CAP * scale, 1.0f, 0, 0, Mm_Rgba(16, 20, 40, 255), 0, alpha);
    Tb_Line(id, x, y + TB_MSG_BASE * scale, TB_MSG_CAP * scale, 1.0f, Mm_Rgba(255, 255, 255, 255),
            Mm_Rgba(255, 255, 255, 255), 0, 1, alpha);
}

/* ---- the pad's buttons ---- */
/* kind: 0 cross, 1 square, 2 triangle, 3 left / right, 4 circle, 7 START */
static s32 Tb_ButtonPic(s32 kind) {
    switch (kind) {
    case 1:
        return TBO_BTN_SQUARE;
    case 2:
        return TBO_BTN_TRIANGLE;
    case 3:
        return TBO_BTN_LEFTRIGHT;
    case 4:
        return TBO_BTN_CIRCLE;
    case 7:
        return TBO_BTN_START;
    }
    return TBO_BTN_CROSS;
}

static f32 Tb_ButtonW(s32 kind, f32 size) {
    s32 id = Tb_ButtonPic(kind);

    return size * Tb_PicW(id) / Tb_PicH(id) * 0.857f;
}

static void Tb_Button(s32 kind, f32 cx, f32 cy, f32 size) {
    Tb_Icon(Tb_ButtonPic(kind), cx, cy, size, 0x80);
}

/* The buttons the screen that is up answers to, at the foot of the hosts' box (bottom right, as in the mockups). */
#define TB_PROMPTS 6
static s32 sTbPromptKind[TB_PROMPTS];
static char *sTbPromptLabel[TB_PROMPTS];
static s32 sTbPromptCount;

static void Tb_Prompt(s32 kind, char *label) {
    if (sTbPromptCount < TB_PROMPTS) {
        sTbPromptKind[sTbPromptCount] = kind;
        sTbPromptLabel[sTbPromptCount++] = label;
    }
}

static f32 Tb_PromptX(s32 k) { /* where prompt k starts: laid from the right edge leftwards */
    f32 x = 470.0f;
    s32 i;

    for (i = sTbPromptCount - 1; i >= k; i--) {
        x -= Tb_MsgWidth(sTbPromptLabel[i], 1.0f) + Tb_ButtonW(sTbPromptKind[i], 19.0f) + 16.0f;
    }
    return x;
}

static void Tb_PromptsDraw(void) {
    s32 k;

    for (k = 0; k < sTbPromptCount; k++) {
        f32 bw = Tb_ButtonW(sTbPromptKind[k], 19.0f);
        f32 x = Tb_PromptX(k);

        Tb_Button(sTbPromptKind[k], x + bw * 0.5f, 425.0f, 19.0f);
        Tb_Msg(x + bw + 5.0f, 425.0f - (TB_MSG_BASE - TB_MSG_CAP * 0.5f), sTbPromptLabel[k], 1.0f, 0x80); /* its capitals beside the button's middle */
    }
}

static void Tb_Text(f32 x, f32 y, char *s, f32 scale, f32 width, s32 style) {
    switch (style) {
    case 1: /* gold: under the cursor */
        Tb_TextEx(x, y, s, scale, width, Mm_Rgba(0xFF, 0xEC, 0xB8, 0x80), Mm_Rgba(0xFF, 0x94, 0x30, 0x80), Mm_Rgba(0x5C, 0x24, 0x08, 0x80));
        break;
    case 2: /* yellow: a heading */
        Tb_TextEx(x, y, s, scale, width, Mm_Rgba(0xFF, 0xF4, 0x90, 0x80), Mm_Rgba(0xFF, 0xD0, 0x30, 0x80), Mm_Rgba(0x50, 0x28, 0x08, 0x80));
        break;
    case 3: /* cyan: a value */
        Tb_TextEx(x, y, s, scale, width, Mm_Rgba(0xD0, 0xFC, 0xFF, 0x80), Mm_Rgba(0x70, 0xE0, 0xFF, 0x80), Mm_Rgba(0x08, 0x28, 0x50, 0x80));
        break;
    case 4: /* grey: not open yet */
        Tb_TextEx(x, y, s, scale, width, Mm_Rgba(0xC8, 0xC8, 0xD4, 0x80), Mm_Rgba(0x98, 0x98, 0xAC, 0x80), Mm_Rgba(0x30, 0x30, 0x44, 0x80));
        break;
    case 5: /* green: online */
        Tb_TextEx(x, y, s, scale, width, Mm_Rgba(0xD8, 0xFF, 0xC0, 0x80), Mm_Rgba(0x60, 0xE8, 0x50, 0x80), Mm_Rgba(0x08, 0x40, 0x10, 0x80));
        break;
    default:
        Tb_TextEx(x, y, s, scale, width, Mm_Rgba(0xFF, 0xFF, 0xFF, 0x80), Mm_Rgba(0xE4, 0xD8, 0xFF, 0x80), Mm_Rgba(0x40, 0x14, 0x64, 0x80));
        break;
    }
}

/* A filled circle, as a fan of quads. */
static void Tb_Disc(f32 cx, f32 cy, f32 r, u32 mid, u32 edge) {
    s32 k;
    f32 rx = r * 0.857f; /* (a round thing is this much narrower in the screen's pixels) */

    for (k = 0; k < 32; k++) {
        f32 a0 = (f32)k * 6.2831853f / 32.0f;
        f32 a1 = (f32)(k + 1) * 6.2831853f / 32.0f;

        Tb_Quad(cx, cy, cx, cy, cx + cosf(a0) * rx, cy + sinf(a0) * r, cx + cosf(a1) * rx, cy + sinf(a1) * r, mid, mid, edge, edge);
    }
}

/* The Dragon Radar of the search: its glass, its grid, the sweeping line, a few dragon balls blinking. */
static void Tb_Radar(f32 cx, f32 cy, f32 r) {
    f32 rx = r * 0.857f;
    f32 sweep = (f32)sTb->frames * 0.06f;
    s32 k;

    Tb_Disc(cx, cy, r + 10.0f, Mm_Rgba(0xD8, 0xDC, 0xE4, 0x80), Mm_Rgba(0x70, 0x78, 0x88, 0x80)); /* the casing */
    Tb_Disc(cx, cy, r + 3.0f, Mm_Rgba(0x20, 0x30, 0x28, 0x80), Mm_Rgba(0x10, 0x18, 0x14, 0x80));
    Tb_Disc(cx, cy, r, Mm_Rgba(0x40, 0xC8, 0x60, 0x80), Mm_Rgba(0x10, 0x60, 0x28, 0x80));    /* the glass */
    for (k = -3; k <= 3; k++) {                                                                  /* the grid */
        f32 t = (f32)k / 3.5f;
        f32 half = sqrtf(1.0f - t * t);

        Tb_Rect(cx + t * rx - 0.6f, cy - half * r, cx + t * rx + 0.6f, cy + half * r, Mm_Rgba(0x90, 0xFF, 0xA0, 0x50), Mm_Rgba(0x90, 0xFF, 0xA0, 0x50));
        Tb_Rect(cx - half * rx, cy + t * r - 0.6f, cx + half * rx, cy + t * r + 0.6f, Mm_Rgba(0x90, 0xFF, 0xA0, 0x50), Mm_Rgba(0x90, 0xFF, 0xA0, 0x50));
    }
    for (k = 0; k < 10; k++) { /* the sweep: a wedge that fades behind the line */
        f32 a0 = sweep - (f32)(k + 1) * 0.07f;
        f32 a1 = sweep - (f32)k * 0.07f;
        s32 al = 0x60 - k * 6;

        Tb_Quad(cx, cy, cx, cy, cx + cosf(a0) * rx, cy + sinf(a0) * r, cx + cosf(a1) * rx, cy + sinf(a1) * r, Mm_Rgba(0xD0, 0xFF, 0xC0, al),
                Mm_Rgba(0xD0, 0xFF, 0xC0, al), Mm_Rgba(0xD0, 0xFF, 0xC0, al / 2), Mm_Rgba(0xD0, 0xFF, 0xC0, al / 2));
    }
    {
        static const f32 ball[5][2] = {{0.35f, -0.4f}, {-0.5f, 0.2f}, {0.15f, 0.55f}, {-0.2f, -0.65f}, {0.62f, 0.25f}};

        for (k = 0; k < 5; k++) {
            f32 bx = cx + ball[k][0] * rx;
            f32 by = cy + ball[k][1] * r;
            s32 al = ((sTb->frames + k * 17) / 20) % 3 == 0 ? 0x40 : 0x80;

            Tb_Disc(bx, by, 4.0f, Mm_Rgba(0xFF, 0xF8, 0x80, al), Mm_Rgba(0xFF, 0xA0, 0x20, al));
        }
    }
    Tb_Disc(cx, cy, 3.0f, Mm_Rgba(0xFF, 0x50, 0x40, 0x80), Mm_Rgba(0xA0, 0x10, 0x10, 0x80)); /* you */
}

static f32 Tb_HubRowY(s32 i) {
    return 84.0f + 29.0f * (f32)i;
}

static f32 Tb_HubRowEnd(s32 i) {
    return 270.0f - 7.0f * (f32)i; /* the bars cascade: each a little shorter than the one above */
}

static void Tb_DrawHub(void) {
    s32 i;
    f32 slide = (1.0f - Tb_In(0)) * -160.0f;
    f32 target = Tb_HubRowY(sTb->hubCur);

    Tb_Hosts(TBO_HOSTS_MIC_WAVE, 396.0f - slide * 0.6f, 350.0f, 262.0f);
    sTb->hubY += (target - sTb->hubY) * 0.35f;
    for (i = 0; i < HUB_COUNT; i++) {
        f32 y = Tb_HubRowY(i);
        f32 x1 = Tb_HubRowEnd(i) + slide;

        if (i != sTb->hubCur) {
            Tb_Pill(TBO_KIT_BAR, TB_BAR_LEFT + slide, y, x1, y + 24.0f, sTbHubReady[i] ? 0x80 : 0x60);
        }
    }
    Tb_Pill(TBO_KIT_SEL, TB_BAR_LEFT + slide, sTb->hubY - 3.0f, Tb_HubRowEnd(sTb->hubCur) + 22.0f + slide, sTb->hubY + 27.0f, 0x80);
    Tb_Icon(TBO_CREST, 20.0f + slide + 2.0f * Tb_Beat(40), sTb->hubY + 12.0f, 24.0f, 0x80);
    for (i = 0; i < HUB_COUNT; i++) {
        if (!sTbHubReady[i]) {
            Tb_Pill(TBO_KIT_STEEL, Tb_HubRowEnd(i) - 46.0f + slide + (i == sTb->hubCur ? 18.0f : 0.0f), Tb_HubRowY(i) + 5.0f,
                    Tb_HubRowEnd(i) - 6.0f + slide + (i == sTb->hubCur ? 18.0f : 0.0f), Tb_HubRowY(i) + 19.0f, 0x80);
        }
    }
}

static void Tb_DrawHubText(void) {
    s32 i;
    f32 slide = (1.0f - Tb_In(0)) * -160.0f;

    for (i = 0; i < HUB_COUNT; i++) {
        f32 y = Tb_HubRowY(i);

        /* each label's capitals in the middle of its bar (the bar under the cursor: hubY - 3 to hubY + 27) */
        if (i == sTb->hubCur) {
            Tb_Text(36.0f + slide, Tb_TextTop(sTb->hubY + 12.0f, 0.84f), sTbHubName[i], 0.84f, 0.0f, 1);
        } else {
            Tb_Text(36.0f + slide, Tb_TextTop(y + 12.0f, 0.7f), sTbHubName[i], 0.7f, 0.0f, sTbHubReady[i] ? 0 : 4);
        }
        if (!sTbHubReady[i]) {
            Tb_Align(1);
            Tb_Text(Tb_HubRowEnd(i) - 26.0f + slide + (i == sTb->hubCur ? 18.0f : 0.0f), Tb_TextTop(Tb_HubRowY(i) + 12.0f, 0.42f), "SOON", 0.42f,
                    0.0f, 4);
            Tb_Align(0);
        }
    }
}

/* rows of label / value, as Create Room and Profile have them */
static void Tb_DrawRows(s32 count, s32 cur, f32 y0) {
    s32 i;

    Tb_Panel(TBO_KIT_WINDOW, 14.0f, y0 - 14.0f, 300.0f, y0 + 34.0f * (f32)count + 6.0f, 0x80);
    for (i = 0; i < count; i++) {
        f32 y = y0 + 34.0f * (f32)i;

        if (i == cur) {
            Tb_Pill(TBO_KIT_SEL, 20.0f, y - 2.0f, 294.0f, y + 28.0f, 0x80);
            Tb_Button(3, 278.0f, y + 13.0f, 20.0f);
        } else {
            Tb_Pill(TBO_KIT_BAR, 24.0f, y, 290.0f, y + 26.0f, 0x80);
        }
    }
}

static void Tb_DrawCreate(void) {
    Tb_Hosts(TBO_HOSTS_ARMS_CROSSED, 408.0f, 350.0f, 250.0f);
    Tb_DrawRows(CR_COUNT, sTb->crCur, 100.0f);
}

static void Tb_DrawCreateText(void) {
    static char value[CR_COUNT][32];
    static char *label[CR_COUNT] = {"Battle", "Time Limit", "Input Delay", "Rollback", "Open Room"};
    s32 i;

    sprintf(value[CR_BATTLE], sTb->battle ? "TEAMS (UP TO 5)" : "ONE ON ONE");
    sprintf(value[CR_TIME], "%s", sTbTime[sTb->time]);
    Tb_DelayText(value[CR_DELAY], sTb->delay);
    Tb_RollText(value[CR_ROLL], sTb->roll);
    value[CR_OPEN][0] = 0;
    Tb_Pill(TBO_KIT_GOLD, 70.0f, 70.0f, 244.0f, 90.0f, 0x80);
    Tb_Align(1);
    Tb_Text(157.0f, Tb_TextTop(80.0f, 0.56f), "ROOM SETTINGS", 0.56f, 0.0f, 2);
    Tb_Align(0);
    for (i = 0; i < CR_COUNT; i++) {
        f32 y = 100.0f + 34.0f * (f32)i;

        Tb_Text(36.0f, Tb_TextTop(y + 13.0f, 0.66f), label[i], 0.66f, 120.0f, i == sTb->crCur ? 1 : 0);
        Tb_Align(2);
        Tb_Text(266.0f, Tb_TextTop(y + 13.0f, 0.54f), value[i], 0.54f, 130.0f, 3);
        Tb_Align(0);
    }
}

static void Tb_DrawJoin(void) {
    s32 c;

    Tb_Hosts(TBO_HOSTS_PUZZLED, 432.0f, 350.0f, 200.0f);
    for (c = 0; c < CODE_LEN; c++) { /* the code's six boxes */
        f32 x = 34.0f + 50.0f * (f32)c;

        Tb_Pill(c == sTb->codeLen ? TBO_KIT_SEL : TBO_KIT_BAR, x, 76.0f, x + 44.0f, 116.0f, 0x80);
    }
    Tb_Panel(TBO_KIT_WINDOW, 14.0f, 126.0f, 352.0f, 330.0f, 0x80);
}

static void Tb_DrawJoinText(void) {
    char one[2] = {0, 0};
    s32 c;

    Tb_Align(1);
    for (c = 0; c < CODE_LEN; c++) {
        f32 x = 56.0f + 50.0f * (f32)c;

        one[0] = c < sTb->codeLen ? sTb->code[c] : (c == sTb->codeLen && (sTb->frames / 16) & 1 ? '_' : ' ');
        Tb_Text(x, 81.0f, one, 1.0f, 0.0f, c < sTb->codeLen ? 2 : 0);
    }
    Tb_Align(0);
    Tb_Keypad(24.0f, 138.0f, KEY_ROWS, sTbKeys, sTbJoinBtn, 2);
}

static void Tb_DrawWait(void) {
    if (sTb->found) {
        Tb_Hosts(TBO_HOSTS_CHEER, 426.0f, 350.0f, 180.0f);
    } else {
        Tb_Hosts(TBO_HOSTS_BINOCULARS, 434.0f, 350.0f, 210.0f);
    }
    Tb_Radar(264.0f, 188.0f, 80.0f);
    Tb_Panel(TBO_KIT_WINDOW, 10.0f, 84.0f, 180.0f, 246.0f, 0x80);
    if (!sTb->found) {
        Tb_Pill(TBO_KIT_SEL, 10.0f, 262.0f, 182.0f, 294.0f, 0x80);
        Tb_Button(1, 28.0f, 278.0f, 22.0f);
    }
    Tb_Pill(TBO_KIT_BAR, 176.0f, 296.0f, 348.0f, 326.0f, 0x80);
}

static void Tb_DrawWaitText(void) {
    static char line[96];
    s32 secs = (sTb->frames - sTb->waitFrom) / 60;
    const char *code = Port_LobbyCode();

    if (sTb->host == 2) {
        s32 n = Port_LobbyQueueWaiting(sTb->queue);

        Tb_Text(22.0f, 94.0f, "QUICK MATCH", 0.56f, 0.0f, 2);
        Tb_Align(1);
        Tb_Text(95.0f, 124.0f, sTb->queue ? "TEAMS" : "ONE ON ONE", 0.86f, 156.0f, 1);
        Tb_Align(0);
        Tb_Text(22.0f, 172.0f, "THE SERVICE PAIRS YOU", 0.46f, 150.0f, 0);
        Tb_Text(22.0f, 190.0f, "WITH THE NEXT RIVAL", 0.46f, 150.0f, 0);
        if (n > 0) {
            sprintf(line, "%d IN THIS QUEUE", (int)n);
            Tb_Text(22.0f, 214.0f, line, 0.42f, 150.0f, 5);
        }
    } else if (sTb->host) {
        Tb_Text(22.0f, 94.0f, "ROOM CODE", 0.56f, 0.0f, 2);
        Tb_Align(1);
        if (code[0] != 0) {
            Tb_Text(95.0f, 120.0f, (char *)code, 1.2f, 156.0f, 1);
        } else {
            Tb_Text(95.0f, 130.0f, "GETTING A CODE...", 0.5f, 156.0f, 0);
        }
        Tb_Align(0);
        Tb_Text(22.0f, 172.0f, code[0] != 0 ? "GIVE THIS CODE TO" : "ASKING THE", 0.46f, 150.0f, 0);
        Tb_Text(22.0f, 190.0f, code[0] != 0 ? "YOUR RIVAL" : "MATCHMAKING SERVICE", 0.46f, 150.0f, 0);
        if (Port_LobbyOther()) {
            Tb_Text(22.0f, 214.0f, "ANOTHER VERSION TRIED", 0.42f, 150.0f, 3);
        }
    } else {
        Tb_Text(22.0f, 94.0f, "JOINING ROOM", 0.56f, 0.0f, 2);
        Tb_Align(1);
        Tb_Text(95.0f, 120.0f, sTb->code, 1.2f, 156.0f, 1);
        Tb_Align(0);
        Tb_Text(22.0f, 172.0f, "LOOKING FOR", 0.46f, 150.0f, 0);
        Tb_Text(22.0f, 190.0f, "THE HOST...", 0.46f, 150.0f, 0);
    }
    if (!sTb->found) {
        Tb_Text(42.0f, Tb_TextTop(278.0f, 0.5f), "TRAIN WHILE WAITING", 0.5f, 134.0f, 1);
    }
    Tb_Align(1);
    if (sTb->found) {
        Tb_Text(262.0f, 302.0f, "YOUR RIVAL IS HERE!", 0.56f, 164.0f, 5);
    } else {
        sprintf(line, "SEARCHING...  %d:%02d", (int)(secs / 60), (int)(secs % 60));
        Tb_Text(262.0f, 302.0f, line, 0.52f, 164.0f, 0);
    }
    Tb_Align(0);
}

static void Tb_DrawBlock(void) {
    Tb_Panel(TBO_KIT_WINDOW, 60.0f, 96.0f, 452.0f, 316.0f, 0x80);
    Tb_Hosts(TBO_HOSTS_PUZZLED, 404.0f, 120.0f, 92.0f);
    Tb_Pill(TBO_KIT_GOLD, 206.0f, 282.0f, 306.0f, 306.0f, 0x80);
}

static void Tb_DrawBlockText(void) {
    static char parts[96];
    const char *t = Port_DataFingerprintText();
    const char *m = t;
    s32 n;

    Tb_TextEx(80.0f, 108.0f, "Cannot Go Online", 0.9f, 250.0f, Mm_Rgba(0xFF, 0xF0, 0x50, 0x80), Mm_Rgba(0xF4, 0x40, 0x10, 0x80), Mm_Rgba(0x70, 0x10, 0x08, 0x80));
    Tb_Msg(80.0f, 146.0f, "Online matches are only between", 1.0f, 0x80);
    Tb_Msg(80.0f, 166.0f, "two unmodded games.", 1.0f, 0x80);
    while (*m != 0 && strncmp(m, "modified:", 9) != 0) {
        m++;
    }
    if (*m != 0) { /* which parts differ, as the data check says it */
        sprintf(parts, "Changed: %.80s", m + 10);
    } else if (Port_DataFingerprint(NULL) == 1) {
        sprintf(parts, "Your game data is not the disc's.");
    } else {
        sprintf(parts, "Your game data could not be checked.");
    }
    for (n = 0; parts[n] != 0; n++) {
        if (parts[n] == ',') {
            parts[n] = ' ';
        }
    }
    Tb_Msg(80.0f, 196.0f, parts, 1.0f, 0x80);
    Tb_Msg(80.0f, 226.0f, "Take out what you added in gamedata/mods.", 1.0f, 0x80);
    Tb_Msg(80.0f, 246.0f, "(Fighter mods switch off by themselves online.)", 1.0f, 0x70);
    Tb_Align(1);
    Tb_Text(256.0f, 285.0f, "BACK", 0.56f, 0.0f, 1);
    Tb_Align(0);
}

/* The name's window: over everything else on the screen (drawn last). */
static void Tb_DrawName(void) {
    static char line[40];

    Tb_Panel(TBO_KIT_WINDOW, 64.0f, 62.0f, 448.0f, 340.0f, 0x80);
    Tb_Text(84.0f, 70.0f, "TYPE YOUR NAME", 0.6f, 0.0f, 2);
    Tb_Pill(TBO_KIT_BAR, 84.0f, 94.0f, 428.0f, 124.0f, 0x80);
    sprintf(line, "%s%s", sTb->typed, (sTb->frames / 16) & 1 ? "_" : " ");
    Tb_Text(100.0f, 97.0f, line, 0.86f, 310.0f, 1);
    Tb_Keypad(94.0f, 134.0f, NAME_ROWS, sTbNameKeys[sTb->lower], sTbNameBtn, 4);
}

static void Tb_DrawSpar(void) {
    Tb_Hosts(TBO_HOSTS_CHEER, 406.0f, 350.0f, 200.0f);
    Tb_DrawRows(3, sTb->spCur, 110.0f);
    Tb_Panel(TBO_KIT_WINDOW, 14.0f, 230.0f, 300.0f, 326.0f, 0x80);
}

static void Tb_DrawSparText(void) {
    static char line[64];
    s32 secs = (sTb->frames - sTb->waitFrom) / 60;

    Tb_Text(26.0f, 76.0f, "SPARRING AT THE TOURNAMENT", 0.6f, 270.0f, 2);
    Tb_Text(36.0f, 115.0f, "You", 0.66f, 70.0f, sTb->spCur == 0 ? 1 : 0);
    Tb_Text(36.0f, 149.0f, "Partner", 0.66f, 80.0f, sTb->spCur == 1 ? 1 : 0);
    Tb_Text(36.0f, 183.0f, "Start Training", 0.66f, 200.0f, sTb->spCur == 2 ? 1 : 0);
    Tb_Align(2);
    Tb_Text(266.0f, 117.0f, Tb_Fighter(sTb->spMe), 0.54f, 170.0f, 3);
    Tb_Text(266.0f, 151.0f, Tb_Fighter(sTb->spFoe), 0.54f, 170.0f, 3);
    Tb_Align(0);
    Tb_Text(28.0f, 240.0f, sTb->host == 2 ? "QUICK MATCH SEARCH" : sTb->host ? "YOUR ROOM IS OPEN" : "JOINING A ROOM", 0.5f, 260.0f, 2);
    sprintf(line, "SEARCHING  %d:%02d", (int)(secs / 60), (int)(secs % 60));
    Tb_Text(28.0f, 264.0f, line, 0.52f, 260.0f, 5);
    Tb_Text(28.0f, 292.0f, "THE MATCH STARTS THE MOMENT", 0.42f, 260.0f, 0);
    Tb_Text(28.0f, 308.0f, "YOUR RIVAL ARRIVES", 0.42f, 260.0f, 0);
}

static void Tb_DrawQuick(void) {
    Tb_Hosts(TBO_HOSTS_MIC_WAVE, 404.0f, 350.0f, 240.0f);
    Tb_DrawRows(2, sTb->qkCur, 110.0f);
    Tb_Panel(TBO_KIT_WINDOW, 14.0f, 196.0f, 300.0f, 330.0f, 0x80);
}

static void Tb_DrawQuickText(void) {
    static char line[64];
    s32 q;

    Tb_Text(26.0f, 76.0f, "PICK YOUR BATTLE", 0.62f, 0.0f, 2);
    Tb_Text(36.0f, 115.0f, "Battle", 0.66f, 110.0f, sTb->qkCur == 0 ? 1 : 0);
    Tb_Align(2);
    Tb_Text(266.0f, 117.0f, sTb->queue ? "TEAMS (UP TO 5)" : "ONE ON ONE", 0.54f, 140.0f, 3);
    Tb_Align(0);
    Tb_Text(36.0f, 149.0f, "Search", 0.66f, 110.0f, sTb->qkCur == 1 ? 1 : 0);
    Tb_Text(28.0f, 206.0f, "WAITING RIGHT NOW", 0.52f, 0.0f, 2);
    for (q = 0; q < 2; q++) {
        s32 n = Port_LobbyQueueWaiting(q);

        Tb_Text(36.0f, 234.0f + 26.0f * (f32)q, q ? "Teams" : "One on One", 0.56f, 120.0f, q == sTb->queue ? 1 : 0);
        if (n < 0) {
            sprintf(line, "...");
        } else {
            sprintf(line, "%d WAITING", (int)n);
        }
        Tb_Align(2);
        Tb_Text(286.0f, 236.0f + 26.0f * (f32)q, line, 0.5f, 120.0f, n > 0 ? 5 : 3);
        Tb_Align(0);
    }
    sprintf(line, "NO TIME LIMIT - %d%% SPEED", (int)Port_NetSpeedStandard());
    Tb_Text(28.0f, 296.0f, line, 0.46f, 260.0f, 3);
}

static void Tb_DrawProfile(void) {
    Tb_Hosts(TBO_HOSTS_THUMBS_UP, 408.0f, 350.0f, 250.0f);
    Tb_DrawRows(PR_COUNT, sTb->prCur, 110.0f);
}

static void Tb_DrawProfileText(void) {
    static char value[PR_COUNT][32];
    static char *label[PR_COUNT] = {"Name", "Input Delay", "Rollback"};
    s32 i;

    Tb_Copy(value[PR_NAME], Port_NetNameGet(), 32);
    Tb_DelayText(value[PR_DELAY], sTb->delay);
    Tb_RollText(value[PR_ROLL], sTb->roll);
    Tb_Text(26.0f, 76.0f, "PLAYER CARD", 0.62f, 0.0f, 2);
    for (i = 0; i < PR_COUNT; i++) {
        f32 y = 110.0f + 34.0f * (f32)i;

        Tb_Text(36.0f, Tb_TextTop(y + 13.0f, 0.66f), label[i], 0.66f, 110.0f, i == sTb->prCur ? 1 : 0);
        Tb_Align(2);
        Tb_Text(266.0f, Tb_TextTop(y + 13.0f, 0.54f), value[i], 0.54f, 140.0f, 3);
        Tb_Align(0);
    }
}

/* What the hosts say in the box at the foot of the screen (two lines at most). */
static char *Tb_Help(void) {
    switch (sTb->mode) {
    case TB_HUB:
        return sTbHubSay[sTb->hubCur];
    case TB_CREATE:
        return sTb->crCur == CR_DELAY ? "How late your moves come out. Automatic measures the line when the match starts."
               : sTb->crCur == CR_ROLL ? "Your moves come out at once and the game corrects itself. More frames cope with a slower line."
               : "Set the rules of your tournament! Then your rival joins with the room code.";
    case TB_JOIN:
        return "Enter your rival's room code! Six letters and numbers, then Join.";
    case TB_WAIT:
        return sTb->found ? "Here we go! The match is starting!" : "Looking for a worthy challenger... Warm up in Training meanwhile!";
    case TB_BLOCK:
        return "Sorry! Online fights have to be fair, so only the game straight off the disc can play.";
    case TB_SPAR:
        return "Warm up at the tournament while we search! Pick yourself and a sparring partner.";
    case TB_QUICK:
        return "Pick a battle and we'll find you a rival! No time limit, and the game runs a little faster.";
    case TB_NAME:
        return "Pick your letters, then Done! Square deletes, Start finishes.";
    case TB_PROFILE:
        return "Show everyone who you are! The host's input delay is the one the match uses.";
    }
    return "";
}

static void Tb_Draw(void) {
    switch (sTb->mode) {
    case TB_HUB:
        Tb_DrawHub();
        break;
    case TB_CREATE:
        Tb_DrawCreate();
        break;
    case TB_JOIN:
        Tb_DrawJoin();
        break;
    case TB_WAIT:
        Tb_DrawWait();
        break;
    case TB_QUICK:
        Tb_DrawQuick();
        break;
    case TB_SPAR:
        Tb_DrawSpar();
        break;
    case TB_BLOCK:
        break; /* (drawn last, over the rest) */
    default:
        Tb_DrawProfile();
        break;
    }
    Tb_Panel(TBO_KIT_WINDOW, 300.0f, 10.0f, 504.0f, 56.0f, 0x80); /* the player card */
    Tb_Panel(TBO_KIT_DIALOG, 6.0f, 350.0f, 506.0f, 444.0f, 0x80);  /* the hosts' line, and the buttons under it */
    if (sTb->mode == TB_BLOCK) {
        Tb_DrawBlock();
    }
}

static void Tb_DrawText(void) {
    static char *title[9] = {"Tenkaichi Budokai Online", "Create Room", "Join Room", "Searching", "Profile", "Profile", "Quick Match", "Train While Waiting",
                             "Tenkaichi Budokai Online"};
    static char lines[3][80];
    char *say = Tb_Help();
    s32 len = Tb_Len(say);
    s32 k;

    Tb_TextEx(Tb_TitleLeft() + 44.0f, 15.0f, title[sTb->mode], sTb->mode == TB_HUB ? 0.92f : 1.1f, 244.0f, Mm_Rgba(0xFF, 0xF0, 0x50, 0x80),
              Mm_Rgba(0xF4, 0x40, 0x10, 0x80), Mm_Rgba(0x70, 0x10, 0x08, 0x80));
    switch (sTb->mode) {
    case TB_HUB:
        Tb_DrawHubText();
        break;
    case TB_CREATE:
        Tb_DrawCreateText();
        break;
    case TB_JOIN:
        Tb_DrawJoinText();
        break;
    case TB_WAIT:
        Tb_DrawWaitText();
        break;
    case TB_QUICK:
        Tb_DrawQuickText();
        break;
    case TB_SPAR:
        Tb_DrawSparText();
        break;
    case TB_BLOCK:
        break;
    default:
        Tb_DrawProfileText();
        break;
    }
    Tb_Text(322.0f, 15.0f, (char *)Port_NetNameGet(), 0.6f, 110.0f, 1);
    Tb_Align(2);
    Tb_Text(484.0f, 17.0f, sTb->mode == TB_WAIT || sTb->mode == TB_SPAR ? "SEARCHING" : "ONLINE", 0.48f, 0.0f, 5);
    Tb_Align(0);
    Tb_Text(322.0f, 34.0f, "TENKAICHI BUDOKAI ONLINE", 0.38f, 160.0f, 3);
    /* the hosts' line in the talk font, cut at spaces into lines of what fits (two at most) */
    {
        f32 room = 424.0f;
        s32 n = 0;
        s32 at = 0;

        while (at < len && n < 2) {
            s32 end = at;
            s32 brk = -1;
            f32 w = 0.0f;

            /* the most whole words that fit (a first word longer than the line is taken whole) */
            for (;;) {
                s32 next = end;
                char part[80];

                while (next < len && say[next] == ' ') {
                    next++;
                }
                while (next < len && say[next] != ' ') {
                    next++;
                }
                k = next - at < 79 ? next - at : 79;
                memcpy(part, say + at, k);
                part[k] = 0;
                w = Tb_MsgWidth(part, 1.0f);
                if (brk >= 0 && w > room) {
                    break;
                }
                brk = next;
                end = next;
                if (next >= len || k == 79) {
                    break;
                }
            }
            k = end - at < 79 ? end - at : 79;
            memcpy(lines[n], say + at, k);
            lines[n][k] = 0;
            n++;
            at = end;
            while (at < len && say[at] == ' ') {
                at++;
            }
        }
        for (k = 0; k < n; k++) {
            Tb_Msg(40.0f, (n == 1 ? 368.0f : 359.0f) + 21.0f * (f32)k, lines[k], 1.0f, 0x80);
        }
    }
    if (sTb->mode == TB_BLOCK) {
        Tb_DrawBlockText();
    }
    if (sTb->mode == TB_NAME) {
        Tb_DrawName();
    }
    if (sTb->note > 0) {
        s32 a = sTb->note > 16 ? 0x80 : sTb->note * 8;

        Tb_Align(1);
        Tb_TextEx(256.0f, 330.0f, sTb->noteLine, 0.52f, 480.0f, Mm_Rgba(0xFF, 0xF8, 0xB0, a), Mm_Rgba(0xFF, 0xC8, 0x30, a), Mm_Rgba(0x5C, 0x24, 0x08, a));
        Tb_Align(0);
    }
}

/* ----------------------------------------------------------------------------------------------- the frame loop */
/* From the main menu's item until triangle on the hub. Returns 1 when it was left for "Train While Waiting" (the
   room is still open; Duel_Main goes on to the Ultimate Training menu), 0 back to the main menu. */
s32 PortTbo_Run(void) {
    s32 train;

    sTb = Heap_Alloc(sizeof(TboScreen), 0x20, 0, 2);
    memset(sTb, 0, sizeof(TboScreen));
    sTb->hubCur = HUB_CREATE;
    sTb->hubY = Tb_HubRowY(HUB_CREATE);
    Tb_LoadChoices();
    Port_NetNameGet();
    gPortTboSpar = 0;
    if (Port_LobbyInBackground()) { /* back with a room open (from the main menu after training): its search again */
        Port_LobbyBackground(0);
        sTb->host = sTbBgKind;
        sTb->queue = sTbBgQueue;
        Tb_Copy(sTb->code, sTbBgCode, CODE_LEN + 1);
        sTb->waitBack = TB_HUB;
        Tb_SetMode(TB_WAIT);
    }
    Port_EscQuit(0);
    ColorFade_StartIn(0, 0, 0, 0x14);
    while (1) {
        s32 live;

        Job_Run();
        Gfx_BeginFrame();
        Pad_Update();
        if (Port_EscTake() && !sTb->typing) {
            gPad[0].gamePressed |= PADG_TRIANGLE;
        }
        Snd_Update();
        ColorFade_Update();
        live = !sTb->leaving && ColorFade_IsInDone();
        sTbPromptCount = 0;
        switch (sTb->mode) {
        case TB_HUB:
            Tb_HubUpdate(live);
            break;
        case TB_CREATE:
            Tb_CreateUpdate(live);
            break;
        case TB_JOIN:
            Tb_JoinUpdate(live);
            break;
        case TB_WAIT:
            Tb_WaitUpdate(live);
            break;
        case TB_PROFILE:
            Tb_ProfileUpdate(live);
            break;
        case TB_NAME:
            Tb_NameUpdate(live);
            break;
        case TB_QUICK:
            Tb_QuickUpdate(live);
            break;
        case TB_SPAR:
            Tb_SparUpdate(live);
            break;
        case TB_BLOCK:
            Tb_BlockUpdate(live);
            break;
        }
        Tb_PageSet();
        Tb_Backdrop();
        Tb_Icon(TBO_CREST, Tb_TitleLeft() + 18.0f, 32.0f, 42.0f, 0x80); /* the tournament's crest beside the title */
        Tb_Draw();
        Tb_DrawText();
        Tb_PromptsDraw();
        ColorFade_Draw();
        File_Stub264D90();
        Gfx_EndFrame(1);
        Dma_Flush();
        sTb->frames++;
        sTb->modeT++;
        if (sTb->note > 0) {
            sTb->note--;
        }
        if (sTb->leaving && ColorFade_IsOutDone()) {
            break;
        }
    }
    sceGsSyncPath(0, 0);
    Port_EscQuit(1);
    Port_TextMode(0);
    if (!sTb->train && (sTb->mode == TB_WAIT || sTb->mode == TB_SPAR)) {
        Port_LobbyCancel();
    }
    train = sTb->train;
    if (train) {
        static u8 noItems[0x10]; /* an empty set of Z items */
        s32 me = sTb->spMe;
        s32 foe = sTb->spFoe;

        Heap_Free(sTb);
        sTb = NULL;
        Dma_ResetBuffers();
        Battle_ClearWork();
        BattleSetup_SetRule(0, 6, 8, 0, 1, TB_TOURNAMENT_STAGE, 0); /* training: no time limit, a partner who stands */
        BattleSetup_SetSide(0, 0, 0, 1, 1, 1, 0, NULL);
        BattleSetup_SetSide(1, 2, 1, 1, 1, 1, 0, NULL);
        BattleSetup_SetMember(0, 0, me, 0, 0, -1, 100.0f, noItems);
        BattleSetup_SetMember(1, 0, foe, 0, 0, -1, 100.0f, noItems);
        BattleSetup_Finish();
        gPortTboSpar = 1;
        return 1;
    }
    Heap_Free(sTb);
    sTb = NULL;
    Dma_ResetBuffers();
    return train;
}
/* The sign over the sparring fight (battle/hud_a.c, Hud_Draw, every frame): how long it has searched, what for, and
   how to go back. Nothing when the fight is not Train While Waiting's or the search is over. */
static s32 sTbSignFrames;

void PortTbo_SparSign(void) {
    static char line[64];
    s32 secs;

    if (!gPortTboSpar || !Port_LobbyInBackground()) {
        return;
    }
    sTbSignFrames++;
    secs = (sTbBgFrames + sTbSignFrames) / 60;
    Tb_PageSet();
    Tb_Panel(TBO_KIT_DIALOG, 128.0f, 66.0f, 384.0f, 118.0f, 0x70);
    Tb_Icon(TBO_CREST, 150.0f, 92.0f, 30.0f, 0x80);
    sprintf(line, "Searching for a rival...  %d:%02d", (int)(secs / 60), (int)(secs % 60));
    Tb_Msg(170.0f, 72.0f, line, 1.0f, 0x80);
    if (sTbBgKind == 2) {
        sprintf(line, "Quick Match: %s", sTbBgQueue ? "Teams" : "One on One");
    } else if (sTbBgKind == 1) {
        sprintf(line, "Your room: %s", Port_LobbyCode());
    } else {
        sprintf(line, "Joining room %s", sTbBgCode);
    }
    Tb_Msg(170.0f, 93.0f, line, 1.0f, 0x80);
}
#endif
