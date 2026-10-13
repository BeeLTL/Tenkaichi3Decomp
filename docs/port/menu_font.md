# Menu fonts

Two settings in F1 > UI draw text from a TrueType font instead of the game's own lettering. Both are on "Original"
unless chosen, and with "Original" nothing of this is active.

| setting | what it replaces | what it needs |
|---|---|---|
| Menu font | the labels of the menus (pictures on the disc: "Dragon History", "1P VS COM", ...), and the names of added stages and songs | the fonts, and a list of labels made from your disc (source installation only) |
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

## The menu labels (source installation)

Which texture has which label, and how large its letters are, is measured on your own disc. `install.py` does it in
its step "Menu fonts"; by hand, from the repository root:

```sh
python3 port/tools/menu_font.py
```

It needs the unpacked `gamedata/`, Python and gcc; nothing else and nothing from the network. It writes
`gamedata/game-text/`: `labels.json` (to edit), and `labels.tsv` and `name-styles.tsv` (what the game reads). Run
again, it keeps the edits in `labels.json`, adds new entries and refreshes the measurements. About 2 seconds.

The step is an extra. If it cannot be done (no gcc, a disc other than the USA release SLUS-21678, whose menu
archives are checked by size and CRC-32 before a texture number is used), it is skipped with one line saying why,
the installation goes on, and the game offers only "Original" under Menu font. `--any-disc` leaves the check out
for a changed archive.

A release does not have this step: it ships the fonts and their licences, so "Dialogue / UI font" works there, and
"Menu font" offers only "Original". The list of labels is derived from the disc and is never part of a package; a
`gamedata/game-text/` folder made by a source installation works in a release too.

## Edit labels

1. Open `gamedata/game-text/labels.json` and find the entry by `id` or `original`.
2. Change `text`. If the entry has `text_from`, edit that referenced entry to change both selection states.
3. Apply the changes and restart the game:

   ```sh
   python3 port/tools/menu_font.py --apply
   ./play.sh
   ```

No game rebuild or archive repacking is needed. `--apply` updates the existing catalog; it does not read the disc.

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
keep the original sprite. `--apply` reports characters unsupported by every font of the Menu font list.
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
After editing styles, run `python3 port/tools/menu_font.py --apply` and restart the game.

## Files and rendering

- `port/third_party/fonts/` (release: `fonts/`): the typefaces, their licences and `fonts.tsv`. `BT3_FONTS` names
  another folder; an empty value switches both settings off.
- `gamedata/game-text/` (or `$BT3_DATA/game-text`): the labels made from the disc; ignored by Git, never packaged.
  `BT3_MENU_FONT` names another folder; an empty value switches the Menu font off.
- `port/tools/menu_font.py`, `port/tools/menu_font_extract.c`: making and editing the labels. `--data` and `--out`
  change the input and output folders.
- `port/src/gs/menu_text.c`: the font check, the drawing and the list of fonts, for Vulkan and OpenGL. Text is
  drawn at the internal resolution, at least 2 and at most 4 times the PS2's.
- `port/src/gs/ui.cpp`: the two settings, and the names of added songs and stages.
- `src/battle/col_c.c` `Font_UploadTexture`: under `#ifdef PORT`, hands the game's glyph table to the renderer.
- `port/tests/menu_font/fuzz.c`: damaged font files against the font check, with the sanitizers.

Texture-pack replacements take precedence over both settings. Neither setting touches the game's state: an online
match is the same with any font on either side. `BT3_FONT_LOG=1` prints every decoded texture and the time the
text took.
