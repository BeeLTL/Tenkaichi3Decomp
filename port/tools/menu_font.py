#!/usr/bin/env python3
"""Edited menu labels of the "Menu font" setting, put into the form the game reads. See docs/port/menu_font.md.

The game makes the list of labels itself, from the disc data, the first time it starts (port/src/gs/menu_labels.c):
gamedata/game-text/labels.tsv and name-styles.tsv, which it reads, and labels.json, the same list for a person to
edit. This tool turns an edited labels.json into labels.tsv and name-styles.tsv again:

    python3 port/tools/menu_font.py                      gamedata/game-text of this checkout
    python3 port/tools/menu_font.py --out <folder>       another game-text folder (a release's gamedata/game-text)

It reads labels.json and nothing else: not the disc, and no table of its own (which texture has which label, the
measurements and the colours are all in the file, written there by the game). Standard library only.
To start over, delete labels.json and labels.tsv: the game writes both again at its next start."""
import argparse
import json
import pathlib
import re
import struct

ROOT = pathlib.Path(__file__).resolve().parents[2]

# The first lines of the two files, as port/src/gs/menu_labels.c writes them (ml_tsv). The game takes a labels.tsv
# without this version as one of an older format and makes the list again from the disc.
LABELS_HEAD = ('# BT3 menu text v6: hash width height y row_height cap_height baseline center margin width_milli x row_width '
               'art_width top bottom outline italic outline_milli outer_milli outer_rgb shadow text')
STYLES_HEAD = '# kind (0 song, 1 selected song, 2 stage) cap_height baseline margin width_milli top bottom outline'


def font_chars(path):
    """The characters a TrueType file has glyphs for (its Unicode character map, formats 4 and 12)."""
    chars = set()
    try:
        d = path.read_bytes()
        tables = {d[12 + 16 * i:16 + 16 * i]: struct.unpack_from('>I', d, 20 + 16 * i)[0] for i in range(struct.unpack_from('>H', d, 4)[0])}
        cmap, best = tables[b'cmap'], None
        for i in range(struct.unpack_from('>H', d, cmap + 2)[0]):
            platform, encoding, offset = struct.unpack_from('>HHI', d, cmap + 4 + 8 * i)
            kind = struct.unpack_from('>H', d, cmap + offset)[0]
            if (platform == 0 or (platform == 3 and encoding in (1, 10))) and kind in (4, 12) and (best is None or kind > best[0]):
                best = (kind, cmap + offset)
        if best is None:
            return chars
        kind, at = best
        if kind == 4:
            segs = struct.unpack_from('>H', d, at + 6)[0] // 2
            ends, starts = at + 14, at + 16 + 2 * segs
            deltas, offsets = starts + 2 * segs, starts + 4 * segs
            for i in range(segs):
                end, start, delta, offset = (struct.unpack_from('>H', d, base + 2 * i)[0] for base in (ends, starts, deltas, offsets))
                for c in range(start, min(end, 0xFFFE) + 1):
                    glyph = c if offset == 0 else struct.unpack_from('>H', d, offsets + 2 * i + offset + 2 * (c - start))[0]
                    if glyph and (glyph + delta) & 0xFFFF:
                        chars.add(c)
        else:
            for i in range(min(struct.unpack_from('>I', d, at + 12)[0], 100000)):
                start, end, glyph = struct.unpack_from('>III', d, at + 16 + 12 * i)
                if end < 0x110000 and end - start < 0x20000:
                    chars.update(range(start, end + 1))
    except (OSError, KeyError, struct.error, IndexError):
        pass
    return chars


def supported_chars(fonts):
    """The characters some font of the "Menu font" list has (fonts.tsv in the fonts folder). None: no list found."""
    if not (fonts / 'fonts.tsv').is_file():
        return None
    chars = set()
    for line in (fonts / 'fonts.tsv').read_text(encoding='utf-8').splitlines():
        fields = line.split('\t')
        if line and not line.startswith('#') and len(fields) >= 2 and (len(fields) < 3 or (fields[2].strip().isdigit() and int(fields[2]) & 1)):
            chars.update(font_chars(fonts / fields[0]))
    return chars


def rgb(value, what):
    if not isinstance(value, list) or len(value) != 3 or any(type(c) != int or not 0 <= c <= 255 for c in value):
        raise ValueError(f'{what}: expected three RGB values (0-255)')
    return ''.join(f'{c:02x}' for c in value)


def apply_labels(out, fonts):
    """labels.json into the bounded TSV files read by the game's renderer (port/src/gs/menu_text.c)."""
    config = json.loads((out / 'labels.json').read_text(encoding='utf-8'))
    palettes = config.get('color_styles')
    if not isinstance(palettes, dict) or not isinstance(config.get('labels'), list):
        raise ValueError('labels.json has no "color_styles" or no "labels" (a file of an early version: delete it and labels.tsv, '
                         'and the game writes both again at its next start)')

    def palette(row):
        name = row.get('color_style')
        if name not in palettes:
            raise ValueError(f"{row['id']}: color_style {name!r} is not one of color_styles")
        colors = palettes[name]
        return colors, [rgb(colors.get(key), f'color_styles.{name}.{key}') for key in ('top', 'bottom', 'outline')]

    def decorations(colors):
        widths = []
        for key, default in (('outline_width', None), ('outer_outline_width', 0)):
            value = colors.get(key, default)
            if value is None and key == 'outline_width':
                widths.append(-1)  # the renderer's own width
            elif type(value) not in (int, float) or not 0 <= value <= 8:
                raise ValueError(f'{key}: expected a width from 0 to 8')
            else:
                widths.append(round(value * 1000))
        shadow = colors.get('shadow', True)
        if type(shadow) != bool:
            raise ValueError('shadow must be true or false')
        return [*widths, rgb(colors.get('outer_outline', [0, 0, 0]), 'outer_outline'), int(shadow)]

    supported = supported_chars(fonts)
    lines = [LABELS_HEAD]
    # The overlay uses these same styles for names of external songs and stages.
    styles = [STYLES_HEAD]
    for selected, id in ((0, 'song/0/0'), (1, 'song-highlighted/0/0'), (2, 'stage/0/0')):
        template = next((r for r in config['labels'] if r['id'] == id), None)
        if template:
            values = [selected, template['cap_height'], template['baseline'], template['margin'], round(template.get('width_scale', 1) * 1000), *palette(template)[1]]
            styles.append('\t'.join(map(str, values)))
    count = 0
    by_id = {row['id']: row for row in config['labels']}
    for row in config['labels']:
        if not re.fullmatch(r'[0-9a-fA-F]{16}', row['hash']):
            raise ValueError(f"{row['id']}: invalid texture hash")
        texture_width = row.get('texture_width', 512)
        if texture_width < 8 or texture_width > 512 or texture_width & (texture_width - 1):
            raise ValueError(f"{row['id']}: unsupported texture width")
        x = row.get('x', 0)
        row_width = row.get('width', texture_width)
        if not (0 <= x < texture_width and 1 <= row_width <= texture_width - x):
            raise ValueError(f"{row['id']}: invalid horizontal bounds")
        if not (1 <= row['texture_height'] <= 512 and 1 <= row['height'] <= 64 and
                0 <= row['y'] <= row['texture_height'] - row['height'] and 1 <= row['cap_height'] <= 64 and
                0 <= row['baseline'] <= row['height'] and 0 <= row['margin'] < row_width / 2 and row['align'] in ('left', 'center')):
            raise ValueError(f"{row['id']}: invalid label bounds or style")
        width_scale = row.get('width_scale', 1.0)
        if not isinstance(width_scale, (int, float)) or not 0.5 <= width_scale <= 2.0:
            raise ValueError(f"{row['id']}: width_scale must be between 0.5 and 2")
        art_width = row.get('art_width', 0)
        if type(art_width) != int or not 0 <= art_width <= row_width:
            raise ValueError(f"{row['id']}: invalid art_width")
        source = by_id.get(row.get('text_from'), row)
        text = source['text']
        if not row.get('enabled', True) or not source.get('enabled', True) or not text:
            continue
        if '\t' in text or '\r' in text or text.count('\n') > 1 or len(text.encode('utf-8')) > 255:
            raise ValueError(f"{row['id']}: use at most two lines and 255 UTF-8 bytes")
        if supported is not None:
            missing = sorted(set(text) - {chr(c) for c in supported} - {'\n'})
            if missing:
                print(f"{row['id']}: fonts will skip unsupported characters {''.join(missing)!r}")
        colors, fill = palette(row)
        italic = row.get('italic', False)
        if type(italic) != bool:
            raise ValueError(f"{row['id']}: italic must be true or false")
        # Only the unchanged original label has a measured artwork width.
        target_width = art_width if text == row.get('original') else 0
        parts = text.split('\n')
        if any(not part.strip() for part in parts):
            raise ValueError(f"{row['id']}: each line must contain visible text")
        for i, part in enumerate(parts):
            height = row['height'] // len(parts)
            cap = min(row['cap_height'], height - 8) if len(parts) > 1 else row['cap_height']
            baseline = (height + cap) // 2 if len(parts) > 1 else row['baseline']
            fields = [row['hash'], texture_width, row['texture_height'], row['y'] + i * height, height, cap, baseline,
                      int(row['align'] == 'center'), row['margin'], round(width_scale * 1000), x, row_width,
                      target_width if len(parts) == 1 else 0, *fill, int(italic), *decorations(colors), part]
            lines.append('\t'.join(map(str, fields)))
            count += 1
    if count > 512:
        raise ValueError('The renderer supports at most 512 label rows')
    # each file whole or not at all; labels.tsv last, as the game writes them
    (out / 'name-styles.tsv.tmp').write_text('\n'.join(styles) + '\n', encoding='utf-8')
    (out / 'name-styles.tsv.tmp').replace(out / 'name-styles.tsv')
    (out / 'labels.tsv.tmp').write_text('\n'.join(lines) + '\n', encoding='utf-8')
    (out / 'labels.tsv.tmp').replace(out / 'labels.tsv')
    print(f'{count} labels ready. Restart the game to load {out}/labels.tsv')


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--out', type=pathlib.Path, default=ROOT / 'gamedata/game-text', help='the game-text folder (labels.json is read there)')
    parser.add_argument('--fonts', type=pathlib.Path, default=None,
                        help='the fonts folder, to say which characters no font has (default: port/third_party/fonts, or fonts/ beside gamedata)')
    parser.add_argument('--apply', action='store_true', help='(what this tool does; kept so that the earlier command line works)')
    args = parser.parse_args()
    if not (args.out / 'labels.json').is_file():
        raise SystemExit(f'No menu labels: {args.out}/labels.json is not there. Start the game once: it makes the list from the disc data '
                         '(USA release) and writes labels.json beside it.')
    fonts = args.fonts
    if fonts is None:
        beside = args.out.resolve().parent.parent / 'fonts'
        fonts = beside if (beside / 'fonts.tsv').is_file() else ROOT / 'port/third_party/fonts'
    apply_labels(args.out, fonts)


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, KeyError, IndexError, TypeError) as exc:
        raise SystemExit(f'No menu labels: {type(exc).__name__}: {exc}')
