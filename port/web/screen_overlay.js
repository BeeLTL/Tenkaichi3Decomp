// What the native settings window (port/src/gs/ui.cpp, Dear ImGui) draws over and beside the game's picture, drawn by
// the page instead (the browser build has no ImGui):
//   - the side bars (setting "side_bars", ui.cpp bars_draw): upstream's art (port/assets/bars: trim.png next to the
//     picture, fill.png outwards, the right side mirrored) in the room beside a picture narrower than its space. In
//     the page that room exists in full screen and in touch play, where the picture is fitted to the screen;
//   - the names of stages and songs added from outside the disc, over the stage and music selects (ui.cpp
//     name_text): the game gives each name's rectangle in its 512 x 448 (web_ui.c Bt3Ui_MenuNames), mapped through
//     the picture's rectangle; cream letters with a dark outline, centred for a stage, from the left for a song, in the
//     shipped Fira Sans Condensed Bold (fonts/).
'use strict';

const SCREEN_POLL_MS = 100;
const BAR_TRIM_SHAPE = 34 / 720;  // trim.png's width over its height (port/assets/bars)
const NAME_FONT = 'Bt3Names';

class Bt3ScreenOverlay {
  constructor({ stage, canvas, settings }) {
    this.stage = stage;
    this.canvas = canvas;
    this.settings = settings;
    const style = document.createElement('style');
    style.textContent = `
      .bt3-bar[hidden], .bt3-name[hidden] { display: none; } /* (their display: flex would override the hidden attribute) */
      .bt3-bar { position: absolute; z-index: 1; pointer-events: none; display: flex; }
      .bt3-bar.right { flex-direction: row-reverse; }
      .bt3-bar .fill { flex: 1 1 auto; background: url(bars/fill.png) 0 0 / 100% 100% no-repeat; }
      .bt3-bar .trim { flex: 0 0 auto; background: url(bars/trim.png) right 0 / auto 100% no-repeat; }
      .bt3-bar.right .fill, .bt3-bar.right .trim { transform: scaleX(-1); }
      .bt3-name { position: absolute; z-index: 2; pointer-events: none; display: flex; align-items: center; white-space: nowrap;
        overflow: visible; color: rgb(255, 236, 170); font-family: ${NAME_FONT}, 'Fira Sans Condensed', sans-serif; font-weight: 700; }`;
    document.head.appendChild(style);
    if (getComputedStyle(stage).position === 'static') stage.style.position = 'relative';
    this.bars = ['left', 'right'].map((side) => {
      const bar = document.createElement('div');
      bar.className = `bt3-bar ${side}`;
      bar.hidden = true;
      bar.setAttribute('aria-hidden', 'true');
      bar.append(Object.assign(document.createElement('div'), { className: 'fill' }), Object.assign(document.createElement('div'), { className: 'trim' }));
      stage.appendChild(bar);
      return bar;
    });
    this.names = { stage: this.nameEl(), song: this.nameEl() };
    try {  // (the font of the names; without it the browser's own sans-serif)
      const face = new FontFace(NAME_FONT, 'url(fonts/fira/FiraSansCondensed-Bold.ttf)', { weight: '700' });
      face.load().then((f) => document.fonts.add(f), () => {});
    } catch (e) { /* (no FontFace) */ }
    setInterval(() => this.update(), SCREEN_POLL_MS);
  }

  nameEl() {
    const e = Object.assign(document.createElement('div'), { className: 'bt3-name', hidden: true });
    e.setAttribute('role', 'status');  // (read out: the game's own text is a picture)
    this.stage.appendChild(e);
    return e;
  }

  // The picture's box on the page, in CSS pixels relative to the stage: the canvas, less what object-fit: contain
  // leaves empty around a picture of the canvas's own shape (full screen, touch play).
  pictureBox() {
    const st = this.stage.getBoundingClientRect(), cv = this.canvas.getBoundingClientRect();
    const shape = this.canvas.width / this.canvas.height;
    let w = cv.width, h = cv.height;
    if (w / h > shape) w = h * shape; else h = w / shape;
    return { x: cv.left - st.left + (cv.width - w) / 2, y: cv.top - st.top + (cv.height - h) / 2, w, h, sx: w / this.canvas.width, sy: h / this.canvas.height };
  }

  update() {
    const s = this.settings;
    if (!s.engineReady || !s.mod || !this.canvas.width || !this.canvas.height) return;
    const box = this.pictureBox(), stageW = this.stage.getBoundingClientRect().width;
    // the side bars: where the picture leaves room at its sides
    const fitted = document.fullscreenElement === this.stage || document.body.classList.contains('touch-play');
    const room = Math.floor(box.x);
    const on = s.get('side_bars') === 1 && fitted && room > 2 && stageW - (box.x + box.w) > 2;
    this.bars.forEach((bar, i) => {
      bar.hidden = !on;
      if (!on) return;
      const trim = Math.min(room, box.h * BAR_TRIM_SHAPE);
      Object.assign(bar.style, { left: `${i === 0 ? box.x - room : box.x + box.w}px`, top: `${box.y}px`, width: `${room}px`, height: `${box.h}px` });
      bar.lastChild.style.width = `${trim}px`;  // (narrower than the border: its inner part, as natively)
    });
    // the names of added stages and songs
    const lines = s.mod.ccall('Bt3Ui_MenuNames', 'string', [], []).split('\n');
    const [px, py, pw, ph] = lines[0].split(',').map(Number);
    const shown = new Set();
    for (const line of lines.slice(1)) {
      const [kind, x, y, w, h, , ...name] = line.split('\t');
      const e = this.names[kind];
      if (!e || !(pw > 0 && ph > 0)) continue;
      const kx = pw / 512 * box.sx, ky = ph / 448 * box.sy;
      const left = box.x + (px + Number(x) * pw / 512) * box.sx, top = box.y + (py + Number(y) * ph / 448) * box.sy;
      const width = Number(w) * kx, height = Number(h) * ky, centre = kind === 'stage';
      const size = height * (centre ? 0.42 : 0.72), o = Math.max(1, size * 0.07);
      Object.assign(e.style, { left: `${left}px`, top: `${top}px`, width: `${width}px`, height: `${height}px`, fontSize: `${size}px`,
        justifyContent: centre ? 'center' : 'flex-start', paddingLeft: centre ? '0' : `${height * 0.15}px`,
        textShadow: [[-o, -o], [0, -o], [o, -o], [-o, 0], [o, 0], [-o, o], [0, o], [o, o]].map(([dx, dy]) => `${dx}px ${dy}px 0 rgb(40, 20, 0)`).join(', ') });
      const text = name.join('\t');
      if (e.textContent !== text) e.textContent = text;
      e.hidden = false;
      const room = width * 0.96 - (centre ? 0 : height * 0.15);
      if (e.scrollWidth > room && e.scrollWidth > 0) e.style.fontSize = `${size * room / e.scrollWidth}px`;  // (a long name: smaller, to fit)
      shown.add(kind);
    }
    for (const [kind, e] of Object.entries(this.names)) if (!shown.has(kind)) e.hidden = true;
  }
}
