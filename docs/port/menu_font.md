# Menu fonts

Two settings in F1 > UI draw text from a TrueType font instead of the game's own lettering. Both are on "Original"
unless chosen, and with "Original" nothing of this is active.

| setting | what it replaces | what it needs |
|---|---|---|
| Menu font | the labels of the menus (pictures on the disc: "Dragon History", "1P VS COM", ...), and the names of added stages and songs | the fonts, and a list of labels the game makes from your disc data the first time it starts |
| Dialogue / UI font | the game's shared text font (`Font_Print`): dialogue, subtitles, explanations, ordinary menu text | the fonts |

So the Dialogue / UI font does change the game's ordinary `Font_Print` text: that is what it is for. Placement,
line breaks, colours, shadows and the button icons stay the game's; only the glyph pictures are drawn again.
Decorative headings and the story's battle titles keep their original artwork with both settings.

## The fonts

The fonts are in `port/third_party/fonts/` (in a release: `fonts/` next to the program), one folder per family
with its licence, and the list `fonts.tsv`:

```
# file <tab> name in the list <tab> use: 1 = Menu font, 2 = Dialogue / UI font, 3 = both
dejavu/DejaVuSansCondensed.ttf	DejaVu Sans Condensed	3
dejavu/DejaVuSansCondensed-Bold.ttf	DejaVu Sans Condensed Bold	1
liberation/LiberationSans-Bold.ttf	Liberation Sans Bold	1
```

To add a font, put the `.ttf` there and add a line; restart the game. A font has to be a static TrueType file
(glyf outlines; not `.otf`, not a collection, not a variable font), have a capital `H` (the letter height is taken
from it) and be at most 16 MB. The saved choice is kept by the file's path, not by its place in the list.

A font file is checked when it is chosen and at every start. One that is cut short, damaged or of another kind is
not used: the setting goes back to "Original", and the log has one line with the reason, for example

```
bt3: font "Liberation Sans Bold" (fonts/liberation/LiberationSans-Bold.ttf) is not used: a table lies outside
the file (the file is cut short or damaged). The setting is back on "Original".
```

Bundled: DejaVu Sans Condensed and Condensed Bold (Bitstream Vera licence, `dejavu/LICENSE`) and Liberation Sans
Bold (SIL Open Font License 1.1, `liberation/LICENSE`).

## The menu labels

Which texture has which label, and how large its letters are, is measured on your own disc data. The game does it
itself (`port/src/gs/menu_labels.c`), in a source installation and in a release alike: when it starts with a
window and `gamedata/game-text/labels.tsv` is not there, or is of an older format, it reads the ten menu archives
`gamedata/pzs3us1/00448.bin` ... `00463.bin` and writes `gamedata/game-text/`:

- `labels.tsv` and `name-styles.tsv`: what the game reads;
- `labels.json`: the same list for a person to edit (below). One that is already there is kept as it is.

This takes about a quarter of a second and runs in the background: the game does not wait for it. Until it is
done "Menu font" offers only "Original"; then the fonts appear in the list, and a font that the settings file
already names is taken into use between two frames (a menu on screen at that moment changes its lettering). The
log says `bt3: menu text: the list of labels was made from the disc (397 labels, 207 ms) in gamedata/game-text`.
Each file is written under another name and renamed when complete: a list is never half there.

The texture numbers are those of the USA release (SLUS-21678), so the ten archives are checked by size and CRC-32
before anything is read from them. With any other disc, a missing archive or a folder that cannot be written to,
nothing is made, the log has one line saying why, and "Menu font" offers only "Original"; the game is otherwise
unaffected. (It is tried again at the next start.) `BT3_MENU_FONT_ANY_DISC=1` leaves the check out, for a changed
archive: where its textures are numbered differently, labels are missing or wrong.

A run whose buttons are recorded or played back, or that is re-run blank by blank (`BT3_PAD_REC`, `BT3_PAD_PLAY`,
`BT3_PAD_TABLE`, `BT3_SYNCTEST`), makes the list at start, before anything is drawn: its pictures do not depend on
how fast the list was made. During an online session nothing is taken into use; a list finished then waits for
the session's end. Without a window (`BT3_GS=none`) nothing of this runs.

The list is derived from the disc and is never part of the repository or of a package. Before this was in the
game it was a step of `install.py` (a Python tool with a C helper built by gcc), which a release could not run;
the files the game writes are the same bytes as that tool's, and a `game-text/` folder made by it goes on working.

## Edit labels

1. Open `gamedata/game-text/labels.json` and find the entry by `id` or `original`.
2. Change `text`. If the entry has `text_from`, edit that referenced entry to change both selection states.
3. Apply the changes and restart the game:

   ```sh
   python3 port/tools/menu_font.py
   ./play.sh
   ```

No game rebuild or archive repacking is needed. The tool turns `labels.json` into `labels.tsv` and
`name-styles.tsv`; it reads nothing else (not the disc) and needs only Python. For a release, take
`port/tools/menu_font.py` from the source and name the folder: `python3 menu_font.py --out <game>/gamedata/game-text`.
The game itself never reads `labels.json` and never writes over one. To go back to the disc's labels, delete
`labels.json` and `labels.tsv`: the game writes both again at its next start.

| field | effect |
|---|---|
| `text` | Label text; `\n` makes two lines |
| `text_from` | Entry supplying shared text and enabled state |
| `enabled` | `false` keeps the original sprite |
| `italic` | `true` slants this label; default `false`. Enabled for “Press START button”. |
| `color_style` | `purple`, `main-menu`, `orange`, or `title-prompt` |
| `cap_height`, `baseline` | Letter height and baseline, in original texture pixels |
| `align`, `margin` | `left` or `center`, with horizontal margin |
| `width_scale` | Horizontal scale, 0.5–2.0; default 1.0 |
| `art_width` | Measured width of the original letter fill |

Leave texture identity and cell bounds unchanged: `hash`, `texture_width`, `texture_height`, `x`, `y`,
`width` and `height`. Edit the JSON, not the generated `labels.tsv` or `name-styles.tsv`.

Unchanged labels match the original artwork's letter width and intended height. Edited labels and added
names use natural font proportions, shrinking to fit. Text supports up to two lines and 255 UTF-8 bytes.
Characters absent from the selected font are skipped. Empty labels, or labels with no supported characters,
keep the original sprite. The tool reports characters unsupported by every font of the Menu font list.
Complex-script shaping and bidirectional text are not supported.

## Text styles

Each label's `color_style` references a shared entry in `color_styles` at the top of `labels.json`.
Styles apply across selectable typefaces. Colors are defined explicitly; artwork is measured only for layout.

| Type | Used for | Default fill, top → bottom | Outline |
|---|---|---|---|
| `purple` | Most unselected labels and unselected song names | White `#FFFFFF` | Purple `#460059` |
| `main-menu` | Unselected main-menu labels | Light gray `#CBCBCB` | Lighter purple `#5E236D` |
| `orange` | Selected labels, stage names and character-preview numbers | Yellow `#F4EF89` → orange `#D2684B` | Dark brown `#2C151B` |
| `title-prompt` | “Press START button” | Cream `#EFEFDF` → yellow-green `#B1B360` | Dark red `#3F1511` |

Colors in the JSON use RGB triples, such as `[203, 203, 203]` for `#CBCBCB`.
Existing catalogs keep their saved style values; editing a shared style changes all labels using it.
Added song and stage names share the corresponding fill and outline colors.

| Style field | Effect |
|---|---|
| `top`, `bottom` | Fill gradient; equal RGB triples give a solid fill |
| `outline` | Inner outline RGB color |
| `outline_width` | Optional width, 0–8 original texture pixels; default automatic, usually 3 |
| `outer_outline` | Optional outer outline RGB color |
| `outer_outline_width` | Width, 0–8 original texture pixels; default 0 (disabled) |
| `shadow` | Offset shadow; default `true` |

`title-prompt` uses a 2-pixel inner outline, a 2-pixel pale outer outline (`#D9C2C1`), and no shadow.
Its label also enables `italic`; italic is a label setting, separate from the shared color style.
After editing styles, run `python3 port/tools/menu_font.py` and restart the game. A style has to be in the file
with its three colours: the tool has no table of its own to fall back on.

## Files and rendering

- `port/third_party/fonts/` (release: `fonts/`): the typefaces, their licences and `fonts.tsv`. `BT3_FONTS` names
  another folder; an empty value switches both settings off.
- `gamedata/game-text/` (or `$BT3_DATA/game-text`): the labels made from the disc; ignored by Git, never packaged.
  `BT3_MENU_FONT` names another folder; an empty value switches the Menu font off (and nothing is made).
- `port/src/gs/menu_labels.c`: makes the list. It holds the catalogue (`ml_catalog`: which archive, section and
  texture a menu's labels are in, and what they say) and nothing of the disc; the stage and song names are
  `namefont.c`'s tables, and its letter cutter measures the long name strips. To add a menu, add its sheets there.
- `port/tools/menu_font.py`: an edited `labels.json` into the files the game reads. `--out` names the folder.
- `port/src/gs/menu_text.c`: the font check, the drawing and the list of fonts, for Vulkan and OpenGL. Text is
  drawn at the internal resolution, at least 2 and at most 4 times the PS2's.
- `port/src/gs/ui.cpp`: the two settings, and the names of added songs and stages.
- `src/battle/col_c.c` `Font_UploadTexture`: under `#ifdef PORT`, hands the game's glyph table to the renderer.
- `port/tests/menu_font/fuzz.c`: damaged font files against the font check, with the sanitizers.
- `port/tests/menu_font/labels.c`: the maker of the list on its own (to compare its files), and against damaged
  menu archives, with the sanitizers.

Texture-pack replacements take precedence over both settings. Neither setting touches the game's state: an online
match is the same with any font on either side. `BT3_FONT_LOG=1` prints every decoded texture and the time the
text took.
