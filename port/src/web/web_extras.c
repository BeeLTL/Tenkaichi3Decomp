/*
 * Browser build: what becomes of the content added from outside the disc (stages, songs, texture packs).
 *
 * The native build finds it by looking through folders: plat_extras.c (stages/, songs/: stat and opendir) and
 * gs/gs_texpack.c (textures/: SDL_GetPathInfo, SDL_EnumerateDirectory). In the browser the data folder is read over
 * HTTP, one file at a time (web_fs.c), and cannot be looked through, so those lookups find no folder. Natively "no
 * folder" means "nothing added", and nothing is said; here it would hide that the feature cannot work at all. The
 * three start-up calls are linked wrapped (wasm_build.py: --wrap=PortStages_Init,PortSongs_Init,TexPack_Init): each
 * runs as always, and when it found nothing, that is said once, with the reason. Stages named through
 * BT3_EXTRA_STAGES (plat_stages.c, no folder needed) still count as found. The page reads the same state
 * (web_ui.c: added_checked, added_stage_count, added_song_count, texture_pack_count).
 */
#include <stdio.h>

void __real_PortStages_Init(void);
void __real_PortSongs_Init(void);
void __real_TexPack_Init(void);
int PortStages_Count(void);
int PortSongs_Count(void);
int TexPack_Count(void);

enum { CHECKED_STAGES = 1, CHECKED_SONGS = 2, CHECKED_TEXTURES = 4 };
static int sChecked;

static const char kWhy[] = "not available in the browser version (the game's data folder is read over the network, "
                           "one file at a time, and cannot be looked through)";

/* After the call for `bit`: says once when nothing was found. */
static void checked(int bit, int count, const char *what) {
    if (sChecked & bit) {
        return;
    }
    sChecked |= bit;
    if (count == 0) {
        fprintf(stderr, "bt3: %s: %s\n", what, kWhy);
    }
}

void __wrap_PortStages_Init(void) {
    __real_PortStages_Init();
    checked(CHECKED_STAGES, PortStages_Count(), "added stages");
}

void __wrap_PortSongs_Init(void) {
    __real_PortSongs_Init();
    checked(CHECKED_SONGS, PortSongs_Count(), "added songs");
}

void __wrap_TexPack_Init(void) {
    __real_TexPack_Init();
    checked(CHECKED_TEXTURES, TexPack_Count(), "texture packs");
}

/* Which of the three have been looked for (CHECKED_*): the page says "not checked yet" before. */
int WebExtras_Checked(void) {
    return sChecked;
}
