#!/usr/bin/env python3
"""Extract European disc UI text into catalogs keyed by the USA disc's IDs, and its menu art.

Usage:
    python3 port/tools/extract_eu_text.py SLES-54945.iso --output gamedata/game-text/translations/ui

Reads the ISO and AFS archive directly, without extracting the whole disc.
Existing locale files are never replaced unless --overwrite is given.

The menus' painted lettering (headings, menu items, stage names) is in textures, painted again for each language
on the European disc. Next to the text catalogs (in ../art, or --art) goes <locale>.bin for each language: the
texture files of the menu archives that differ from the USA disc's, each with where it is in the USA archive.
The game pairs them with the USA textures and shows the European pictures in their place (menu_labels.c,
MenuLabels_Art). Like the catalogs, they are the player's own disc data: they stay in gamedata.
"""
import argparse
import json
from pathlib import Path
import struct

SECTOR = 2048
MAX_ARCHIVE = 16 * 1024 * 1024
MAX_UNPACKED = 64 * 1024 * 1024
USA_FILES = (448, 449, 450, 451, 452, 453, 459, 460, 461, 463, 472)
EU_FIRST = 451
EU_STRIDE = 31
# Order of the five repeated menu archive banks on this disc, checked against their dialog text.
LANGUAGES = (("en", "English"), ("es", "Español"), ("de", "Deutsch"),
             ("fr", "Français"), ("it", "Italiano"))


def u32(data, at=0):
    return struct.unpack_from("<I", data, at)[0]


def iso_entries(disc, extent, size):
    disc.seek(extent * SECTOR)
    data = disc.read(size)
    if len(data) != size:
        raise ValueError("truncated ISO directory")
    pos = 0
    while pos < size:
        length = data[pos]
        if not length:
            pos = (pos // SECTOR + 1) * SECTOR
            continue
        record = data[pos:pos + length]
        if len(record) < 34 or 33 + record[32] > len(record):
            raise ValueError("invalid ISO directory record")
        name = record[33:33 + record[32]].decode("ascii", "replace").split(";", 1)[0]
        yield name, u32(record, 2), u32(record, 10), bool(record[25] & 2)
        pos += length


def iso_file(disc, path):
    disc.seek(16 * SECTOR)
    pvd = disc.read(SECTOR)
    if pvd[:7] != b"\x01CD001\x01":
        raise ValueError("not an ISO 9660 disc")
    root = pvd[156:156 + pvd[156]]
    extent, size = u32(root, 2), u32(root, 10)
    parts = path.split("/")
    for index, part in enumerate(parts):
        entry = next(((block, length, folder) for name, block, length, folder
                      in iso_entries(disc, extent, size) if name.upper() == part.upper()), None)
        if entry is None:
            raise ValueError(f"ISO file not found: {path}")
        extent, size, folder = entry
        if index < len(parts) - 1 and not folder:
            raise ValueError(f"not an ISO directory: {part}")
    if folder:
        raise ValueError(f"not an ISO file: {path}")
    return extent * SECTOR, size


class Afs:
    def __init__(self, disc, base, size):
        self.disc, self.base, self.size = disc, base, size
        disc.seek(base)
        head = disc.read(8)
        if len(head) != 8 or head[:4] != b"AFS\0":
            raise ValueError("European AFS archive not found")
        count = u32(head, 4)
        if not 0 < count < 100000:
            raise ValueError("invalid AFS entry count")
        self.entries = disc.read(count * 8)
        if len(self.entries) != count * 8:
            raise ValueError("truncated AFS index")

    def read(self, index):
        if not 0 <= index < len(self.entries) // 8:
            raise ValueError(f"missing AFS entry {index}")
        offset, size = struct.unpack_from("<II", self.entries, index * 8)
        if not 12 <= size <= MAX_ARCHIVE or offset + size > self.size:
            raise ValueError(f"invalid AFS entry {index}")
        self.disc.seek(self.base + offset)
        data = self.disc.read(size)
        if len(data) != size:
            raise ValueError(f"truncated AFS entry {index}")
        return data


def sections(data):
    if len(data) < 12:
        return None
    count = u32(data)
    if not 0 < count <= 16384 or (count + 2) * 4 > len(data):
        return None
    offsets = [u32(data, i * 4) & ~3 for i in range(1, count + 2)]
    if offsets[0] < (count + 2) * 4 or offsets[-1] > len(data) or offsets != sorted(offsets):
        return None
    return [data[offsets[i]:offsets[i + 1]] for i in range(count)]


def unpack(data):
    if len(data) < 8:
        return None
    size, packed = struct.unpack_from("<II", data)
    if not 0 < size <= MAX_UNPACKED or not 0 < packed <= len(data) - 8:
        return None
    pos, end = 8, 8 + packed
    out = bytearray()
    try:
        while pos < end:
            left = list(range(256))
            right = [0] * 256
            ch = 0
            while ch < 256:
                if pos >= end:
                    return None
                count = data[pos]
                pos += 1
                if count > 127:
                    ch += count - 127
                    count = 0
                if ch == 256:
                    break
                if ch > 256 or ch + count >= 256:
                    return None
                for _ in range(count + 1):
                    if pos >= end:
                        return None
                    left[ch] = data[pos]
                    pos += 1
                    if ch != left[ch]:
                        if pos >= end:
                            return None
                        right[ch] = data[pos]
                        pos += 1
                    ch += 1
            if end - pos < 2:
                return None
            block = data[pos] * 256 + data[pos + 1]
            pos += 2
            if pos + block > end:
                return None
            for value in data[pos:pos + block]:
                stack = [value]
                while stack:
                    value = stack.pop()
                    if left[value] == value:
                        if len(out) >= size:
                            return None
                        out.append(value)
                    else:
                        if len(stack) >= 255:
                            return None
                        stack.extend((right[value], left[value]))
            pos += block
    except (IndexError, OverflowError):
        return None
    return bytes(out) if pos == end and len(out) == size else None


def text_line(data):
    if len(data) < 4 or data[:2] != b"\xff\xfe":
        return None
    end = 2
    while end + 1 < len(data) and data[end:end + 2] != b"\0\0":
        end += 2
    if end + 1 >= len(data):
        return None
    try:
        return data[2:end].decode("utf-16le")
    except UnicodeError:
        return None


def walk(data, path, output, depth=0):
    parts = sections(data)
    if parts is None or depth > 4:
        return
    lines = [text_line(part) for part in parts]
    if all(line is not None for line in lines):
        for index, line in enumerate(lines):
            output[f"{path}/{index}"] = line
        return
    for index, part in enumerate(parts, 1):
        if len(part) < 12:
            continue
        child = part if sections(part) is not None else unpack(part)
        if child is not None:
            walk(child, f"{path}/{index}", output, depth + 1)


# The menu archives with painted lettering (menu_labels.c, kMlFiles; the HUD's file 5 has no European banks).
ART_MAGIC = b"BT3ART1\n"


def is_texture_file(data):
    """A texture file of a menu archive: its texture table's first entry an 8- or 4-bit texture."""
    if len(data) < 16:
        return False
    table = u32(data, 4) * 4
    if table + 64 > len(data):
        return False
    tex0 = struct.unpack_from("<Q", data, table + 48)[0]
    return (tex0 >> 20) & 63 in (19, 20) and (tex0 >> 26) & 15 <= 9 and (tex0 >> 30) & 15 <= 9


def art_walk(usa, eu, path, out, depth=0):
    """Texture files that differ, by path of section numbers as the game's tables count them (the first is 1)."""
    if usa == eu:
        return
    if is_texture_file(usa) and is_texture_file(eu):
        out.append((path, eu))
        return
    a, b = sections(usa), sections(eu)
    if a is None or b is None or len(a) != len(b) or depth > 3:
        return
    for index, (x, y) in enumerate(zip(a, b), 1):
        art_walk(x, y, path + [index], out, depth + 1)


def extract_art(afs, usa_dir, bank):
    """The differing texture files of language bank `bank`, as (USA file, path, European bytes)."""
    out = []
    for usa_index in USA_FILES:
        usa = (usa_dir / f"{usa_index:05d}.bin").read_bytes()
        eu = afs.read(EU_FIRST + (usa_index - USA_FILES[0]) + bank * EU_STRIDE)
        a, b = sections(usa), sections(eu)
        if a is None or b is None or len(a) != len(b):
            continue
        for index, (x, y) in enumerate(zip(a, b), 1):
            x, y = unpack(x), unpack(y)
            if x is None or y is None:
                continue
            found = []
            art_walk(x, y, [], found)
            out += [(usa_index, [index] + p, data) for p, data in found]
    return out


def write_art(path, records):
    """BT3ART1: count, then per texture file: USA file, path length, four section numbers, size, bytes (to 4)."""
    with path.open("wb") as f:
        f.write(ART_MAGIC + struct.pack("<I", len(records)))
        for usa_index, at, data in records:
            f.write(struct.pack("<HH4HI", usa_index, len(at), *(at + [0, 0, 0, 0])[:4], len(data)))
            f.write(data + b"\0" * (-len(data) % 4))


def extract_bank(afs, bank):
    output = {}
    for usa_index in USA_FILES:
        eu_index = EU_FIRST + (usa_index - USA_FILES[0]) + bank * EU_STRIDE
        walk(afs.read(eu_index), f"pzs3us1/{usa_index:05d}", output)
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("iso", type=Path, help="European/Australian PS2 ISO")
    parser.add_argument("--english", type=Path, default=Path("gamedata/game-text/translations/ui/en.json"),
                        help="English catalog extracted from the USA disc")
    parser.add_argument("--output", type=Path, default=Path("port/build/eu-ui-translations"),
                        help="destination for generated JSON files")
    parser.add_argument("--overwrite", action="store_true", help="replace existing output files")
    parser.add_argument("--art", type=Path, default=None,
                        help="destination for the menu art (default: art next to the output folder)")
    parser.add_argument("--usa", type=Path, default=Path("gamedata/pzs3us1"),
                        help="the USA disc's archives, which the menu art is compared with")
    parser.add_argument("--no-art", action="store_true", help="the text catalogs only")
    args = parser.parse_args()
    art_dir = args.art if args.art is not None else args.output.parent / "art"
    english = json.loads(args.english.read_text(encoding="utf-8"))
    originals = english["labels"]
    targets = {}
    art = {}
    for bank, (locale, name) in enumerate(LANGUAGES[1:], 1):
        path = args.output / f"{locale}.json"
        if path.exists() and not args.overwrite:
            print(f"{path}: exists, skipped")
        else:
            targets[bank] = (locale, name, path)
        path = art_dir / f"{locale}.bin"
        if not args.no_art:
            if path.exists() and not args.overwrite:
                print(f"{path}: exists, skipped")
            else:
                art[bank] = path
    if art:
        with args.iso.open("rb") as disc:
            base, size = iso_file(disc, "DATA/PZS3EU1.AFS")
            afs = Afs(disc, base, size)
            art_dir.mkdir(parents=True, exist_ok=True)
            for bank, path in art.items():
                records = extract_art(afs, args.usa, bank)
                write_art(path, records)
                print(f"{path}: {len(records)} texture files")
    if not targets:
        return
    with args.iso.open("rb") as disc:
        base, size = iso_file(disc, "DATA/PZS3EU1.AFS")
        afs = Afs(disc, base, size)
        banks = {bank: extract_bank(afs, bank) for bank in (0, *targets)}
    matched = sum(k in banks[0] and banks[0][k] == v["original"]
                  for k, v in originals.items())
    print("EU English matching USA IDs:", matched, "/", len(originals))
    if matched < len(originals) * 7 // 10:
        raise ValueError("European archives do not align with this USA English catalog")
    args.output.mkdir(parents=True, exist_ok=True)
    for bank, (locale, name, path) in targets.items():
        labels = {}
        for key, entry in originals.items():
            value = banks[bank].get(key)
            if value is None or key not in banks[0] or banks[0][key] != entry["original"]:
                value = entry["original"]
            labels[key] = {"original": entry["original"], "text": value}
        catalog = {"schema_version": 1, "labels_version": english.get("labels_version", 1),
                   "translation_version": 1, "locale": locale, "name": name,
                   "source": args.iso.name, "labels": labels, "labels_count": len(labels)}
        with path.open("x" if not args.overwrite else "w", encoding="utf-8") as output:
            json.dump(catalog, output, ensure_ascii=False, indent=2)
            output.write("\n")
        changed = sum(row["text"] != row["original"] for row in labels.values())
        print(f"{path}: {changed} translated / {len(labels)} labels")


if __name__ == "__main__":
    main()
