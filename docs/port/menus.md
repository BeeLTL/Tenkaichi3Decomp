# Menus: adding an entry and the screen behind it

How the PC port added **Tenkaichi Budokai Online** (TBO) to the main menu and the screens it opens, written
down so the same can be done again and, later, offered to mods. Everything here was done and checked on the
`tbo` branch (2026-10-14/15); where something is a limit or an open question, it says so. The details of each
step are in [README.md](README.md) (section "Tenkaichi Budokai Online: lettered like the game's menus").

Rules that hold throughout: the decomp's own files change only inside `#ifdef PORT` (and the PS2 matching build
must still pass: `build/SLUS_216.78.ok`, `build/DBZP.ok`); nothing from the disc is committed (pictures made from
it exist in memory only); an online session runs the stock game.

## 1. How the game's menus work

- **The progress mode.** `gProgress->mode` says which screen runs; `Progress_Main` (src/menu/menu_a_b.c) calls
  that screen's frame loop until it returns, then reads the next mode. The main menu is mode 4, the versus mode
  38 (its menu) and 39 (its character select); the full list is in `docs/systems/menu_overlay.md` (decomp).
- **A screen is a Flash-like movie** from the menu archives (src/sys/gfxm_c.c, `Flash_*`): clips found by name
  (`Flash_FindLabel`), shown or hidden (`Flash_ClipSetFlags(..., 2, on)`), stepped (`Flash_Advance`) and drawn
  (`Flash_Draw`). A movie's shapes take their pictures from the screen's texture list (`flash->tex[i]`).
- **Labels are pictures, not text.** The plates' words ("Dragon Net Battle", "Options") are painted into texture
  sheets on the disc. The game has no font for them. Running text (descriptions, dialogue) is the game's font
  (src/battle/col_c.c, `Font_*`), written by a text box (src/battle/view_b.c, `TextBox_*`) hung on a clip.
- **Shared windows.** Every menu uses the same two: the heading window (`IconWin`, src/battle/view_a_c.c: the
  cloud swirls, the emblem, the heading's words, the line under them) and the message window (`MsgWin`,
  src/battle/view_a_b.c: the teal box with the meander corners and the line about the item under the cursor).

## 2. Adding an entry to the main menu

All in src/menu/menu_a.c, under `#ifdef PORT`.

### The item

- `MAINMENU_ITEM_MAX` (include/menu/menu_a.h) is 12 on PC: item 11 is the new one (`MAINMENU_TBO`).
- `MainMenu_Init` builds the list in a loop over the items. The new item is **inserted** where it should appear
  (after Dragon Net Battle, or after Duel when that one is hidden) and **skipped** by the loop itself; otherwise
  it is listed twice (it was, until that was found).
- Its icon is another item's (`MM_ICON`: Dragon Net Battle's); it has no voiced line of the guides
  (`MAINMENU_VOICED`).
- Choosing it (`MainMenu_Update`): `MAINMENU_CHOOSE(mode)` sets the next progress mode and plays the plate's
  "out" animation. TBO sets `gPortDuelRole = 6` and chooses mode 38 (see 3.).

### The label (the item's words)

The plate clips take item *n*'s label from row *n* of the label sheets: sheet 1 holds rows 0-7, sheet 2 (512 x 128,
four rows of 32) rows 8-10 and **one free row, 3**. There is a sheet per plate state: `gMainMenu->tex[8]` (as it is)
and `tex[12]` (chosen); the second sheet is the texture entry right after the first (`+ 0x40`). The sheets are
8-bit pictures with a 256-colour table, stored the way the GS keeps them (`PortMainMenu_PixelAt` finds a pixel).

The label is **put together from the disc's own letters**, so it matches the other plates exactly:

1. `MenuLabels_MainItem` (port/src/gs/menu_labels.c) reads archives 448 and 449 from the player's game data
   (size and CRC checked), cuts the main menu's ten labels into letters with the cutter the added stages use
   (port/src/gs/namefont.c), lends the letters they lack from the stage select's names (the `k` of "Tenkaichi"
   from "Rocky"/"Lookout", resized), and composes the text in both states (512 x 32 RGBA each). About 60 ms, in a
   thread started with the first frame (menu_text.c).
2. `Port_MainItemLabel` (menu_text.c) turns the picture into the sheet's own values: for each pixel the nearest
   colour of the sheet's table (`MenuLabels_ItemPixels`), in the table's order.
3. `PortMainMenu_WriteDisc` writes them into the free row. If any of this fails (another edition of the game),
   the old picture of `port/include/port_tbo_label.h` is written instead.

**Menu font setting.** With a font chosen in F1 > UI, the port draws the game's labels again in that font. It
finds a sheet by a hash of *all* its pixels, so writing a label into a sheet changes the hash: the item and the
three labels already on that sheet were left in the disc's lettering. The list of labels now writes the item into
the sheet as the game does (the same `MenuLabels_ItemPixels`), hashes that, and lists the item as the sheet's
twelfth label (`ml_item_into`); its version was raised (v8) so older lists are made again.

**Limits.**
- **Icons can be shared, labels cannot.** The plate's icon is a 64 x 64 cell of the icon sheet chosen by item
  number (`(n % 4) * 64`, `(n / 4) * 64`; the animated one a 48-pixel strip, `n % 10`), so a new entry can point
  at any existing item's icon (`MM_ICON`). Its words have to be its own: the plate reads row `n % 8` of label
  sheet `n / 8` (`Flash_ClipSetUv` / `Flash_ClipSetTex` in `MainMenu_Draw`).
- One free row: **one** new entry fits the existing sheets. Item 12 would read row 4 of sheet 2, which is only
  four rows high. More entries need a larger (or a third) sheet the plates are pointed at: not looked into yet.
  (Shenron's row is unused while Shenron is not unlocked, but an entry there would lose its words when it is.)
- A label can only use letters the disc's labels have (or can be lent from the stage names). Any text a mod
  might want would need a fallback (a font, as the screens use).

## 3. Linking the entry to a screen of its own

There is no free progress mode to give a new screen, and making a new movie is not possible (no tool writes the
movie format). TBO borrows the **versus mode**: the item chooses mode 38 with a role flag, and `Duel_Main`
(src/menu/menu_g_b.c) runs the new screen instead of the versus menu:

```c
if (Port_NetSession()) { gPortDuelRole = 0; }       /* (see "Pitfalls") */
if (gPortDuelRole == 6) {
    if (PortTbo_Run()) { gProgress->mode = 45; ... } /* left for Ultimate Training ("Train While Waiting") */
    else               { gProgress->mode = 4; ... }  /* back to the main menu */
    gPortDuelRole = 0;
    done = 1;
}
```

The versus mode's archive (`gMenuArc5`) is loaded then, which the screen uses for the game's windows. The
screen itself (src/menu/port_tbo.c, `PortTbo_Run`) is an ordinary frame loop, as the game's own:

```
Gfx_BeginFrame, Pad_Update, Snd_Update, ColorFade_Update
    the page's update (cursor, choices)        Tb_*Update
    speech for the screen reader               Tb_Speak
    drawing                                    Tb_Backdrop, IconWin_Draw, Tb_Draw, Tb_DrawText, prompts
ColorFade_Draw, Gfx_EndFrame, Dma_Flush
```

### The game's windows in a screen of ours

From the versus menu's data (archive 448, member 1: section 21 the message text, 22 the message window,
23 the heading window, 24 its icons), made as `DuelMenu_Init` makes them (`Tb_WinInit` / `Tb_WinTerm`):

- **Heading window.** `IconWin_Init`, `IconWin_Open`, then `IconWin_Draw` each frame. Its words ("Versus Menu")
  and emblem are taken out by emptying their texture slots, 4 and 2 (`IconWin_PortHideWords`): a shape whose
  texture is missing is not drawn. Hiding their clips does not last: `Flash_Advance` shows a movie's clips again
  every frame.
- **Message window.** `MsgWin_Init`, `MsgWin_Open`, then `MsgWin_PortDrawString(str)` each frame: the window with
  a line of ours, written by the game's own text box in the game's font at the game's place. `str` is the game's
  kind of text: 16-bit, `0xFEFF` first, `\n` between lines, no `<` (it starts a tag). Lines are wrapped with the
  font's own widths (`Font_GetWidth`) up to the step of the box's top right corner; `MsgWin_PortSameTop` makes one
  and two lines start at the same height. Hand over a different buffer when the line changes: the screen reader
  tells lines apart by where they are.

### What the screen draws itself

- **Pictures:** PNG files in `tbo/ui/` next to the program (include/menu/tbo_art.h names them), drawn as sprites
  with the reserved format `TBO_PSM` (port/src/gs/gs_draw.c, `tbo_texture`) at the PNG's own resolution. Their
  width : height is taken from a table built into the program (include/menu/tbo_sizes.h): a replacement keeps the
  shape or the table changes. They are not in the repository (see the README).
- **Text:** TrueType lines (port/src/gs/tbo_text.c) in the Menu font for headings and buttons, the game's font
  for the line in the box. The heading is one picture in the main menu's heading style (`MenuText_HeadingRender`).
- **Coordinates:** a page is 4:3 in the middle of the window (`Tb_PageSet`, `Tb_X`, `Tb_Y`: the game's 512 x 448
  units of that page); the game's movies (the windows) are drawn over the **whole** screen. Converting between the
  two is needed wherever ours meet theirs (the box's corner, where the text box writes).
- **Transitions:** a page slides in from the left as it opens (`Tb_In`, the offset `sTbShift` in `Tb_X`); the
  title, player card and box stay.
- **Screen reader:** our own text is pictures, which the reader (port/src/a11y_game.c) cannot read. The box's line
  is read because it is the game's text box; everything else the screen says itself through `PortA11y_Say`
  (`Tb_Speak`: the title as a page opens, the item under the cursor and its value, keys, what was typed, notes).
  Check without a speech library with `BT3_A11Y_LOG=<file>`.

## Recipe: a new main-menu item, step by step

What to change, in order. Item 11 is taken by TBO; with the existing sheets there is no room for a second (see the
limits in 2.), so this is the list for replacing what item 11 does, or for whoever gives the plates a larger sheet.

1. **Its number and place** (src/menu/menu_a.c, `#ifdef PORT`): a `#define` for the item number; in
   `MainMenu_Init`'s loop, `continue` on that number, and insert it after the item it should follow.
2. **Its icon:** `MM_ICON` maps it to the item whose icon it shows.
3. **Its voice and line:** `MAINMENU_VOICED` keeps it from playing a guide's line (there is none for it):
   `voiceLine` is -1 on it, so **the message box is empty and nothing is said**. Open: write a line of its own
   into the box with `MsgWin_PortDrawString` (section 3) in the main menu's draw, when the cursor is on it.
4. **What choosing it does:** a `case` in `MainMenu_Update`'s switch: `MAINMENU_COUNT(); MAINMENU_CHOOSE(mode);`.
   - To open an existing mode, that mode's number is all (no screen to write).
   - To open a screen of its own, set a role flag first and catch it in that mode's main function, as
     `gPortDuelRole` / `Duel_Main` do (section 3), clearing the flag in an online session.
5. **Its words:** the text is `kMlItem` (port/src/gs/menu_labels.c), used both to compose the label at start
   (`MenuLabels_MainItem`) and to list it for the Menu font (`ml_item_into`). Every letter has to be on the disc's
   labels or the stage names (check with the cutter: a missing letter makes the composition fail, and the log says
   so). The fallback picture (`port_tbo_label.h`) is TBO's own: a new text has none, so without the USA disc's
   archives it would show TBO's words. Raise `MENU_LABELS_VERSION` (menu_labels.h) whenever what the list writes
   changes.
6. **The screen** (if it has one): start from `PortTbo_Run`'s frame loop (section 3): the windows (`Tb_WinInit` /
   `Tb_WinTerm`), the page's update, the speech, the drawing, a fade out, and a return value its caller turns into
   the next progress mode.

**Checks before it is done:**

- the item in both plate states (not chosen / under the cursor), and listed once;
- with a Menu font chosen in F1 > UI: the item and the three labels on its sheet (Character Reference, Options,
  Shenron) drawn in that font (if not, the sheet's hash in the list does not match: step 5);
- the screen reader's log (`BT3_A11Y_LOG`): the screen's pages, items and values said;
- in and out of the screen several times; an online match started from it, and back;
- the PC build without `error:` in its output; the PS2 matching build (both `.ok` files).

## 4. Pitfalls found on the way

- **An online session restarts the game.** A match found from a screen of ours puts the game back to its first
  moment and goes to mode 39; when the session ends, the game is put back to the moment the match was asked
  for. The port's own globals are not put back: `gPortDuelRole` was still 6 inside the session (the match opened
  the screen again instead of the character select), and the screen came back on its search page after the
  match. Fixes: `Duel_Main` clears the role in a session; the screen notes `Port_NetSessionsEnded()` (port memory,
  not rewound) when it starts a match and goes to its first page when it has changed.
- **Draw order is call order** (`Flash_Draw` adds the default drawing state, then draws into the same stream).
  The message box drawn after the hosts covers them; it is see-through, so what is cut behind it shows.
- **Labels' hashes** (see 2.): anything written into a game texture changes how the Menu font finds it.
- **Game pointers are 32-bit** in the decomp's code (`ptr32`): pass game addresses to port code as integers and
  turn them back with `Port_Ptr`; no `?:` between a table's pointer and a literal.
- **A file that does not compile still links** (the old object is used): look for `error:` in the build's output,
  not only "link OK"; a changed stub count is the other sign.

## 5. What is possible, towards a mod framework

What exists now, and what a mod-facing version would add:

| | Now (TBO) | For mods |
|---|---|---|
| A new main-menu entry | one, in code | a manifest entry: its text, its place, what it opens; the label from the disc's letters, a font where letters are missing |
| More than one new entry | no (one free label row) | a larger label sheet: to be found out |
| Entries in other menus | not tried | the same three parts (list, label sheet, choice) per menu; each menu's list and sheets have to be mapped first |
| A screen of its own | in code (port_tbo.c), reached through the versus mode | the `Tb_*` toolkit (rows, keypads, windows, prompts, speech) as a library a screen is described with |
| The game's look | heading window, message box, game font in the box | the same, with any menu's windows (each menu's archive section to be listed) |
| A native list (the game's own plates) | not done | reuse an existing list movie, its labels made from disc letters |
| New movies | not possible | (no tool writes the format) |
| Online | screens of ours run the stock game's session | mods' entries and screens off in a session, as all mods |

The first step for mods would be the settings entry the port itself could use (a "PC settings" item opening a
screen of rows), since it needs only what TBO already proved.
