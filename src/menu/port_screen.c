#include "common.h"
#include "menu/menu_a.h"
#include "menu/port_screen.h"

#ifdef PORT
/*
 * PC only: the progress mode of the port's own screens (include/menu/port_screen.h, docs/port/menus.md). The game
 * has no mode to spare for a new screen, so the screens run in one that Progress_Main (menu_a_b.c) hands to
 * PortScreen_Main; gPortScreen says which. A screen reads what it needs itself: the windows of the game's menus come
 * from the versus mode's archive (gMenuArc5, as Duel_Main reads it).
 */

extern s32 PortTbo_Run(void); /* menu/port_tbo.c: 1 = left for "Train While Waiting" (its fight is set up) */

s32 gPortScreen;

s32 PortScreen_Main(void) {
    s32 screen = gPortScreen;
    s32 result = 0;

    gPortScreen = PORT_SCREEN_NONE;
    if (gMenuArc5 == NULL) {
        gMenuArc5 = File_LoadSync(gProgress->baseFile, NULL, 0);
    }
    switch (screen) {
    case PORT_SCREEN_TBO:
        Bgm_Play(0x10B16 + 2);
        result = PortTbo_Run();
        Adx_StopAll();
        gProgress->mode = result ? 45 : 4; /* the Ultimate Training's fight, or back to the main menu */
        break;
    default:
        gProgress->mode = 4;
        break;
    }
    return result;
}
#endif
