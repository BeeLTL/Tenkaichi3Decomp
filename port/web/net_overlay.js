// Online play's chat box and connection meter over the game (docs/BROWSER_ONLINE_PLAN.md, P5). Natively the settings
// window draws them (port/src/gs/ui.cpp chat_draw / meter_draw, Dear ImGui); the browser build has no ImGui, so the
// page does, from the engine's values (port/src/web/web_ui.c: Bt3Ui_ChatLines / ChatSay / ChatClose / NetStats).
// The same rules as natively:
//   - the meter (setting "meter", off by default): during a match, ping (and "relay"), rollbacks, the time waited for
//     the other player, the input delay;
//   - the chat (setting "chat", on by default): the last lines in the lower left, each shown 8 s and faded over the
//     last 2; the chat key (Tab, the engine's key_chat) opens a line: Enter sends (one line a second: a line said
//     sooner stays, to be sent again), Esc or the chat key puts it away; the other player can be muted; a click on a
//     line copies it. While a line is typed the keyboard is not the game's (web_ui.c gPortChatTyping); the controller
//     still plays. Box size: setting "chat_size" (Compact / Normal / Large, as natively).
'use strict';

const CHAT_SHOWN_MS = 8000, CHAT_FADE_MS = 2000;
const CHAT_ROWS = [[2, 4], [4, 6], [6, 8]];  // per chat_size: rows shown, rows while typing (ui.cpp)
const OVERLAY_POLL_MS = 200;

class Bt3NetOverlay {
  constructor({ stage, settings }) {
    this.settings = settings;
    this.typing = false;
    this.soon = false;  // the last Enter was within a second of the line before: kept, Enter sends it again
    const style = document.createElement('style');
    style.textContent = `
      #net-meter, #net-chat { position: absolute; z-index: 5; font: 13px/1.35 system-ui, sans-serif; color: #fff; border-radius: 8px; }
      #net-meter { left: 8px; top: 8px; padding: 6px 9px; background: rgba(0, 0, 0, .6); pointer-events: none; white-space: pre; }
      #net-chat { left: 2%; bottom: 14%; padding: 6px 8px; background: rgba(0, 0, 0, .65); max-width: 60%; }
      #net-chat.typing { background: rgba(0, 0, 0, .85); outline: 1px solid rgba(255, 191, 51, .5); }
      #net-chat .line { overflow-wrap: anywhere; text-shadow: 1px 1px 0 #000; }
      #net-chat.typing .line { cursor: copy; }
      #net-chat .mine { color: #59d9ff; } #net-chat .theirs { color: #ffd147; }
      #net-chat .head { display: flex; justify-content: space-between; gap: 12px; color: #ffd147; margin-bottom: 4px; }
      #net-chat input[type=text] { width: 100%; box-sizing: border-box; margin-top: 4px; font: inherit; }
      #net-chat .hint { color: #aaa; font-size: 11.5px; }
      #net-banner[hidden], #net-chat .head[hidden], #net-chat [hidden] { display: none; } /* (a display: flex would override the hidden attribute) */
      #net-banner { position: absolute; inset: 0; z-index: 6; display: flex; align-items: center; justify-content: center; pointer-events: none; }
      #net-banner.dim { background: rgba(0, 0, 0, .55); }
      #net-banner span { font: 700 clamp(18px, 3.2vw, 34px)/1.25 system-ui, sans-serif; color: #fff; text-align: center; padding: 0 5%;
        text-shadow: -2px -2px 0 #000, 2px -2px 0 #000, -2px 2px 0 #000, 2px 2px 0 #000; }`;
    document.head.appendChild(style);
    if (getComputedStyle(stage).position === 'static') stage.style.position = 'relative';
    this.meter = Object.assign(document.createElement('div'), { id: 'net-meter', hidden: true });
    this.meter.setAttribute('aria-hidden', 'true');
    this.box = Object.assign(document.createElement('div'), { id: 'net-chat', hidden: true });
    this.box.setAttribute('role', 'log');
    this.box.setAttribute('aria-live', 'polite');
    this.box.setAttribute('aria-label', 'Online chat');
    this.head = document.createElement('div');
    this.head.className = 'head';
    this.mute = Object.assign(document.createElement('input'), { type: 'checkbox', id: 'net-chat-mute' });
    const muteLabel = Object.assign(document.createElement('label'), { htmlFor: 'net-chat-mute', textContent: ' Mute' });
    muteLabel.prepend(this.mute);
    this.head.append(Object.assign(document.createElement('span'), { textContent: 'Chat (Tab)' }), muteLabel);
    this.lines = document.createElement('div');
    this.input = Object.assign(document.createElement('input'), { type: 'text', maxLength: 160, autocomplete: 'off', spellcheck: false });
    this.input.setAttribute('aria-label', 'Chat line');
    this.hint = Object.assign(document.createElement('div'), { className: 'hint' });
    this.box.append(this.head, this.lines, this.input, this.hint);
    this.bannerEl = Object.assign(document.createElement('div'), { id: 'net-banner', hidden: true });
    this.bannerEl.setAttribute('role', 'status');
    this.bannerEl.appendChild(document.createElement('span'));
    stage.append(this.meter, this.box, this.bannerEl);
    this.mute.addEventListener('change', () => { this.call('Bt3Ui_ChatMutedSet', null, [this.mute.checked ? 1 : 0]); this.input.focus(); });
    this.input.addEventListener('keydown', (e) => this.onKey(e));
    this.lines.addEventListener('click', (e) => {  // a click copies the line (a room code or an address is passed on this way)
      const line = e.target.closest('.line');
      if (!this.typing || !line) return;
      navigator.clipboard?.writeText(line.dataset.text).then(() => { this.copiedAt = Date.now(); this.update(); }, () => {});
      this.input.focus();
    });
    setInterval(() => this.update(), OVERLAY_POLL_MS);
  }

  call(name, ret, args) { return this.settings.mod.ccall(name, ret, args.map((a) => (typeof a === 'string' ? 'string' : 'number')), args); }

  // the engine (web_ui.c Port_UiBanner, upstream 0.1.37 gs/net.c): the start of an online match. A line in the middle,
  // the picture dimmed, while the game waits ("Connecting to your rival...", then the rival's name); ms > 0: the wait
  // is over, the line stays that long more without the dimming; no text and no ms: gone.
  banner(text, ms) {
    clearTimeout(this.bannerTimer);
    if (text !== null) this.bannerEl.firstChild.textContent = text;
    if (text === null && ms <= 0) { this.bannerEl.hidden = true; return; }
    this.bannerEl.hidden = this.bannerEl.firstChild.textContent === '';
    this.bannerEl.classList.toggle('dim', ms <= 0);
    if (ms > 0) this.bannerTimer = setTimeout(() => { this.bannerEl.hidden = true; }, ms);
  }

  // the engine: the chat key during a match (web_ui.c)
  open() {
    this.typing = true;
    this.soon = false;
    this.mute.checked = this.call('Bt3Ui_ChatMuted', 'number', []) === 1;
    this.update();
    this.input.focus();
  }

  close() {
    if (!this.typing) return;
    this.typing = false;
    this.call('Bt3Ui_ChatClose', null, []);
    this.update();
    document.getElementById('canvas')?.focus();
  }

  onKey(e) {
    e.stopPropagation();  // (the page's own keys: not while a line is typed)
    const chatKey = e.key === 'Tab';  // (the engine's chat key is Tab unless rebound; Tab leaves the box either way)
    if (e.key === 'Escape' || chatKey) {
      e.preventDefault();
      this.close();
    } else if (e.key === 'Enter') {
      e.preventDefault();
      const text = this.input.value;
      if (text.trim() === '') { this.close(); return; }
      if (this.call('Bt3Ui_ChatSay', 'number', [text]) === 1) {
        this.input.value = '';
        this.close();
      } else {
        this.soon = true;  // (within a second of the line before: kept)
        this.update();
      }
    }
  }

  update() {
    const s = this.settings;
    if (!s.engineReady || !s.mod) return;
    // the meter (as ui.cpp meter_draw): the pictures shown a second and the game's speed (its blanks a second as a
    // share of 60; a fight shows 30 pictures a second and the menus 60, as on the console), and during an online
    // match the connection
    const meterOn = s.get('meter') === 1;
    if (meterOn) {
      const now = performance.now(), frame = s.get('frame'), blanks = s.get('vblanks');
      if (!this.count || now - this.count.t < 0 || frame < this.count.frame) this.count = { t: now, frame, blanks, fps: 0, speed: 0 };
      else if (now - this.count.t >= 500) {
        this.count = { t: now, frame, blanks, fps: (frame - this.count.frame) * 1000 / (now - this.count.t),
          speed: (blanks - this.count.blanks) * 1000 / (now - this.count.t) / 60 * 100 };
      }
      const lines = [`${this.count.fps.toFixed(0)} fps   speed ${this.count.speed.toFixed(0)}%`];
      const stats = this.call('Bt3Ui_NetStats', 'string', []);
      if (stats) {
        const [ping, rbs, rbf, waits, waitMs, rollMax, delay, auto, relayed] = stats.split(',').map(Number);
        lines.push(ping >= 0 ? `ping ${ping} ms${relayed ? ' (relay)' : ''}` : 'ping ...',
          rollMax > 0 ? `rollbacks ${rbs}/s, ${(rbf / 10).toFixed(1)} frames` : 'rollback off',
          `waited ${waitMs} ms/s (${waits} times)`,
          `delay ${delay}${auto ? ' (auto)' : ''}, rollback up to ${rollMax}`);
      }
      this.meter.textContent = lines.join('\n');
    } else {
      this.count = null;
    }
    this.meter.hidden = !meterOn;
    // the chat
    const online = this.call('Bt3Ui_ChatOnline', 'number', []) === 1;
    if (!online && this.typing) this.close();  // (the match ended while a line was typed)
    const size = Math.min(2, Math.max(0, s.get('chat_size')));
    const rows = CHAT_ROWS[size][this.typing ? 1 : 0];
    const text = online ? this.call('Bt3Ui_ChatLines', 'string', [rows]) : '';
    const lines = text ? text.split('\n').map((l) => { const [mine, age, sender, ...rest] = l.split('\t'); return { mine: mine === '1', age: Number(age), sender, text: rest.join('\t') }; }) : [];
    const shown = this.typing ? lines : lines.filter((l) => l.age < CHAT_SHOWN_MS);
    this.lines.replaceChildren(...shown.map((l) => {
      const row = document.createElement('div');
      row.className = 'line';
      row.dataset.text = l.text;
      if (!this.typing && l.age > CHAT_SHOWN_MS - CHAT_FADE_MS) row.style.opacity = String((CHAT_SHOWN_MS - l.age) / CHAT_FADE_MS);
      const who = Object.assign(document.createElement('span'), { className: l.mine ? 'mine' : 'theirs', textContent: `${l.sender}: ` });
      row.append(who, l.text);
      return row;
    }));
    this.box.classList.toggle('typing', this.typing);
    this.head.hidden = this.input.hidden = this.hint.hidden = !this.typing;
    if (this.typing) this.hint.textContent = Date.now() - (this.copiedAt || 0) < 1500 ? 'Copied' : this.soon ? '1 line/s: Enter to send' : 'Enter: send   Esc/Tab: close   Ctrl+V: paste';
    this.box.hidden = !online || (!this.typing && shown.length === 0);
  }
}
