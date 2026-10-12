#!/usr/bin/env python3
"""Prepare editable menu labels with a clean vector font. See docs/port/menu_font.md."""
import argparse
import collections
import ctypes
import functools
import json
import pathlib
import re
import shutil
import struct
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]


def u32(data, offset=0):
    return struct.unpack_from('<I', data, offset)[0]


def section(data, index):
    return data[u32(data, index * 4) & ~3:]


def unpack(data):
    """The game's byte-pair compression, src/sys/bpe.c (no game code is executed)."""
    size, packed = struct.unpack_from('<II', data)
    if size > 64 * 1024 * 1024 or packed > len(data) - 8:
        raise ValueError('Invalid BPE header')
    p, out = 8, bytearray()
    while p < packed + 8:
        left, right = list(range(256)), [0] * 256
        c = 0
        while c < 256:
            count = data[p]; p += 1
            if count > 127:
                c += count - 127
                count = 0
            if c == 256:
                break
            for _ in range(count + 1):
                left[c] = data[p]; p += 1
                if c != left[c]:
                    right[c] = data[p]; p += 1
                c += 1
        block = data[p] * 256 + data[p + 1]; p += 2
        for value in data[p:p + block]:
            stack = [value]
            while stack:
                value = stack.pop()
                if left[value] == value:
                    out.append(value)
                else:
                    stack.extend((right[value], left[value]))
                if len(out) > size or len(stack) > 256:
                    raise ValueError('Invalid BPE block')
        p += block
    if p != packed + 8 or len(out) != size:
        raise ValueError('BPE size mismatch')
    return bytes(out)


# The same GS layout tables as namefont.c. Map each decoded texel to its uploaded byte once.
BLOCK = ((0,1,4,5,16,17,20,21),(2,3,6,7,18,19,22,23),(8,9,12,13,24,25,28,29),(10,11,14,15,26,27,30,31))
COL = ((0,1,4,5,8,9,12,13),(2,3,6,7,10,11,14,15),(16,17,20,21,24,25,28,29),(18,19,22,23,26,27,30,31),
       (32,33,36,37,40,41,44,45),(34,35,38,39,42,43,46,47),(48,49,52,53,56,57,60,61),(50,51,54,55,58,59,62,63))
COL8 = ((0,4,16,20,32,36,48,52,2,6,18,22,34,38,50,54),(8,12,24,28,40,44,56,60,10,14,26,30,42,46,58,62),
        (33,37,49,53,1,5,17,21,35,39,51,55,3,7,19,23),(41,45,57,61,9,13,25,29,43,47,59,63,11,15,27,31))
_MAPPING = {}


BLOCK16 = ((0,2,8,10),(1,3,9,11),(4,6,12,14),(5,7,13,15),(16,18,24,26),(17,19,25,27),(20,22,28,30),(21,23,29,31))
COL4 = ((0,8,32,40,64,72,96,104,2,10,34,42,66,74,98,106,4,12,36,44,68,76,100,108,6,14,38,46,70,78,102,110),
        (16,24,48,56,80,88,112,120,18,26,50,58,82,90,114,122,20,28,52,60,84,92,116,124,22,30,54,62,86,94,118,126),
        (65,73,97,105,1,9,33,41,67,75,99,107,3,11,35,43,69,77,101,109,5,13,37,45,71,79,103,111,7,15,39,47),
        (81,89,113,121,17,25,49,57,83,91,115,123,19,27,51,59,85,93,117,125,21,29,53,61,87,95,119,127,23,31,55,63))


def texture(data, index):
    """Decode indexed menu textures using their actual GS upload format and pitch."""
    e = data[u32(data,4)*4+index*64:][:64]
    if len(e)<64:
        return None
    tex0 = struct.unpack_from('<Q',e,48)[0]
    w,h = 1<<((tex0>>26)&15),1<<((tex0>>30)&15)
    psm = (tex0>>20)&63
    if w>512 or h>512 or psm not in (19,20) or u32(e,24)>>8 != 0:
        return None
    pixels,palette = u32(e)*4+96,u32(e,4)*4+96
    reg = struct.unpack_from('<Q',data,pixels-48)[0]
    uw,uh = reg&4095,(reg>>32)&4095
    dbw,tbw = u32(e,24)&63,(tex0>>14)&63
    key=(w,h,psm,uw,uh,dbw,tbw)
    if key not in _MAPPING:
        mem=[None]*(512*512*4)
        for y in range(uh):
            for x in range(uw):
                a=(((y&~31)*dbw+((x>>1)&~31)+BLOCK[(y>>3)&3][(x>>3)&7])<<6)+COL[y&7][x&7]
                at=(y*uw+x)*4
                mem[a*4:a*4+4]=range(at,at+4)
        mapping=[]
        for y in range(h):
            for x in range(w):
                col=(y&15)>>2;sx=x^(4 if col&1 else 0)
                if psm==19:
                    a=((((y>>1)&~31)*(tbw>>1)+((x>>2)&~31)+BLOCK[(y>>4)&3][(x>>4)&7])<<8)+COL8[y&3][sx&15]+col*64
                    address,shift=a,0
                else:
                    a=((((y>>2)&~31)*(tbw>>1)+((x>>2)&~31)+BLOCK16[(y>>4)&7][(x>>5)&3])<<9)+COL4[y&3][sx&31]+col*128
                    address,shift=a>>1,(a&1)*4
                if mem[address] is None:
                    return None  # dimensions outside the uploaded rectangle
                mapping.append((mem[address],shift))
        _MAPPING[key]=mapping
    count=256 if psm==19 else 16
    colors=[]
    for i in range(count):
        c=(i&0xE7)|((i&8)<<1)|((i&16)>>1) if psm==19 else i
        colors.append(data[palette+c*4:palette+c*4+4])
    mask=count-1
    return b''.join(colors[(data[pixels+a]>>shift)&mask] for a,shift in _MAPPING[key])


def sheets(pack, part, width=512):
    data = section(pack, part)
    result = []
    for i in range(u32(data)):
        entry = u32(data,4)*4 + i*64
        tex0 = struct.unpack_from('<Q',data,entry+48)[0]
        if 1 << ((tex0 >> 26) & 15) == width:
            rgba = texture(data,i)
            if rgba is not None:
                result.append(rgba)
    return result


def image(rgba, width=512):
    from PIL import Image
    im = Image.frombytes('RGBA', (width, len(rgba)//(width*4)), rgba)
    im.putalpha(im.getchannel('A').point(lambda a: min(255, a*2)))
    return im



def catalog(data):
    """Named texture groups, with the archive/section addresses used by the menu source."""
    @functools.lru_cache(maxsize=None)
    def read(n):
        return (data / 'pzs3us1' / f'{n:05d}.bin').read_bytes()
    @functools.lru_cache(maxsize=None)
    def screen(file, outer):
        return unpack(section(read(file), outer))
    names = (ROOT / 'port/src/gs/namefont.c').read_text()
    def table(name):
        return re.findall(r'"([^"\\]*)"', names.split('static const char *const ' + name + '[] = {', 1)[1].split('};', 1)[0])
    select = unpack(section(read(448), 2))
    main = unpack(section(read(449), 2))
    groups = [dict(id='stage', height=64, labels=table('kStageNames'), rgba=sheets(select, 6)),
              dict(id='song', height=32, labels=table('kSongNames'), rgba=sheets(select, 4)[:3]),
              dict(id='song-highlighted', height=32, labels=table('kSongNames'), rgba=sheets(select, 3)[:3])]
    # Main-menu selectable labels, normal and selected. Their Flash clips use these same UV rows.
    main_labels = ['Dragon History','Ultimate Battle','Dragon World Tour','Duel','Dragon Net Battle','Evolution Z',
                   'Ultimate Training','Data Center','Character Reference','Options','Shenron']
    for part in (5,6):
        groups.append(dict(id=f'main-{part}', height=32, labels=main_labels, rgba=sheets(main, part)))
    # Explicit texture indices: panels/backgrounds often share the same section as text.
    def add(id, file, outer, part, index, labels, height=32, centered=False, text_from=None, width=512, nested=None):
        pack = screen(file, outer)
        if nested is not None:
            pack = section(pack, nested)
        section_data = section(pack, part)
        rgba = texture(section_data, index)
        entry = u32(section_data,4)*4 + index*64
        actual_width = 1 << ((struct.unpack_from('<Q',section_data,entry+48)[0] >> 26) & 15)
        if rgba is None or actual_width != width:
            raise ValueError(f'{id}: unexpected menu text sheet dimensions')
        groups.append(dict(id=id, height=height, labels=labels, rgba=[rgba], centered=centered, text_from=text_from, width=width))

    add('title-normal',449,1,8,0,['New Game','Continue'],centered=True)
    add('title-selected',449,1,8,1,['New Game','Continue'],centered=True,text_from='title-normal')
    versus = ['1P VS COM','1P VS 2P','COM VS COM','Battle Settings']
    battle = ['Single Battle','Team Battle','DP Battle']
    for index in range(4):
        family = 'versus' if index < 2 else 'versus-type'
        style = 'normal' if index % 2 == 0 else 'selected'
        add(f'{family}-{style}',448,1,8,index,versus if index < 2 else battle,
            text_from=f'{family}-normal' if index % 2 else None)
    options = ['Save/Load','Screen','Sound','Controller','Camera Type','Screen Shake','Screen Adjust','Speaker']
    sound = ['Volume','BGM Test','Change Voice','Control Type','Vibration']
    for index in range(4):
        family = 'options' if index % 2 == 0 else 'options-sound'
        style = 'normal' if index < 2 else 'selected'
        add(f'{family}-{style}',453,1,16,index,options if index % 2 == 0 else sound,
            text_from=f'{family}-normal' if index >= 2 else None)
    for index, style in ((0,'normal'),(1,'selected')):
        add(f'options-actions-{style}',453,1,27,index,['EXIT','Default'],width=256,
            text_from='options-actions-normal' if index else None)
        add(f'save-confirm-{style}',453,1,3,index,['Yes','No'],width=128,centered=True,nested=35,
            text_from='save-confirm-normal' if index else None)
    values = ['ON','OFF','Save','Load','Stereo','Mono','English','Japanese']
    controls = ['1P','2P','Normal','Custom','Simple']
    for index in range(4):
        family = 'options-values' if index % 2 == 0 else 'options-controls'
        style = 'normal' if index < 2 else 'selected'
        add(f'{family}-{style}',453,1,14,index,values if index % 2 == 0 else controls,
            centered=True,width=256,text_from=f'{family}-normal' if index >= 2 else None)
    for index, style in ((2,'normal'),(3,'selected')):
        add(f'history-difficulty-{style}',460,1,9,index,['Easy','Normal','Hard'],centered=True,
            text_from='history-difficulty-normal' if index == 3 else None)
    for index, style in ((4,'normal'),(5,'selected')):
        add(f'history-action-{style}',460,1,9,index,['Select Scenario','Select Difficulty'],centered=False,
            text_from='history-action-normal' if index == 5 else None)
    add('ultimate-battle',451,1,3,7,['Sim Dragon','Mission 100','Survival','Disc Fusion','???'],centered=True)
    add('sim-dragon-actions',451,5,2,19,['Start','Battle Record','Explanation'])
    for index, style in ((0,'normal'),(1,'selected')):
        add(f'evolution-{style}',452,1,5,index,['Character Customize','Item Shop','Evolution Z Explanation'],
            text_from='evolution-normal' if index else None)
        add(f'item-shop-{style}',452,2,3,index,['Item Exchange','Item List'],
            text_from='item-shop-normal' if index else None)
        add(f'data-center-{style}',459,1,8,index,['Password Input Screen','Replay','Fighter Catalog'],
            text_from='data-center-normal' if index else None)
    for index, style in ((0,'normal'),(4,'selected')):
        add(f'training-{style}',461,1,9,index,['Training','Battle Training'],
            text_from='training-normal' if index == 4 else None)
    for index, style in ((1,'normal'),(5,'selected')):
        add(f'practice-{style}',461,1,9,index,['Single Practice','Team Practice'],
            text_from='practice-normal' if index == 5 else None)
    for index, style in ((3,'normal'),(7,'selected')):
        add(f'training-controller-{style}',461,1,9,index,['Nunchuk Style','Classic Style','GC Controller'],
            text_from='training-controller-normal' if index == 7 else None)
    add('ultimate-battle-normal',451,1,3,6,['Sim Dragon','Mission 100','Survival','Disc Fusion','???'],
        centered=True,text_from='ultimate-battle')
    add('sim-dragon-actions-normal',451,5,2,18,['Start','Battle Record','Explanation'],text_from='sim-dragon-actions')
    tour=['World Tour','Free','World Tournament','World Martial Arts Big Tournament','Cell Games','Otherworld Tournament','Yamcha Game']
    for index,style in ((0,'normal'),(1,'selected')):
        add(f'world-tour-list-{style}',450,1,5,index,tour,text_from='world-tour-list-normal' if index else None)
        add(f'world-tour-custom-{style}',450,2,13,index,['Normal','Custom 1','Custom 2','Custom 3'],width=256,centered=True,
            text_from='world-tour-custom-normal' if index else None)
        add(f'versus-settings-{style}',448,1,13,index,['Duel Time','COM Level','Referee','Transformations','Map Setting','Default'],
            width=256,centered=False,text_from='versus-settings-normal' if index else None)
        add(f'versus-dp-{style}',448,1,12,index+1,['10','15','20'],width=256,centered=True,
            text_from='versus-dp-normal' if index else None)
        # The narrow "10" has more fill than outline; use this sheet's outline palette color.
        groups[-1]['outline'] = (44,21,27) if index else (70,0,89)
        add(f'versus-onoff-{style}',448,1,17,index,['ON','OFF'],width=256,centered=True,
            text_from='versus-onoff-normal' if index else None)
        for family,part,labels in (
                ('versus-time',15,['60','90','180','240','∞']),
                ('versus-com-level',16,['Very Weak','Weak','Average','Strong','Very Strong']),
                ('versus-referee',18,['Ox King','Videl','Supreme Kai™','Shenron','Announcer 1','Announcer 2','Announcer 3'])):
            add(f'{family}-{style}',448,1,part,index,labels,width=256,centered=True,
                text_from=f'{family}-normal' if index else None)
            # The infinity symbol's fill also outnumbers its outline pixels.
            groups[-1]['outline'] = (44,21,27) if index else (70,0,89)
        for player in (1,2):
            family=f'versus-transform-{player}p'
            add(f'{family}-{style}',448,1,17,player*2+index,[f'{player}P:ON',f'{player}P:OFF'],width=128,centered=True,
                text_from=f'{family}-normal' if index else None)
        add(f'item-shop-confirm-{style}',452,2,28,index,['Yes','No','Explanation'],width=256,centered=True,
            text_from='item-shop-confirm-normal' if index else None)
    for index,style in ((2,'normal'),(6,'selected')):
        add(f'battle-training-{style}',461,1,9,index,['Basic','Intermediate','Advanced'],width=256,
            text_from='battle-training-normal' if index==6 else None)
    for index,style in ((0,'normal'),(1,'selected')):
        add(f'team-end-{style}',448,2,36,index,['END'],width=256,centered=True,
            text_from='team-end-normal' if index else None)
        add(f'sound-digits-{style}',453,1,11,index,[],width=128)
        groups[-1]['rectangles']=[dict(text=str(n),x=(n%4)*32,y=(n//4)*32,width=32,height=32) for n in range(10)]
        add(f'character-color-{style}',448,2,29,index,[],width=128)
        groups[-1]['rectangles']=[dict(text=str(n+1),x=(n%2)*64,y=(n//2)*32,width=64,height=32) for n in range(4)]
        groups[-1]['outline'] = (44,21,27) if index else (70,0,89)
    add('song-random',448,2,4,3,['Random'])
    add('song-random-highlighted',448,2,3,3,['Random'],text_from='song-random')
    return groups


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--data', type=pathlib.Path, default=ROOT/'gamedata')
    parser.add_argument('--out', type=pathlib.Path, default=ROOT/'gamedata/game-text')
    parser.add_argument('--font', type=pathlib.Path, help='Select a static TTF/OTF for offline export and preview')
    parser.add_argument('--extract-only', action='store_true')
    parser.add_argument('--apply', action='store_true', help='Compile edited labels.json for the game; no font extraction')
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    default_font=prepare_bundled_fonts(args.out)
    if args.font and (args.out/'labels.json').exists():
        install_font(args.font, args.out)
        apply_labels(args.out)
        font_preview(args.out)
        return
    if args.apply:
        apply_labels(args.out)
        return
    groups = catalog(args.data)
    for group in groups:
        for i, rgba in enumerate(group['rgba']):
            image(rgba,group.get('width',512)).save(args.out / f"{group['id']}-{i}.png")
        print(group['id'], len(group['rgba']), 'sheets')
    if not args.extract_only:
        export(args, groups, default_font)




def glyphs_from(groups, out):
    """Extract the existing segmenter's letter masks; no font artwork is checked into the repository."""
    result = {}
    with tempfile.TemporaryDirectory() as td:
        so = pathlib.Path(td)/'extract.so'
        subprocess.run(['cc','-shared','-fPIC','-O2', str(ROOT/'port/tools/menu_font_extract.c'), '-lm','-o',str(so)], check=True)
        lib = ctypes.CDLL(str(so))
        lib.MenuFont_Extract.argtypes = [ctypes.c_int, ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_void_p, ctypes.c_int]
        lib.MenuFont_Glyph.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_void_p, ctypes.c_void_p]
        lib.MenuFont_Glyph.restype = ctypes.c_void_p
        for group in groups:
            if not group['labels'] or not group['rgba']:
                continue
            sample_labels = []
            sample_rows = []
            slots = [(sheet, y) for sheet in group['rgba'] for y in range(0,len(sheet)//2048,group['height'])]
            for (sheet,y), label in zip(slots,group['labels']):
                if label and '\n' not in label:
                    at = y * 2048
                    sample_rows.append(sheet[at:at + group['height'] * 2048])
                    sample_labels.append(label)
            # The segmenter operates on 256-high sheets; pad the final, shorter one transparently.
            rgba = b''.join(sample_rows)
            rgba = rgba.ljust((len(rgba) + 512*256*4 - 1) // (512*256*4) * (512*256*4), b'\0')
            buf = ctypes.create_string_buffer(rgba)
            labels = (ctypes.c_char_p * len(sample_labels))(*(t.encode('ascii') for t in sample_labels))
            if not lib.MenuFont_Extract(0, buf, len(rgba)//(512*256*4), group['height'], labels, len(labels)):
                print('Could not segment', group['id'], '(other groups may supply its letters)')
                continue
            found = {}
            for ch in range(33,127):
                m, sp = (ctypes.c_int*7)(), (ctypes.c_float*3)()
                ptr = lib.MenuFont_Glyph(0,ch,m,sp)
                if ptr:
                    rgba = ctypes.string_at(ptr, m[0]*m[1]*4)
                    found[chr(ch)] = dict(metrics=list(m), spacing=list(sp), mask=rgba[3::4])
            capitals = [g['metrics'] for ch,g in found.items() if ch in 'HETILMN']
            if not capitals:
                continue
            baseline = sorted(m[5]+1 for m in capitals)[len(capitals)//2]
            cap = sorted(m[5]-m[4]+1 for m in capitals)[len(capitals)//2]
            group['font_cap'], group['font_baseline'] = cap, baseline
            for ch,g in found.items():
                # Use the largest source when a letter occurs in several sheets.
                if ch not in result or cap > result[ch]['cap']:
                    result[ch] = dict(g, baseline=baseline, cap=cap, source=group['id'])
    if len(result) < 20:
        raise ValueError('Not enough letters recovered; check that the extracted data is the USA PS2 edition')
    return result


def contours(mask, w, h):
    """Trace pixel boundaries with holes intact. Collinear edges collapse to one segment."""
    solid = {(x,y) for y in range(h) for x in range(w) if mask[y*w+x] >= 128}
    edges = set()
    for x,y in solid:
        for a,b in [((x,y),(x+1,y)),((x+1,y),(x+1,y+1)),((x+1,y+1),(x,y+1)),((x,y+1),(x,y))]:
            if (b,a) in edges:
                edges.remove((b,a))
            else:
                edges.add((a,b))
    paths = []
    starts = collections.defaultdict(set)
    for a,b in edges:
        starts[a].add(b)
    while starts:
        first = min(starts)
        path, a = [first], first
        while True:
            candidates = starts[a]
            # At diagonally touching pixels, keep the filled region on the right.
            if len(candidates) > 1 and len(path) > 1:
                dx,dy = a[0]-path[-2][0], a[1]-path[-2][1]
                b = max(candidates, key=lambda p: dx*(p[1]-a[1])-dy*(p[0]-a[0]))
            else:
                b = min(candidates)
            candidates.remove(b)
            if not candidates:
                del starts[a]
            if b == first:
                break
            path.append(b); a=b
        simple=[]
        for i,p in enumerate(path):
            before, after = path[i-1], path[(i+1)%len(path)]
            if (p[0]-before[0])*(after[1]-p[1]) != (p[1]-before[1])*(after[0]-p[0]):
                simple.append(p)
        if len(simple) >= 3:
            paths.append(simple)
    return paths


def simplify_path(points, tolerance=0.65):
    # Simplify the staircase caused by thresholding the small bitmap. Keep the outline's topology.
    def simplify(line):
        if len(line) < 3:
            return line
        ax,ay=line[0]; bx,by=line[-1]; dx,dy=bx-ax,by-ay
        length=(dx*dx+dy*dy)**0.5
        distances=[abs(dy*(x-ax)-dx*(y-ay))/length if length else ((x-ax)**2+(y-ay)**2)**0.5 for x,y in line[1:-1]]
        maximum=max(distances)
        if maximum <= tolerance:
            return [line[0],line[-1]]
        at=distances.index(maximum)+1
        return simplify(line[:at+1])[:-1]+simplify(line[at:])
    first=points[0]
    split=max(range(1,len(points)),key=lambda i:(points[i][0]-first[0])**2+(points[i][1]-first[1])**2)
    result=simplify(points[:split+1])[:-1]+simplify(points[split:]+[first])[:-1]
    return result if len(result)>=3 else points


def build_font(glyphs, path):
    from fontTools.fontBuilder import FontBuilder
    from fontTools.pens.ttGlyphPen import TTGlyphPen
    fb = FontBuilder(1000, isTTF=True)
    order = ['.notdef','space'] + [f'uni{ord(ch):04X}' for ch in sorted(glyphs)]
    fb.setupGlyphOrder(order)
    fb.setupCharacterMap({32:'space', **{ord(ch):f'uni{ord(ch):04X}' for ch in glyphs}})
    shapes, metrics = {}, {}
    pen = TTGlyphPen(None)
    pen.moveTo((50,0)); pen.lineTo((50,700)); pen.lineTo((450,700)); pen.lineTo((450,0)); pen.closePath()
    pen.moveTo((100,50)); pen.lineTo((400,50)); pen.lineTo((400,650)); pen.lineTo((100,650)); pen.closePath()
    shapes['.notdef'], metrics['.notdef'] = pen.glyph(), (500,50)
    shapes['space'], metrics['space'] = TTGlyphPen(None).glyph(), (220,0)
    for ch,g in sorted(glyphs.items()):
        w,h,ox,oy,_,_,sw = g['metrics']; lb,rb,_ = g['spacing']
        scale = 700/g['cap']
        pen = TTGlyphPen(None)
        xs=[]
        for contour in contours(g['mask'],w,h):
            points = [(round((x+ox+lb)*scale),round((g['baseline']-oy-y)*scale)) for x,y in simplify_path(contour)]
            xs.extend(p[0] for p in points)
            pen.moveTo(points[0])
            for point in points[1:]:pen.lineTo(point)
            pen.closePath()
        name = f'uni{ord(ch):04X}'
        shapes[name] = pen.glyph()
        metrics[name] = (max(1,round((lb+sw+rb)*scale)),min(xs,default=0))
    fb.setupGlyf(shapes)
    fb.setupHorizontalMetrics(metrics)
    fb.setupHorizontalHeader(ascent=900, descent=-250)
    fb.setupNameTable({'familyName':'BT3 Menu Reconstructed','styleName':'Regular','uniqueFontIdentifier':'BT3MenuReconstructed-1',
                      'fullName':'BT3 Menu Reconstructed','psName':'BT3MenuReconstructed-Regular','version':'Version 1.000',
                      'description':'Reconstructed from the user-provided USA PS2 menu bitmap artwork. Limited character coverage; pixel contours, not the original typeface.'})
    fb.setupOS2(sTypoAscender=900, sTypoDescender=-250, usWinAscent=1000, usWinDescent=350, sCapHeight=700, sxHeight=520)
    fb.setupPost()
    fb.setupMaxp()
    fb.save(path)


def hash_rgba(rgba):
    value=14695981039346656037
    for b in rgba:
        value=((value^b)*1099511628211)&0xffffffffffffffff
    return f'{value:016x}'


def row_style(rgba, height, centered, width=512, outline=None):
    pixels=list(zip(rgba[0::4],rgba[1::4],rgba[2::4],rgba[3::4]))
    colors=collections.Counter(p[:3] for p in pixels if p[3]>=120)
    if not colors:
        return None
    outline=outline if outline is not None else colors.most_common(1)[0][0]
    fill=[(i%width,i//width,p[:3]) for i,p in enumerate(pixels) if p[3]>=100 and sum(p[:3])-sum(outline)>100]
    if not fill:
        return None
    ys=collections.Counter(y for x,y,c in fill)
    full=[y for y,n in ys.items() if n >= max(ys.values())*0.35]
    # Sparse capital strokes still define the cap top; dense rows locate the baseline.
    y0,y1=min(y for x,y,c in fill),max(full)
    def color(y):
        return max((c for x,yy,c in fill if yy==y),key=sum)
    return dict(height=height, cap_height=y1-y0+1, baseline=y1+1, align='center' if centered else 'left',
                margin=8 if centered else min(x for x,y,c in fill), art_width=max(x for x,y,c in fill)-min(x for x,y,c in fill)+1, top=list(color(y0)), bottom=list(color(y1)), outline=list(outline))


def apply_labels(out):
    """Turn user-editable JSON into the bounded TSV read by the native renderer."""
    config=json.loads((out/'labels.json').read_text())
    supported={ord(c) for c in json.loads((out/'coverage.json').read_text())['characters']}
    from fontTools.ttLib import TTFont
    for line in (out/'fonts.tsv').read_text(encoding='utf-8').splitlines():
        if line and not line.startswith('#'):
            with TTFont(out/'fonts'/line.split('\t',1)[0]) as font:
                supported.update(font.getBestCmap() or {})
    lines=['# BT3 menu text v4: hash width height y row_height cap_height baseline center margin width_milli x row_width art_width top bottom outline text']
    # The overlay uses these same styles for names of external songs and stages.
    styles=['# kind (0 song, 1 selected song, 2 stage) cap_height baseline margin width_milli top bottom outline']
    for selected,id in ((0,'song/0/0'),(1,'song-highlighted/0/0'),(2,'stage/0/0')):
        template=next((r for r in config['labels'] if r['id']==id),None)
        if template:
            colors=[''.join(f'{c:02x}' for c in template[key]) for key in ('top','bottom','outline')]
            values=[selected,template['cap_height'],template['baseline'],template['margin'],round(template.get('width_scale',1)*1000),*colors]
            styles.append('\t'.join(map(str,values)))
    count=0
    by_id={row['id']:row for row in config['labels']}
    for row in config['labels']:
        if row['id'].startswith('submenu-'):
            continue  # story battle-title artwork has a different typeface
        if not re.fullmatch(r'[0-9a-fA-F]{16}', row['hash']):
            raise ValueError(f"{row['id']}: invalid texture hash")
        texture_width=row.get('texture_width',512)
        if texture_width < 8 or texture_width > 512 or texture_width & (texture_width-1):
            raise ValueError(f"{row['id']}: unsupported texture width")
        x=row.get('x',0);row_width=row.get('width',texture_width)
        if not (0 <= x < texture_width and 1 <= row_width <= texture_width-x):
            raise ValueError(f"{row['id']}: invalid horizontal bounds")
        if not (1 <= row['texture_height'] <= 512 and 1 <= row['height'] <= 64 and
                0 <= row['y'] <= row['texture_height'] - row['height'] and 1 <= row['cap_height'] <= 64 and
                0 <= row['baseline'] <= row['height'] and 0 <= row['margin'] < row_width/2 and row['align'] in ('left','center')):
            raise ValueError(f"{row['id']}: invalid label bounds or style")
        width_scale = row.get('width_scale', 1.0)
        if not isinstance(width_scale, (int,float)) or not 0.5 <= width_scale <= 2.0:
            raise ValueError(f"{row['id']}: width_scale must be between 0.5 and 2")
        art_width = row.get('art_width', 0)
        if type(art_width) != int or not 0 <= art_width <= row_width:
            raise ValueError(f"{row['id']}: invalid art_width")
        source=by_id.get(row.get('text_from'),row)
        text=source['text']
        if not row.get('enabled',True) or not source.get('enabled',True) or not text:
            continue
        if '\t' in text or '\r' in text or text.count('\n') > 1 or len(text.encode('utf-8'))>255:
            raise ValueError(f"{row['id']}: use at most two lines and 255 UTF-8 bytes")
        missing=sorted(set(text)-{chr(c) for c in supported}-{'\n'})
        if missing:
            print(f"{row['id']}: fonts will skip unsupported characters {''.join(missing)!r}")
        def rgb(name):
            v=row[name]
            if len(v)!=3 or any(type(c)!=int or not 0<=c<=255 for c in v):raise ValueError(name)
            return ''.join(f'{c:02x}' for c in v)
        # Only the unchanged original label has a measured artwork width.
        target_width = art_width if text == row.get('original') else 0
        parts = text.split('\n')
        if any(not part.strip() for part in parts):
            raise ValueError(f"{row['id']}: each line must contain visible text")
        for i, part in enumerate(parts):
            height = row['height'] // len(parts)
            cap = min(row['cap_height'], height - 8) if len(parts) > 1 else row['cap_height']
            baseline = (height + cap) // 2 if len(parts) > 1 else row['baseline']
            fields=[row['hash'],texture_width,row['texture_height'],row['y'] + i * height,height,cap,baseline,
                    int(row['align']=='center'),row['margin'],round(width_scale*1000),x,row_width,target_width if len(parts) == 1 else 0,rgb('top'),rgb('bottom'),rgb('outline'),part]
            lines.append('\t'.join(map(str,fields)));count+=1
    if count > 512:
        raise ValueError('The renderer supports at most 512 label rows')
    (out/'labels.tsv.tmp').write_text('\n'.join(lines)+'\n',encoding='utf-8')
    (out/'labels.tsv.tmp').replace(out/'labels.tsv')
    (out/'name-styles.tsv.tmp').write_text('\n'.join(styles)+'\n')
    (out/'name-styles.tsv.tmp').replace(out/'name-styles.tsv')
    print(f'{count} labels ready. Restart the game to load {out}/labels.tsv')


def export(args,groups,default_font):
    # The legacy strips benefit from the spacing solver; standalone submenu sheets
    # use row_style's direct cap/baseline measurements (including single-color labels).
    glyphs_from([g for g in groups if 'centered' not in g],args.out)
    rows=[]
    for group in groups:
        if 'rectangles' in group:
            rgba=group['rgba'][0];width=group['width'];stride=width*4
            for n,rect in enumerate(group['rectangles']):
                x,y,rw,rh=rect['x'],rect['y'],rect['width'],rect['height']
                pixels=b''.join(rgba[(yy*width+x)*4:(yy*width+x+rw)*4] for yy in range(y,y+rh))
                style=row_style(pixels,rh,True,rw,group.get('outline'))
                if style:
                    style['margin']=0
                    rows.append(dict(id=f"{group['id']}/0/{n}",hash=hash_rgba(rgba),texture_width=width,texture_height=len(rgba)//stride,
                                     x=x,y=y,width=rw,original=rect['text'],text=rect['text'],enabled=True,**style))
            continue
        label_idx=0
        width=group.get('width',512)
        stride=width*4
        for i,rgba in enumerate(group['rgba']):
            texh=len(rgba)//stride
            for y in range(0,texh,group['height']):
                original=group['labels'][label_idx] if label_idx<len(group['labels']) else ''
                label_idx+=1
                if not original:
                    continue
                style=row_style(rgba[y*stride:(y+group['height'])*stride],group['height'],group.get('centered',group['id']=='stage' or group['id'].startswith('submenu')),width,group.get('outline'))
                if style:
                    if 'font_cap' in group:
                        style['cap_height'], style['baseline'] = group['font_cap'], group['font_baseline']
                    row=dict(id=f"{group['id']}/{i}/{y//group['height']}",hash=hash_rgba(rgba),texture_height=texh,y=y,
                             original=original,text=original,enabled=True,**style)
                    if width != 512:
                        row['texture_width']=width
                    source = group.get('text_from')
                    if group['id'] == 'main-5':
                        source = 'main-6'
                    if source:
                        row['text_from'] = f"{source}/{i}/{y//group['height']}"
                    rows.append(row)
    for row in rows:
        if row['id'].startswith('song-random'):
            source='song-highlighted/0/0' if 'highlighted' in row['id'] else 'song/0/0'
            template=next((r for r in rows if r['id']==source),None)
            if template:
                for key in ('cap_height','baseline','margin'):
                    row[key]=template[key]
    audio_families=('song','song-highlighted','song-random','song-random-highlighted',
                    'options-sound-normal','options-sound-selected')
    for row in rows:
        if row['id'].split('/')[0] in audio_families:
            row.update(align='left',margin=4)
    # These lists share one font size; measuring each word separately can mistake
    # its lowercase x-height for its cap height. Normalize each list across states.
    uniform_families=('history-action','ultimate-battle','sim-dragon-actions','world-tour-list',
                      'world-tour-custom','versus-settings','versus-dp','versus-onoff','versus-time','versus-com-level',
                      'versus-referee','versus-transform','save-confirm','item-shop-confirm','battle-training','sound-digits','character-color')
    for family in uniform_families:
        entries=[r for r in rows if r['id'].split('/')[0] == family or r['id'].startswith(family+'-')]
        if entries:
            cap=max(r['cap_height'] for r in entries);baseline=max(r['baseline'] for r in entries)
            for row in entries:
                row.update(cap_height=cap,baseline=baseline)
    for row in rows:
        if row['id'].startswith('versus-settings-') and row['y']==160:
            row.update(align='center',margin=8)  # Default is centered; the five settings are left-aligned.
    actions=[r for r in rows if r['id'].startswith('history-action-')]
    if actions:
        cap=max(r['cap_height'] for r in actions)
        margin=min(r['margin'] for r in actions)
        for row in actions:
            row.update(cap_height=cap,align='left',margin=margin)
    for family in ('sound-digits-normal','sound-digits-selected'):
        digits=[r for r in rows if r['id'].startswith(family+'/')]
        if digits:
            cap=max(r['cap_height'] for r in digits);baseline=max(r['baseline'] for r in digits)
            for row in digits:
                row.update(cap_height=cap,baseline=baseline)
    path=args.out/'labels.json'
    if not path.exists():
        path.write_text(json.dumps(dict(font='BT3MenuReconstructed.ttf',labels=rows),indent=2)+'\n')
    else:
        config=json.loads(path.read_text())
        config['labels']=[r for r in config['labels'] if not r['id'].startswith('submenu-')]
        known={row['id'] for row in config['labels']}
        fresh={row['id']:row for row in rows}
        for row in config['labels']:
            if row['id'] in fresh:
                for key in ('hash','texture_height','y','art_width'):
                    row[key]=fresh[row['id']][key]
                family=row['id'].split('/')[0]
                if any(family==name or family.startswith(name+'-') for name in uniform_families):
                    for key in ('align','margin','cap_height','baseline'):
                        row[key]=fresh[row['id']][key]
                if family in audio_families:
                    row.update(align='left',margin=4)
        additions=[row for row in rows if row['id'] not in known]
        config['labels'].extend(additions)
        path.write_text(json.dumps(config,indent=2,ensure_ascii=False)+'\n')
        print(f'Keeping existing labels.json edits; adding {len(additions)} new rows')
    selected = args.font
    if selected is None:
        name = json.loads(path.read_text()).get('font', '')
        selected = args.out/name if name and name != 'BT3MenuReconstructed.ttf' and (args.out/name).is_file() else default_font
    install_font(selected, args.out)
    apply_labels(args.out)
    font_preview(args.out)


def prepare_bundled_fonts(out):
    """Discover compatible TTF files recursively and write the runtime font list."""
    from fontTools.ttLib import TTFont, TTLibError
    root=ROOT/'port/third_party/fonts'
    entries=[]
    for source in sorted(root.rglob('*'),key=lambda p:p.relative_to(root).as_posix()):
        if not source.is_file() or source.suffix.lower() != '.ttf':
            continue
        relative=source.relative_to(root)
        key=relative.as_posix()
        if len(key.encode('utf-8')) > 255 or any(ord(c)<32 for c in key):
            raise ValueError(f'{key}: font path is too long or contains control characters')
        try:
            with TTFont(source) as font:
                if 'fvar' in font or not 256 <= source.stat().st_size <= 4*1024*1024 or ord('H') not in (font.getBestCmap() or {}):
                    print(f'{key}: skipping unsupported font (requires a static TTF with H, up to 4 MB)')
                    continue
                name=font['name'].getDebugName(4) or source.stem
                name=' '.join(''.join(' ' if ord(c)<32 else c for c in name).split())
                if len(name.encode('utf-8')) > 255:
                    name=source.stem
        except (TTLibError, KeyError, ValueError) as exc:
            print(f'{key}: skipping invalid font ({exc})')
            continue
        target=out/'fonts'/relative
        target.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(source,target)
        license=source.parent/'LICENSE'
        if license.is_file():
            shutil.copy2(license,target.parent/'LICENSE')
        entries.append((key,name))
    if not entries:
        raise ValueError('No compatible .ttf fonts found in port/third_party/fonts')
    # Duplicate metadata names are distinguished by their relative source path.
    counts=collections.Counter(name for key,name in entries)
    lines=['# BT3 menu fonts v1: relative_path display_name']
    for key,name in entries:
        if counts[name]>1:
            name=f'{name} ({key})'
        lines.append(key+'\t'+name)
    (out/'fonts.tsv.tmp').write_text('\n'.join(lines)+'\n',encoding='utf-8')
    (out/'fonts.tsv.tmp').replace(out/'fonts.tsv')
    return out/'fonts'/entries[0][0]


def install_font(source, out):
    """Keep a real typeface's curves and names. Never relabel a substitute as the original game font."""
    from fontTools.ttLib import TTFont
    source = source.resolve()
    if not source.is_file() or source.suffix.lower() not in ('.ttf', '.otf'):
        raise ValueError('Choose a static .ttf or .otf font')
    if any(ord(c) < 32 for c in source.name):
        raise ValueError('Font filename contains a control character')
    font = TTFont(source)
    if 'fvar' in font:
        raise ValueError('Use a static font instance, not a variable font')
    cmap = font.getBestCmap()
    if ord('H') not in cmap:
        raise ValueError('The font must contain H for cap-height calibration')
    target = out/source.name
    if source != target.resolve():
        shutil.copy2(source, target)
    license = source.parent/'LICENSE'
    if license.is_file() and source.parent != out.resolve():
        shutil.copy2(license, out/'FONT-LICENSE.txt')
    config_path = out/'labels.json'
    config = json.loads(config_path.read_text())
    config['font'] = source.name
    config_path.write_text(json.dumps(config, indent=2, ensure_ascii=False)+'\n')
    # Filename only: the game resolves it relative to this font directory.
    (out/'font.txt').write_text(source.name+'\n')
    coverage = dict(characters=''.join(chr(c) for c in sorted(cmap) if c>=32 and not 0xD800<=c<=0xDFFF),
                    missing_ascii=''.join(chr(c) for c in range(32,127) if c not in cmap),
                    family=font['name'].getDebugName(1), style=font['name'].getDebugName(2),
                    note='Clean vector typeface used as a visual alternative; not verified as the original BT3 font.')
    (out/'coverage.json').write_text(json.dumps(coverage, indent=2, ensure_ascii=False)+'\n')
    print('Vector font:',target)


def font_preview(out):
    from PIL import Image, ImageDraw, ImageFont
    config=json.loads((out/'labels.json').read_text())
    preview=Image.new('RGB',(1200,440),(27,30,38));d=ImageDraw.Draw(preview)
    font=ImageFont.truetype(str(out/config['font']),48)
    for y,text in zip(range(20,440,65),['Dragon History','The Meteor','Wasteland - Noon','Custom Menu Name',
                                     'ABCDEFGHIJKLMNOPQRSTUVWXYZ','abcdefghijklmnopqrstuvwxyz 0123456789']):
        d.text((20,y),text,font=font,fill=(255,213,123))
    preview.save(out/'font-preview.png')


if __name__ == '__main__':
    try:
        main()
    except ImportError as exc:
        raise SystemExit(f'{exc}. Install Pillow and fonttools in the Python environment used for this tool.')
    except (ValueError, FileNotFoundError) as exc:
        raise SystemExit(str(exc))
