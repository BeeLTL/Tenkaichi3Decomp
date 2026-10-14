#ifndef MENU_PORT_SCREEN_H
#define MENU_PORT_SCREEN_H
#ifdef PORT
/*
 * PC only: the screens of the port's own (menu/port_screen.c). They run in one progress mode of their own, which
 * the game does not have; which screen runs is gPortScreen, set before the mode is chosen. See docs/port/menus.md.
 */
#define MPROG_MODE_PORT_SCREEN 80 /* (the game's modes end at 70) */

enum {
    PORT_SCREEN_NONE,
    PORT_SCREEN_TBO, /* Tenkaichi Budokai Online (menu/port_tbo.c) */
};

extern s32 gPortScreen; /* the screen the mode runs next (PORT_SCREEN_*) */

/* Progress_Main's handler of the mode: runs gPortScreen's screen and chooses the next mode. Non-zero when a battle
   was set up (the menus are left), as the game's handlers return. */
s32 PortScreen_Main(void);
#endif
#endif
