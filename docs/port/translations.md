# Translations

The port uses one selected language for three kinds of text. Each kind has its
own JSON catalog under `gamedata/game-text/translations/`:

| Folder | Text covered | Where the English text comes from |
| --- | --- | --- |
| `labels/` | Menu and other text painted into artwork textures | Bundled `port/translations/labels/en.json` |
| `ui/` | Original game's menu and dialog text tables | Extracted from the USA game data at startup into `ui/en.json`, unless an English UI catalog is bundled |
| `port/` | Text added by the PC port, currently the quit confirmation | Bundled `port/translations/port/en.json` |

An entry has a stable ID, the English `original`, and editable `text`. For
example, `port/en.json` contains:

```json
{
  "quit/confirm": {
    "original": "Close the game?\nUnsaved progress will be lost.",
    "text": "Close the game?\nUnsaved progress will be lost."
  }
}
```

Keep IDs and `original` unchanged when translating; change `text`. Use `\n`
inside a JSON string for a line break. The catalog's `locale` must match its
filename, such as `es` in `es.json`. Set `name` to the name shown in the
language selector. `order` controls its position there; smaller numbers come
first. `schema_version` identifies the JSON format. `labels_version`,
`translation_version`, and `labels_count` document the source and translation
revision and entry count; they do not select the language or alter rendering.

## Source files and game-data files

The source tree bundles JSON catalogs from `port/translations/labels/`,
`port/translations/ui/`, and `port/translations/port/`. A release puts these
under its corresponding `translations/` folders. At startup, the game
copies missing files into `gamedata/game-text/translations/`. It **never
overwrites an existing game-data translation**, so the game-data copy is the
one to edit after the first launch. Editing a bundled source file will affect
an existing installation only if its game-data copy is also updated or removed.
Packaging includes locale JSON files with valid locale filenames.

The port extracts `ui/en.json` from the installed USA game data at startup
when no English UI catalog exists. It preserves an existing game-data copy.
Developers may bundle reviewed UI catalogs in `port/translations/ui/`;
the game copies those before starting extraction. Translators can also place
UI catalogs directly in `gamedata/game-text/translations/ui/`.

To add a language, copy an English catalog to
`gamedata/game-text/translations/<kind>/<locale>.json`, set `locale` and
`name`, and translate the `text` values. A language may have files in only one
or two of the three folders. Missing menu and battle labels use the English
label catalog; missing disc UI translations leave the game's original text;
missing port entries use the English port catalog or its built-in message.
For artwork labels, select a Menu or Battle font instead of **Original** to
display translated words in place of the original artwork.

## Selecting and loading a language

The game discovers locale files in all three folders and shows each language
once in **F1 > UI > Language**. The startup selector appears only when at least
two languages are available; it is skipped when there is just one. The selected
locale is saved as `menu_lang` in `bt3_settings.txt`. The optional **Show language
selector at startup** setting displays the selector on later launches too.
`BT3_MENU_LANG=es` overrides the saved choice for one launch.

The catalogs are loaded once for a run. Restart the game after choosing a
different language or editing a JSON file. The selected language applies
across the available label, UI, and port catalogs; each kind falls back
independently when it has no matching translation.

## What each catalog can translate

**Labels:** `labels.tsv` identifies texture positions and label IDs, while
`labels/<locale>.json` supplies words. Changing words does not require
rebuilding `labels.tsv`. Layout and style changes are described in
[Menu fonts](menu_font.md).

**Game UI:** `ui/en.json` lists text found in the extracted game tables. Copy
it to `ui/<locale>.json`, keep each ID and `original`, and translate `text`.
Repeated English phrases can have separate IDs because they occur in
different tables. Only extracted tables are covered; text elsewhere in the
original game does not become translatable by adding a JSON entry.

**Port text:** An entry in `port/<locale>.json` works only after port code has
registered its ID and looks it up. To add one, define the ID in
`port/src/gs/menu_translation.h`, add its English wording to `kPortText` in
`port/src/gs/menu_translation.c`, increase `TR_PORT_TEXT_COUNT`, and add the
same ID, `original`, and English `text` to `port/translations/port/en.json`.
Update `labels_count` and increment `labels_version` when changing the source
set of IDs or wording. At the display site, call
`MenuTranslation_PortText(id)` and keep a built-in English fallback for a
missing or invalid catalog. The returned text is UTF-8; ImGui can display it
directly. Native game dialogs need conversion to UTF-16 and a buffer large
enough for translated text, as in `port/src/quit_dialog.c`.

Adding an ID to a JSON file alone cannot translate an arbitrary hardcoded
string: the code or data that displays it must use the corresponding
translation system.

## What happens when a translation does not cover every ID?

Each catalog handles missing entries differently:

| Catalog | Missing ID or untranslated entry |
| --- | --- |
| `ui/` | A matched entry with empty `text` uses the original English. A missing ID normally leaves the original text, though an identical English line elsewhere in the catalog can supply its translation when that match is unambiguous. |
| `port/` | The loader uses the English `port/en.json` entry, then the built-in English string if needed. |
| `labels/` | An entry with empty `text` uses its `original`. An ID absent from the selected label catalog has no per-ID English fallback: its artwork stays in place. Some IDs are required to load the catalog at all; if one is missing, the catalog is rejected. |



## The European disc's languages

`port/tools/extract_eu_text.py` reads a European disc image (SLES-54945) and
gives Spanish, German, French and Italian from the game's own translation:

```
python3 port/tools/extract_eu_text.py SLES-54945.iso --output gamedata/game-text/translations/ui
```

It writes two things for each language, both the player's own disc data that
stay in `gamedata` (never commit them):

- `ui/<locale>.json`: the menu and dialog text, keyed by the USA IDs. A line
  whose European English differs from the USA English cannot be matched and
  stays English (about 2,300 of 20,385).
- `art/<locale>.bin`: the menus' painted lettering (headings, menu items,
  buttons such as Yes and No, the title's Press START). It holds the texture
  files of the menu archives (448 to 472) that the language paints otherwise,
  about 120 per language, each with where it is in the USA archive.

When a language with an `art/<locale>.bin` is chosen, the game pairs each of
those textures with the USA one (`MenuLabels_Art` in `menu_labels.c`, in a
thread of its own at start, under two seconds): by the hash of the decoded USA
pixels, the same hash the label list uses. The renderer then puts the European
picture in place of the USA one when it decodes it (`MenuLabels_ArtSwap`,
`gs_draw.c`). The game itself is untouched, and so is online play: it is only
what a texture shows. The two main menu sheets that the PC's own item is
written into are paired by their first 96 rows; the item's row is kept.

A redrawn label (Menu font other than Original) is drawn from `labels/`; on a
texture that shows the European picture there is no USA label to redraw, so
the European art is what shows. The battle HUD (file 5: Ready, Fight, K.O.) is
not covered (not looked for on the European disc yet) and stays as it is.

`BT3_ART_LOG=1` lists every pair at start and every swap.
