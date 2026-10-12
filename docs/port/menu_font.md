# Menu fonts

Font choices come from the `.ttf` files found recursively in `port/third_party/fonts/menu/`
and `port/third_party/fonts/dialogue/`,
using their font family/style names. The choice is saved independently of the list order.

Selected and unselected labels, including added song and stage names, use the chosen font.
Decorative headings and Story battle titles keep their original artwork. The bundled fonts are
alternatives to the original typeface.

## Setup

`install.py` discovers the fonts and prepares the label catalog from your disc automatically:

```sh
python3 install.py --iso roms/SLUS-21678.iso --rebuild
```

`--rebuild` and `--force` regenerate the assets; otherwise, existing assets are reused.

To regenerate separately, from the repository root:

```sh
python3 port/tools/menu_font.py
```

This needs extracted `gamedata/`, Pillow, fontTools and a C compiler (`cc`). New catalog entries are added;
existing text edits are kept, while catalog layout corrections are refreshed. Restart the game afterward.

## Add a font

Put a `.ttf` under `port/third_party/fonts/menu/` for Menu font, or
`port/third_party/fonts/dialogue/` for Dialogue / UI font. Subfolders work.
To offer a font in both lists, put a copy in each folder. Regenerate and restart:

```sh
python3 port/tools/menu_font.py --apply
```

The tool copies fonts into `gamedata/game-text/fonts/` and writes `fonts.tsv`. Subfolders are preserved,
so duplicate filenames work. Removed fonts disappear from options after regeneration; a missing saved choice
falls back to Original. Fonts must be static, contain `H` for sizing, and be no larger than 4 MB.

## Edit labels

1. Open `gamedata/game-text/labels.json` and find the entry by `id` or `original`.
2. Change `text`. If the entry has `text_from`, edit that referenced entry to change both selection states.
3. Apply the changes and restart the game:

   ```sh
   python3 port/tools/menu_font.py --apply
   ./play.sh
   ```

No game rebuild or archive repacking is needed. `--apply` updates the existing catalog; it does not find
new texture sheets.

| field | effect |
|---|---|
| `text` | Label text; `\n` makes two lines |
| `text_from` | Entry supplying shared text and enabled state |
| `enabled` | `false` keeps the original sprite |
| `top`, `bottom` | Fill gradient, as RGB triples; equal values give a solid fill |
| `outline` | Outline RGB color |
| `cap_height`, `baseline` | Letter height and baseline, in original texture pixels |
| `align`, `margin` | `left` or `center`, with horizontal margin |
| `width_scale` | Horizontal scale, 0.5–2.0; default 1.0 |
| `art_width` | Measured width of the original letter fill |

Leave texture identity and cell bounds unchanged: `hash`, `texture_width`, `texture_height`, `x`, `y`,
`width` and `height`. Edit the JSON, not the generated `labels.tsv` or `name-styles.tsv`.

Unchanged labels match the original artwork's letter width and intended height. Edited labels and added
names use natural font proportions, shrinking to fit. Text supports up to two lines and 255 UTF-8 bytes.
Characters absent from the selected font are skipped. Empty labels, or labels with no supported characters,
keep the original sprite. `--apply` reports characters unsupported by every catalog font.
Complex-script shaping and bidirectional text are not supported.

## Files and rendering

- `gamedata/game-text/`: generated labels, runtime styles, font files and previews; ignored by Git.
- `port/third_party/fonts/`: bundled typefaces and their licenses. The generated TTF files can also be
  installed in an operating system; colors, outlines and shadows are applied by the game.
- `port/tools/menu_font.py`: generation and label editing. `--data` and `--out` change the input/output
  folders. `--font` selects an offline export/preview font.
- `port/src/gs/menu_text.c`: vector rendering, at least 4× texture resolution, for Vulkan and OpenGL.
- `port/src/gs/ui.cpp`: F1 selection and added song/stage name overlays.

The default runtime folder is `gamedata/game-text`, or `$BT3_DATA/game-text`. `BT3_MENU_FONT` selects another
folder; an empty value disables replacements. Without a generated font catalog, options offers only Original.
Missing label assets keep the original sprites. Texture-pack replacements take precedence. The game's ordinary
`Font_Print*` text is unaffected.
