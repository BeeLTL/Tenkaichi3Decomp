/*
 * Accessibility: what the screen reader says about the game's screens. Called once per blank from Port_VBlank
 * (plat_stub.c) on the game's thread; it reads the game's state and passes what changed to the speaker
 * (gs/a11y.c), which is off unless its library is there.
 *
 * It only reads: nothing here writes the game's memory or draws a random number, so the game runs the same with it
 * on or off and an online session stays in step. Its own variables are not game state (port/tools/make_state.py
 * lists the port files that are). Blanks being run again after a rollback say nothing.
 *
 * The game's structures are declared here only as far as they are read, at their offsets (the whole ones are in
 * include/; the port's files do not include the game's headers).
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "plat_songs.h"
#include "plat_stages.h"

#if defined(__x86_64__) && defined(__clang__)
#undef __ptr32 /* the Windows headers of mingw define these two away */
#undef __uptr
#define GAME_PTR *__ptr32 __uptr
#else
#define GAME_PTR *
#endif

#define A11Y_INTERRUPT 1 /* gs/a11y.c */
#define A11Y_TEXT 1024
extern int PortA11y_Begin(void), PortA11y_Spanish(void);
extern void PortA11y_Say(const char *text, int flags);
extern int PortA11y_Utf8(char *out, int cap, const uint16_t *str);
extern int gPortResim; /* gs/state.c: blanks being run again */

/* include/menu/menu_a.h: MenuProgress; the modes are in docs/systems/menu_overlay.md */
extern struct { int unk0[2]; int loadPack, loadRes, loadSprites; int flags; int mode; } GAME_PTR gProgress;
#define MODE_TITLE 1
#define MODE_MAIN_MENU 4

/* include/menu/menu_a.h: Title (src/menu/menu_a_c.c) */
typedef struct {
    char unk0[0xA8];
    int flags;     /* 0xA8: TITLE_ */
    int cursor;    /* 0xAC: 0 "New Game", 1 "Continue" */
    int timer;     /* 0xB0 */
    int itemCount; /* 0xB4: 2 with a saved game */
    int state;     /* 0xB8: 0 intro, 1 "press start", 2 the two choices */
    int mcState;   /* 0xBC: not 0 while a memory card flow runs (its dialog speaks) */
} A11yTitle;
_Static_assert(offsetof(A11yTitle, mcState) == 0xBC, "Title");
extern A11yTitle GAME_PTR gTitle;
#define TITLE_CHOSEN 1
#define TITLE_LEAVING 2

/* include/sys/dialog.h: Dialog, the memory card window (src/sys/dialog.c) */
typedef struct {
    uint32_t GAME_PTR titleTbl; /* 0x00: the window's own text file (layout 1: the title and the body) */
    uint32_t GAME_PTR msgTbl;   /* 0x04: the caller's text file (layout 0) */
    char unk08[8];
    int windowFlags;            /* 0x10: flash[0].flags, bit 1: fully open, the choices can be used */
    char unk14[0x64 - 0x14];
    int flags;                  /* 0x64: DIALOG_FLAG_ */
    int cursor;                 /* 0x68: 0 "Yes", 1 "No" */
    int msgIdx;                 /* 0x6C */
    int defCursor;              /* 0x70 */
    int size;                   /* 0x74 */
    int layout;                 /* 0x78 */
    int titleIdx;               /* 0x7C */
    int unk80;
    int choices;                /* 0x84: not 0: "Yes" and "No" are shown */
} A11yDialog;
_Static_assert(offsetof(A11yDialog, choices) == 0x84, "Dialog");
extern A11yDialog GAME_PTR gDialog;
#define DIALOG_FLAG_OPEN 4

/* include/menu/menu_a.h: MainMenu (src/menu/menu_a.c). Its "subtitles" (0x0C) are the guides' mouth data, not
   text: the line about the item is in the message window (msg_window). */
typedef struct {
    char unk0[0x108];
    int flags;                   /* 0x108: MAINMENU_ */
    int cursor;                  /* 0x10C: row 0..3 */
    int timer;                   /* 0x110 */
    int unk114;
    int items[11];               /* 0x118: item ids in list order */
    int itemCount;               /* 0x144 */
    int top;                     /* 0x148: list index of the plate above the first row (a ring) */
} A11yMainMenu;
_Static_assert(offsetof(A11yMainMenu, top) == 0x148, "MainMenu");
extern A11yMainMenu GAME_PTR gMainMenu;
#define MAINMENU_CHOSEN 1
#define MAINMENU_LEAVING 2

/* include/battle/view_a.h: MsgWin (src/battle/view_a_b.c), the window that shows one line of text: the main
   menu's line about the item, the duel settings' help. */
typedef struct {
    void GAME_PTR text;      /* 0x00: NULL: drawn without text */
    char unk04[0x98 - 0x04];
    uint16_t GAME_PTR str;   /* 0x98: box (0x44) .draw.str (+0x54), the line shown (TextBox_AttachLine) */
} A11yMsgWin;
_Static_assert(offsetof(A11yMsgWin, str) == 0x98, "MsgWin");
extern A11yMsgWin GAME_PTR gMsgWin;

/* include/menu/menu_d.h: CharSel (src/menu/menu_c_e.c), the character, stage and music select. */
typedef struct {
    int col;          /* 0x00: cursor in the character grid (7 columns) */
    int row;          /* 0x04 */
    int form;         /* 0x08: chip of the form reel */
    int customCol;    /* 0x0C: cursor in the saved custom characters (7 x 2) */
    int customRow;    /* 0x10 */
    int customPlate;  /* 0x14: item set, 0 none */
    int colorPlate;   /* 0x18: costume */
    char unk1C[0x68 - 0x1C];
    int flags;        /* 0x68: CHARSEL_SIDE_ */
    int chara;        /* 0x6C: character under the cursor, line of the name files */
    int state;        /* 0x70: CHARSEL_ST_ */
    char unk74[0xF0 - 0x74];
} A11yCharSide;
_Static_assert(offsetof(A11yCharSide, chara) == 0x6C && offsetof(A11yCharSide, state) == 0x70, "CharSelSide");
_Static_assert(sizeof(A11yCharSide) == 0xF0, "CharSelSide");
#define CHARSEL_SIDE_CUSTOM 0x80
#define CHARSEL_ST_GRID 0
#define CHARSEL_ST_FORM 1
#define CHARSEL_ST_ITEMSET 2
#define CHARSEL_ST_ITEMS 3
#define CHARSEL_ST_ITEMINFO 4
#define CHARSEL_ST_COLOR 5
#define CHARSEL_ST_CUSTOM 6
#define CHARSEL_ST_DONE 7
#define CHARSEL_ST_STAGE 8
#define CHARSEL_ST_BGM 9

typedef struct {
    int col;          /* 0x00: cursor in the stage grid; CHARSEL_STAGE_BGM_COL is the music entry */
    int row;          /* 0x04 */
    char unk08[0x3C - 0x08];
    int stage;        /* 0x3C: stage id under the cursor (kept on the music entry) */
    int state;        /* 0x40: CHARSEL_ST_STAGE or CHARSEL_ST_BGM */
    char unk44[0x4C - 0x44];
    int bgm;          /* 0x4C: index in the music list */
    int bgmPlaying;   /* 0x50: index of the entry that plays */
} A11yCharStage;
_Static_assert(offsetof(A11yCharStage, state) == 0x40 && offsetof(A11yCharStage, bgmPlaying) == 0x50, "CharSelStage");
#define CHARSEL_STAGE_BGM_COL 6

typedef struct {
    int id;
    int formCount;    /* 0x04: forms on the reel, 0 = the cell is the one character */
    int form[7];
} A11yChrCell;
_Static_assert(sizeof(A11yChrCell) == 0x24, "MChrCell");
#define MCHR_COLS 7
#define MCHR_RANDOM 0xA1 /* to MCHR_EMPTY 0xA4: random, locked, custom characters, empty */

typedef struct {
    char unk0[0x28];
    uint32_t GAME_PTR nameText;     /* 0x28: section 43, line `chara`: the name */
    uint32_t GAME_PTR formText;     /* 0x2C: section 44, line `chara`: the form */
    char unk30[0x2FC - 0x30];
    int flags;                      /* 0x2FC: CHARSEL_ */
    char unk300[0x4F8 - 0x300];
    A11yCharSide GAME_PTR side[2];  /* 0x4F8 */
    char unk500[0x554 - 0x500];
    A11yCharStage GAME_PTR stage;   /* 0x554 */
    char unk558[0x560 - 0x558];
    A11yChrCell GAME_PTR cells[2];  /* 0x560: each side's grid */
    int cellCount[2];               /* 0x568 */
    char unk570[0x33D8 - 0x570];
    int customCount[2];             /* 0x33D8: saved custom characters */
    char unk33E0[0x37D0 - 0x33E0];
    int rows[2];                    /* 0x37D0: rows of each side's grid */
    int customRows[2];              /* 0x37D8: item sets offered */
    int colorCount[2];              /* 0x37E0: costumes */
    int stageRows;                  /* 0x37E8 */
    int players;                    /* 0x37EC: 1 = a pad a side; else pad 0 chooses side `turn` */
    char unk37F0[0x3800 - 0x37F0];
    int turn;                       /* 0x3800 */
    int unk3804;
    int GAME_PTR bgmIds;            /* 0x3808: the music list (values: kSongNames, then the markers below) */
    int stageCount;                 /* 0x380C */
    int bgmCount;                   /* 0x3810 */
} A11yCharSel;
_Static_assert(offsetof(A11yCharSel, flags) == 0x2FC && offsetof(A11yCharSel, side) == 0x4F8, "CharSel");
_Static_assert(offsetof(A11yCharSel, stage) == 0x554 && offsetof(A11yCharSel, cells) == 0x560, "CharSel");
_Static_assert(offsetof(A11yCharSel, customCount) == 0x33D8 && offsetof(A11yCharSel, rows) == 0x37D0, "CharSel");
_Static_assert(offsetof(A11yCharSel, players) == 0x37EC && offsetof(A11yCharSel, turn) == 0x3800, "CharSel");
_Static_assert(offsetof(A11yCharSel, bgmIds) == 0x3808 && offsetof(A11yCharSel, bgmCount) == 0x3810, "CharSel");
extern A11yCharSel GAME_PTR gCharSel;
#define CHARSEL_STARTED 2
#define CHARSEL_STAGE_PHASE 4
#define CHARSEL_LEAVING 8
#define CHARSEL_BGM_RANDOM 0x18
#define CHARSEL_STAGE_RANDOM 0x23

/* include/menu/menu_g.h: DuelMenu (src/menu/menu_g_c.c). The line about the item is in the message window. */
typedef struct {
    char unk0[0x10C];
    int flags;      /* 0x10C: DUELMENU_ */
    int sel[5];     /* 0x110: the cursor of each level */
    int timer;      /* 0x124 */
    int level;      /* 0x128: DUEL_LEVEL_ */
    char unk12C[0x138 - 0x12C];
    int column;     /* 0x138: which of the two values of setting 3 the cursor is on */
    int value[2];   /* 0x13C */
    int valueCount; /* 0x144 */
    int startState; /* 0x148: not 0 once a battle type is chosen */
    char unk14C[0x184 - 0x14C];
    int resetState; /* 0x184: not 0 while the "restore the default settings?" question runs (a Dialog) */
} A11yDuelMenu;
_Static_assert(offsetof(A11yDuelMenu, level) == 0x128 && offsetof(A11yDuelMenu, valueCount) == 0x144, "DuelMenu");
_Static_assert(offsetof(A11yDuelMenu, resetState) == 0x184, "DuelMenu");
extern A11yDuelMenu GAME_PTR gDuelMenu;
#define DUELMENU_CHOSEN 1
#define DUELMENU_LEAVING 2
#define DUELMENU_STARTED 4
#define DUEL_LEVEL_TOP 0
#define DUEL_LEVEL_TYPE 1
#define DUEL_LEVEL_DP 2
#define DUEL_LEVEL_SETTING 3
#define DUEL_LEVEL_VALUE 4
#define DUEL_SETTINGS 6 /* five settings and "restore the defaults" */

/* include/sys/save.h: SaveData. rule[] holds the duel settings: time, CPU level, referee, CPU transformation for
   each side (two words) and stage destruction; each is the row of its value list. */
extern struct { char unk0[0xC34]; int rule[6]; } GAME_PTR gSaveData;

/* Labels the game draws as pictures (the plates of pzs3us1/00449.bin and the title's), English then Spanish. */
static const char *const kMainMenuItems[2][11] = { /* by item id: src/menu/menu_a.c, MainMenu_Init */
    {"Dragon History", "Ultimate Battle", "Dragon World Tour", "Duel", "Dragon Net Battle", "Evolution Z",
     "Ultimate Training", "Data Center", "Character Reference", "Options", "Shenron"},
    {"Historia Dragón", "Batalla definitiva", "Torneo mundial Dragón", "Duelo", "Batalla Dragón en línea",
     "Evolución Z", "Entrenamiento definitivo", "Centro de datos", "Referencia de personajes", "Opciones",
     "Shenron"},
};
static const char *const kTitleItems[2][2] = {{"New Game", "Continue"}, {"Partida nueva", "Continuar"}};
static const char *const kYesNo[2][2] = {{"Yes", "No"}, {"Sí", "No"}};
/* By stage id; the English as in gs/namefont.c (kStageNames, what the name pictures say). */
static const char *const kStageNames[2][CHARSEL_STAGE_RANDOM + 1] = {
    {"Wasteland - Noon", "Rocky Area - Noon", "Planet Namek", "Dying Namek", "World Tournament Stage - Noon",
     "Kami's Lookout", "Cell Games Arena - Evening", "Supreme Kai's World", "Hyperbolic Time Chamber",
     "City Ruins - Noon", "Mountain Road - Noon", "Islands", "Kame House", "Planet - Night", "Glacier",
     "Ruined Earth", "Outer Space", "Penguin Village", "Hell", "Desert - Noon", "King's Castle", "Muscle Tower",
     "Mount Paozu", "Wasteland - Evening", "Wasteland - Night", "Rocky Area - Evening", "Rocky Area - Night",
     "World Tournament Stage - Evening", "Cell Game - Noon", "City Ruins - Evening", "City Ruins - Night",
     "Desert - Evening", "Desert - Night", "Mountain Road - Evening", "Planet - Evening", "Random"},
    /* As the Spanish mod's name pictures say them, with their abbreviations written out. */
    {"Páramo, mediodía", "Área rocosa, mediodía", "Planeta Namek", "Namek agonizante",
     "Escenario Torneo Mundial, mediodía", "Atalaya de Kami", "Pista Juegos Célula, tarde", "Mundo Kaiyoshin",
     "Habitación del tiempo", "Ciudad en ruinas, mediodía", "Carretera montañosa, mediodía", "Islas",
     "Kame House", "Planeta, noche", "Glaciar", "Tierra en ruinas", "Espacio exterior", "Aldea Pingüino",
     "Infierno", "Desierto, mediodía", "Castillo del rey", "Torre Músculo", "Monte Paozu", "Yermos, tarde",
     "Yermos, noche", "Área rocosa, tarde", "Área rocosa, noche", "Escenario Torneo Mundial, tarde",
     "Juegos Célula, mediodía", "Ciudad en ruinas, tarde", "Ciudad en ruinas, noche", "Desierto, tarde",
     "Desierto, noche", "Carretera montañosa, tarde", "Planeta, tarde", "Aleatorio"},
};
/* By music-list value 0x00..0x13 (gs/namefont.c, kSongNames); titles, the same in both languages. */
static const char *const kSongNames[20] = {
    "The Meteor", "Vital Burner", "Innocent World", "After The Fire", "Sweet Vibration", "Survive", "Heat Capacity",
    "Overture", "Shine", "Power Scale", "Edge of Spirit", "Caution !", "Menace", "Hot Soul", "High and Scream",
    "Shootout In Meteor", "Dynamite Battle", "Burnin' Up", "Wild Rush", "Evolution",
};
/* The grid's marker cells, when the name file has no line for them. */
static const char *const kCellMarks[2][4] = {
    {"Random", "Locked", "Custom characters", "Empty"},
    {"Aleatorio", "Bloqueado", "Personajes personalizados", "Vacío"},
};
/* The duel menu (src/menu/menu_g_c.c, DuelMenu_ClipGoto): its plates by level and row, then the settings' values
   by row of their lists (DuelMenu_SetupValue). */
static const char *const kDuelTop[2][4] = {
    {"1P vs CPU", "1P vs 2P", "CPU vs CPU", "Battle Settings"},
    {"Jugador 1 contra CPU", "Jugador 1 contra jugador 2", "CPU contra CPU", "Ajustes de batalla"},
};
static const char *const kDuelType[2][3] = {
    {"Single Battle", "Team Battle", "DP Battle"},
    {"Combate individual", "Combate por equipos", "Combate PD"},
};
static const char *const kDuelDp[2][3] = {{"10 DP", "15 DP", "20 DP"}, {"10 PD", "15 PD", "20 PD"}};
static const char *const kDuelSettings[2][DUEL_SETTINGS] = {
    {"Duel Time", "CPU Level", "Referee", "Transformation", "Stage Destruction", "Restore Defaults"},
    {"Tiempo de duelo", "Dificultad de la CPU", "Árbitro", "Transformación", "Ajuste del mapa", "Predeterminado"},
};
static const char *const kDuelTime[2][5] = {
    {"60", "90", "180", "240", "Infinite"},
    {"60", "90", "180", "240", "Infinito"},
};
static const char *const kDuelCpu[2][5] = {
    {"Very Weak", "Weak", "Normal", "Strong", "Very Strong"},
    {"Muy débil", "Débil", "Normal", "Fuerte", "Muy fuerte"},
};
static const char *const kDuelReferee[2][7] = {
    {"Ox-King", "Videl", "Supreme Kai", "Shenron", "Announcer 1", "Announcer 2", "Announcer 3"},
    {"Rey Ox", "Videl", "Kaiyoshin", "Shenron", "Locutor 1", "Locutor 2", "Locutor 3"},
};

static int sEs;           /* Spanish labels */
static int sAfterHeading; /* a screen's name or a window's text was just said */

/* A screen's name or a window's text: it cuts what was being said. */
static void heading(const char *text) {
    PortA11y_Say(text, A11Y_INTERRUPT);
    sAfterHeading = 1;
}

/* The flags for an item: the first one after a heading is queued behind it, the next ones cut the last item. */
static int follow(void) {
    int flags = sAfterHeading ? 0 : A11Y_INTERRUPT;

    sAfterHeading = 0;
    return flags;
}

/* Appends ", 2 of 5" (English) or ", 2 de 5" (Spanish) when there is more than one. */
static void add_position(char *text, int cap, int index, int count) {
    size_t len = strlen(text);

    if (count > 1 && index >= 0 && index < count) {
        snprintf(text + len, cap - len, sEs ? ", %d de %d" : ", %d of %d", index + 1, count);
    }
}

/* "label, 2 of 5" (English) or "label, 2 de 5" (Spanish). */
static void say_item(const char *label, int index, int count, int flags) {
    char text[256];

    snprintf(text, sizeof(text), "%s", label);
    add_position(text, sizeof(text), index, count);
    PortA11y_Say(text, flags);
}

/* Line `n` of one of the game's text files, as UTF-8 into out: the file is a table of byte offsets (entry n + 1)
   to UTF-16 strings that begin with U+FEFF (src/sys/dialog.c DIALOG_TEXT, src/battle/view_b.c
   TextBox_AttachLine). Empty when it does not look like such a line. Returns the length. */
static int text_line(char *out, int cap, uint32_t GAME_PTR file, int n) {
    uint32_t ofs;
    uint16_t GAME_PTR str;

    out[0] = '\0';
    if (file == NULL || n < 0 || n >= 0x1000) {
        return 0;
    }
    ofs = file[n + 1] >> 2 << 2;
    if (ofs == 0 || ofs >= 0x200000) {
        return 0;
    }
    str = (uint16_t GAME_PTR)((uint8_t GAME_PTR)file + ofs);
    if (str[0] != 0xFEFF) {
        return 0;
    }
    return PortA11y_Utf8(out, cap, (const uint16_t *)str);
}

/* The memory card window: its text when it opens or changes, then the choice under the cursor. */
static void dialog(void) {
    static char said[A11Y_TEXT], seen[A11Y_TEXT];
    static int cursor = -1;
    char text[A11Y_TEXT], body[A11Y_TEXT];
    A11yDialog GAME_PTR d = gDialog;

    if (d == NULL || !(d->flags & DIALOG_FLAG_OPEN)) {
        said[0] = seen[0] = '\0';
        cursor = -1;
        return;
    }
    if (d->layout == 0) {
        text_line(text, sizeof(text), d->msgTbl, d->msgIdx);
    } else {
        text_line(text, sizeof(text), d->titleTbl, d->titleIdx);
        if (text_line(body, sizeof(body), d->titleTbl, d->msgIdx) > 0 && strcmp(body, text) != 0) {
            snprintf(text + strlen(text), sizeof(text) - strlen(text), "%s%s", text[0] != '\0' ? ". " : "", body);
        }
    }
    if (strcmp(text, seen) != 0) { /* a window being set up can show a line for one blank: wait a blank */
        memcpy(seen, text, sizeof(seen));
        return;
    }
    if (text[0] != '\0' && strcmp(text, said) != 0) {
        memcpy(said, text, sizeof(said));
        heading(text);
        cursor = -1;
    }
    if (d->choices != 0 && (d->windowFlags & 2) && d->cursor != cursor && (d->cursor & ~1) == 0) {
        say_item(kYesNo[sEs][d->cursor], d->cursor, 2, follow());
        cursor = d->cursor;
    }
}

/* The title: "press start", then "New Game" / "Continue". */
static void title(void) {
    static int state = -1, cursor = -1;
    A11yTitle GAME_PTR t = gTitle;

    if (gProgress->mode != MODE_TITLE || t == NULL) {
        state = cursor = -1;
        return;
    }
    if (t->mcState != 0 || (t->flags & (TITLE_CHOSEN | TITLE_LEAVING))) {
        cursor = -1; /* (said again when the card's window closes without leaving) */
        return;
    }
    if (t->state == 1 && state != 1) {
        heading(sEs ? "Presiona START" : "Press START button");
    }
    if (t->state == 2 && t->cursor != cursor && t->cursor >= 0 && t->cursor < 2 && t->itemCount >= 1) {
        say_item(kTitleItems[sEs][t->cursor], t->cursor, t->itemCount, follow());
        cursor = t->cursor;
    }
    state = t->state;
}

/* The main menu: its name when it opens, then the item under the cursor (the guide's line about it is in the
   message window). */
static void main_menu(void) {
    static int open, item = -1;
    A11yMainMenu GAME_PTR m = gMainMenu;
    int index, cur;

    if (gProgress->mode != MODE_MAIN_MENU || m == NULL) {
        open = 0;
        item = -1;
        return;
    }
    if (m->itemCount < 1 || m->itemCount > 11 || m->top < 0 || m->cursor < 0 ||
        (m->flags & (MAINMENU_CHOSEN | MAINMENU_LEAVING))) {
        return;
    }
    index = (m->top + m->cursor + 1) % m->itemCount;
    cur = m->items[index];
    if (cur < 0 || cur > 10) {
        return;
    }
    if (!open) {
        heading(sEs ? "Menú principal" : "Main Menu");
        open = 1;
    }
    if (cur != item) {
        say_item(kMainMenuItems[sEs][cur], index, m->itemCount, follow());
        item = cur;
    }
}

/* The name of character `chara` from the select's own text (line chara of sections 43 and 44, as CharSel_Draw
   shows them), and its form after it when that says something else. */
static void char_name(char *out, int cap, A11yCharSel GAME_PTR cs, int chara) {
    char form[256];
    size_t len;

    if (text_line(out, cap, cs->nameText, chara) == 0) {
        if (chara >= MCHR_RANDOM && chara < MCHR_RANDOM + 4) {
            snprintf(out, cap, "%s", kCellMarks[sEs][chara - MCHR_RANDOM]);
        } else {
            snprintf(out, cap, sEs ? "Personaje %d" : "Character %d", chara);
        }
        return;
    }
    if (text_line(form, sizeof(form), cs->formText, chara) > 0 && strcmp(form, out) != 0) {
        len = strlen(out);
        snprintf(out + len, cap - len, ", %s", form);
    }
}

/* What each side of the character select showed when it was last told. */
static struct { int state, pos, row, chara; } sSide[2];
static int sSideSaid; /* the side last spoken for, -1 none */

/* One side of the character select: the character under the cursor, and each step after it (form, item set,
   costume, done) with its place in the list. */
static void char_side(A11yCharSel GAME_PTR cs, int i) {
    A11yCharSide GAME_PTR s = cs->side[i];
    A11yChrCell GAME_PTR cell;
    char text[A11Y_TEXT], what[A11Y_TEXT];
    int pos = 0, count = 0, cellIdx, newRow, flags;

    if (s == NULL || s->state < CHARSEL_ST_GRID || s->state > CHARSEL_ST_DONE) {
        return;
    }
    newRow = s->state != sSide[i].state || s->row != sSide[i].row;
    what[0] = '\0';
    switch (s->state) {
    case CHARSEL_ST_GRID:
        pos = s->col;
        char_name(what, sizeof(what), cs, s->chara);
        if (newRow && cs->rows[i] > 1) {
            snprintf(what + strlen(what), sizeof(what) - strlen(what), sEs ? ", fila %d de %d" : ", row %d of %d",
                     s->row + 1, cs->rows[i]);
        }
        break;
    case CHARSEL_ST_FORM:
        pos = s->form;
        char_name(what, sizeof(what), cs, s->chara);
        cellIdx = s->col + s->row * MCHR_COLS;
        if (!(s->flags & CHARSEL_SIDE_CUSTOM) && cs->cells[i] != NULL && cellIdx >= 0 && cellIdx < cs->cellCount[i]) {
            cell = &cs->cells[i][cellIdx];
            count = cell->formCount >= 1 && cell->formCount <= 7 ? cell->formCount : 0;
        }
        break;
    case CHARSEL_ST_ITEMSET:
        pos = s->customPlate;
        count = cs->customRows[i];
        if (pos == 0) {
            snprintf(what, sizeof(what), "%s", sEs ? "Sin objetos" : "No items");
        } else {
            snprintf(what, sizeof(what), sEs ? "Juego de objetos %d" : "Item set %d", pos);
        }
        break;
    case CHARSEL_ST_ITEMS:
        snprintf(what, sizeof(what), "%s", sEs ? "Panel de objetos" : "Item panel");
        break;
    case CHARSEL_ST_ITEMINFO: /* the item's description window over the panel */
        sSide[i].state = s->state;
        return;
    case CHARSEL_ST_COLOR:
        pos = s->colorPlate;
        count = cs->colorCount[i];
        snprintf(what, sizeof(what), sEs ? "Traje %d" : "Costume %d", pos + 1);
        break;
    case CHARSEL_ST_CUSTOM:
        pos = s->customCol + s->customRow * MCHR_COLS;
        count = cs->customCount[i];
        char_name(what, sizeof(what), cs, s->chara);
        break;
    case CHARSEL_ST_DONE:
        snprintf(what, sizeof(what), "%s", sEs ? "Listo" : "Ready");
        break;
    }
    if (s->state == sSide[i].state && pos == sSide[i].pos && s->row == sSide[i].row && s->chara == sSide[i].chara) {
        return;
    }
    sSide[i].state = s->state;
    sSide[i].pos = pos;
    sSide[i].row = s->row;
    sSide[i].chara = s->chara;
    add_position(what, sizeof(what), pos, count);
    flags = follow();
    if (i != sSideSaid) { /* the other side spoke last: say whose this is, after it */
        snprintf(text, sizeof(text), sEs ? "Jugador %d: %s" : "Player %d: %s", i + 1, what);
        if (sSideSaid != -1) {
            flags = 0;
        }
    } else {
        snprintf(text, sizeof(text), "%s", what);
    }
    sSideSaid = i;
    PortA11y_Say(text, flags);
}

/* A stage's name by id: the disc's, then the ones the port adds (plat_stages.h), then the grid's markers. */
static const char *stage_name(int id) {
    if (id >= 0 && id <= CHARSEL_STAGE_RANDOM) {
        return kStageNames[sEs][id];
    }
    if (id >= PORT_STAGE_FIRST && id - PORT_STAGE_FIRST < gPortExtraStageCount && id - PORT_STAGE_FIRST < PORT_STAGE_MAX) {
        return gPortStageNames[id - PORT_STAGE_FIRST];
    }
    return sEs ? "Bloqueado" : "Locked"; /* STGGRID_ID_LOCKED; the empty cells are not reached */
}

/* A song's name by music-list value: the disc's, "random", the ones the port adds (plat_songs.h), else locked. */
static const char *song_name(int v) {
    if (v >= 0 && v < 20) {
        return kSongNames[v];
    }
    if (v == CHARSEL_BGM_RANDOM) {
        return sEs ? "Aleatoria" : "Random";
    }
    if (v >= PORT_SONG_FIRST_OFFSET && v - PORT_SONG_FIRST_OFFSET < gPortSongCount && v - PORT_SONG_FIRST_OFFSET < PORT_SONG_MAX) {
        return gPortSongNames[v - PORT_SONG_FIRST_OFFSET];
    }
    return sEs ? "Bloqueada" : "Locked"; /* 0x19, and the disc's four "********" */
}

/* The stage and music choice: the stage under the cursor, the music entry with the song that plays, and in the
   music list the song under the cursor. */
static void char_stage(A11yCharSel GAME_PTR cs) {
    static int open, state, col, row, value;
    A11yCharStage GAME_PTR st;
    char text[A11Y_TEXT];
    int v;

    if (cs == NULL) { /* the choice starts (again) */
        open = 0;
        state = -1;
        return;
    }
    st = cs->stage;
    if (st == NULL || cs->bgmIds == NULL || cs->bgmCount < 1 || cs->bgmCount > 64) {
        return;
    }
    if (!open) {
        heading(sEs ? "Escenario y música" : "Stage and music");
        open = 1;
    }
    if (st->state == CHARSEL_ST_STAGE && st->col == CHARSEL_STAGE_BGM_COL) {
        v = st->bgmPlaying >= 0 && st->bgmPlaying < cs->bgmCount ? cs->bgmIds[st->bgmPlaying] : -1;
        snprintf(text, sizeof(text), sEs ? "Música: %s" : "Music: %s", song_name(v));
    } else if (st->state == CHARSEL_ST_STAGE) {
        v = st->stage;
        snprintf(text, sizeof(text), "%s", stage_name(v));
        if ((st->row != row || state != CHARSEL_ST_STAGE) && cs->stageRows > 1) {
            snprintf(text + strlen(text), sizeof(text) - strlen(text), sEs ? ", fila %d de %d" : ", row %d of %d",
                     st->row + 1, cs->stageRows);
        }
    } else if (st->state == CHARSEL_ST_BGM && st->bgm >= 0 && st->bgm < cs->bgmCount) {
        v = cs->bgmIds[st->bgm];
        snprintf(text, sizeof(text), "%s", song_name(v));
        add_position(text, sizeof(text), st->bgm, cs->bgmCount);
    } else {
        return;
    }
    if (st->state == state && st->col == col && st->row == row && v == value) {
        return;
    }
    state = st->state;
    col = st->col;
    row = st->row;
    value = v;
    PortA11y_Say(text, follow());
}

/* The character select: its name when the cursors light, each side's choice, then the stage and music. Quiet once
   the stage is confirmed. */
static void char_select(void) {
    static int open, stage;
    A11yCharSel GAME_PTR cs = gCharSel;
    int i;

    if (cs == NULL || !(cs->flags & CHARSEL_STARTED)) {
        open = 0;
        return;
    }
    if (cs->flags & CHARSEL_LEAVING) {
        return;
    }
    if (!open) {
        heading(sEs ? "Selección de personajes" : "Character select");
        open = 1;
        stage = 0;
        sSideSaid = -1;
        for (i = 0; i < 2; i++) {
            sSide[i].state = sSide[i].pos = sSide[i].row = sSide[i].chara = -1;
        }
    }
    if (cs->flags & CHARSEL_STAGE_PHASE) {
        if (!stage) {
            stage = 1;
            char_stage(NULL);
        }
        char_stage(cs);
        return;
    }
    if (stage) { /* back from the stage choice to the sides */
        stage = 0;
        for (i = 0; i < 2; i++) {
            sSide[i].state = -1;
        }
    }
    for (i = 0; i < 2; i++) { /* with one pad, the sides it has reached (`turn`) */
        if (cs->players == 1 || i <= cs->turn) {
            char_side(cs, i);
        }
    }
}

/* The label of value `v` of the duel setting in row `row`, NULL when out of range. */
static const char *duel_value(int row, int v) {
    switch (row) {
    case 0:
        return v >= 0 && v < 5 ? kDuelTime[sEs][v] : NULL;
    case 1:
        return v >= 0 && v < 5 ? kDuelCpu[sEs][v] : NULL;
    case 2:
        return v >= 0 && v < 7 ? kDuelReferee[sEs][v] : NULL;
    case 3: /* one value per side */
    case 4:
        return (v & ~1) == 0 ? kYesNo[sEs][v] : NULL;
    }
    return NULL;
}

/* A row of the duel settings as the list shows it: the name and the saved value (one per side in row 3). */
static void duel_setting(char *out, int cap, int row) {
    const char *a, *b;
    size_t len;

    snprintf(out, cap, "%s", kDuelSettings[sEs][row]);
    len = strlen(out);
    if (gSaveData == NULL || row >= 5) {
        return;
    }
    if (row == 3) {
        a = duel_value(3, gSaveData->rule[3]);
        b = duel_value(3, gSaveData->rule[4]);
        if (a != NULL && b != NULL) {
            snprintf(out + len, cap - len, sEs ? ": jugador 1 %s, jugador 2 %s" : ": 1P %s, 2P %s", a, b);
        }
    } else if ((a = duel_value(row, gSaveData->rule[row == 4 ? 5 : row])) != NULL) {
        snprintf(out + len, cap - len, ": %s", a);
    }
}

/* The duel menu: its name when the cursor lights, then the plate under the cursor of the level in use; in the
   settings each row with its saved value, then the value list. Quiet once a battle is chosen, and while the
   "restore the defaults?" question runs (dialog() reads it). */
static void duel_menu(void) {
    static int open, level;
    static char said[256];
    A11yDuelMenu GAME_PTR m = gDuelMenu;
    char text[256];
    const char *label = NULL;
    int lv, cur, count = 0;

    if (m == NULL || !(m->flags & DUELMENU_STARTED)) {
        open = 0;
        return;
    }
    if ((m->flags & (DUELMENU_CHOSEN | DUELMENU_LEAVING)) || m->startState != 0 || m->resetState != 0) {
        said[0] = '\0'; /* the row is said again when the question closes */
        return;
    }
    lv = m->level;
    if (lv < DUEL_LEVEL_TOP || lv > DUEL_LEVEL_VALUE) {
        return;
    }
    if (!open) {
        heading(sEs ? "Menú versus" : "Duel");
        open = 1;
        level = lv;
        said[0] = '\0';
    }
    if (lv == DUEL_LEVEL_SETTING && level == DUEL_LEVEL_TOP) {
        heading(kDuelTop[sEs][3]);
    }
    level = lv;
    text[0] = '\0';
    cur = m->sel[lv];
    switch (lv) {
    case DUEL_LEVEL_TOP:
        count = 4;
        label = cur >= 0 && cur < count ? kDuelTop[sEs][cur] : NULL;
        break;
    case DUEL_LEVEL_TYPE:
        count = 3;
        label = cur >= 0 && cur < count ? kDuelType[sEs][cur] : NULL;
        break;
    case DUEL_LEVEL_DP:
        count = 3;
        label = cur >= 0 && cur < count ? kDuelDp[sEs][cur] : NULL;
        break;
    case DUEL_LEVEL_SETTING:
        count = DUEL_SETTINGS;
        if (cur >= 0 && cur < count) {
            duel_setting(text, sizeof(text), cur);
        }
        break;
    case DUEL_LEVEL_VALUE:
        if ((m->column & ~1) != 0 || m->valueCount < 1 || m->valueCount > 7) {
            return;
        }
        cur = m->value[m->column];
        count = m->valueCount;
        label = duel_value(m->sel[DUEL_LEVEL_SETTING], cur);
        if (label != NULL && m->sel[DUEL_LEVEL_SETTING] == 3) {
            snprintf(text, sizeof(text), sEs ? "Jugador %d: %s" : "%dP: %s", m->column + 1, label);
        }
        break;
    }
    if (label != NULL && text[0] == '\0') {
        snprintf(text, sizeof(text), "%s", label);
    }
    if (text[0] == '\0') {
        return;
    }
    add_position(text, sizeof(text), cur, count);
    if (strcmp(text, said) != 0) {
        snprintf(said, sizeof(said), "%s", text);
        PortA11y_Say(text, follow());
    }
}

/* The message window's line, whenever it shows another one; queued behind the item it is about. */
static void msg_window(void) {
    static uint16_t GAME_PTR said;
    A11yMsgWin GAME_PTR w = gMsgWin;
    char text[A11Y_TEXT];

    if (w == NULL) {
        said = NULL;
        return;
    }
    if (w->str == said) {
        return;
    }
    said = w->str;
    if (said == NULL || w->text == NULL || said[0] != 0xFEFF) {
        return;
    }
    if (PortA11y_Utf8(text, sizeof(text), (const uint16_t *)said) > 0) {
        PortA11y_Say(text, 0);
    }
}

/* Once per blank (plat_stub.c: Port_VBlank). */
void PortA11y_GameTick(void) {
    if (!PortA11y_Begin() || gPortResim) {
        return;
    }
    sEs = PortA11y_Spanish();
    dialog();
    if (gProgress != NULL) {
        title();
        main_menu();
    }
    duel_menu();
    char_select();
    msg_window();
}
