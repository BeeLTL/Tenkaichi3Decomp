// Online play's connection in the page (docs/BROWSER_ONLINE_PLAN.md, P2): one RTCPeerConnection with one data channel,
// unordered and without retransmits (packets as with UDP: late or lost, never held back behind each other). The game's
// transport (port/src/web/net_web.c) sends and takes packets through `bt3Net.send` / `bt3Net.recv`; net.c above it
// keeps its own packets, redundancy, delay, sync check and rollback.
//
// Setting the connection up (signalling):
//   - with a room code (P4): the game's own room-code lobby (net.c Port_LobbyStartCode) asks net_web.c's Match_*,
//     which asks matchBegin / matchStatus / matchCancel here; they talk to the signalling Worker (port/web/signal),
//     whose address the page is given once with ?signal=<address> (kept in the browser's local storage);
//   - by hand (tests):
//       host:  const offer = await bt3Net.offer();          // give it to the other page
//       join:  const answer = await bt3Net.answer(offer);  // give it back
//       host:  await bt3Net.accept(answer);
// Offers and answers are complete (every ICE candidate inside: no trickling). iceServers: none by hand (the same
// machine or network); with a room, the Worker's (STUN, and its TURN relay when it has one).
'use strict';

const NET_CHANNEL = { ordered: false, maxRetransmits: 0 };
const NET_GATHER_MS = 5000;  // at most this long for the ICE candidates (then what there is)
const NET_QUEUE_MAX = 4096;  // packets kept while the game does not take them (the oldest go first, as a full socket buffer)
const NET_RELAUNCH_KEY = 'bt3.relaunch';  // (the tab's session storage: what a relaunch hands to the next page)
const NET_ROOM_KEY = 'bt3.room';          // (the same: the room, so that the next page connects again through it)
const NET_SIGNAL_KEY = 'bt3.signal';      // (local storage: the signalling Worker's address)
const NET_POLL_MS = 1000;                 // how often the Worker is asked while waiting for the other page
const NET_LEAVE_MS = 1500;                // a room the game is done with is given up after this long, unless the page reloads for the session
const NET_LEAVE_LAUNCH_MS = 60000;        // ... the same once the two were connected (a session is starting: the game may take a
                                          // while before the page reloads, e.g. Tenkaichi Budokai Online's screens)
const NET_ERRORS = {  // the Worker's errors, as the game's window shows them
  'no room': 'No room with that code (it may have expired).',
  full: 'That room already has two players.',
  version: 'The other player has a different release of the game.',
  busy: 'Too many tries from here: wait a little and try again.',
};

// a line in the page's log (index.html's log, once it is there)
const netSay = (text) => { if (typeof log === 'function') log(`net: ${text}`); else console.log(`net: ${text}`); };

class Bt3Net {
  constructor() {
    this.pc = null;
    this.channel = null;
    this.queue = [];
    this.iceServers = [];
    this.stats = { sent: 0, received: 0, dropped: 0, opened: 0 };
    this.room = null;     // {url, code, key, role (1 host, 2 joined), round, name}
    this.match = { state: 0, code: '', error: '', peerName: '' };  // what Match_Poll reports (net_match.h's states)
    this.matchRun = 0;    // raised by every begin / cancel: an older run stops at its next step
    this.leaveTimer = 0;
    this.signalUrl = Bt3Net.signal();
    this.resume();
  }

  // The signalling Worker's address: ?signal=<address> sets it (and keeps it), else the kept one, else ''.
  static signal() {
    let url = '';
    try {
      const q = new URLSearchParams(location.search).get('signal');
      if (q !== null) localStorage.setItem(NET_SIGNAL_KEY, q);
      url = localStorage.getItem(NET_SIGNAL_KEY) || '';
    } catch (e) { /* (no storage) */ }
    return url.replace(/\/+$/, '');
  }

  async post(what, body, url = this.room ? this.room.url : this.signalUrl) {
    let r;
    try {
      r = await fetch(`${url}/v1/${what}`, {
        method: 'POST', headers: { 'content-type': 'application/json' }, body: JSON.stringify(body), cache: 'no-store',
      });
    } catch (e) {
      throw new Error('The signalling service could not be reached.');
    }
    const j = await r.json().catch(() => ({ error: 'bad answer' }));
    if (j.error) throw new Error(NET_ERRORS[j.error] || `The signalling service said: ${j.error}.`);
    return j;
  }

  // net_web.c Match_Begin: role 1 makes a room, 2 joins room `code`, 3 a quick match (not here yet).
  matchBegin(role, code, name, version) {
    const run = ++this.matchRun;
    clearTimeout(this.leaveTimer);
    this.match = { state: 1, code: '', error: '', peerName: '' };
    const fail = (e) => { if (run === this.matchRun) this.match = { ...this.match, state: -1, error: String(e.message || e) }; };
    if (role === 3) return fail(new Error('Quick match is not available in the browser version yet: use a room code.'));
    if (!this.signalUrl) return fail(new Error('Room codes need a signalling service: open the page once with ?signal=<its address>.'));
    this.room = null;
    (async () => {
      const url = this.signalUrl;
      if (role === 1) {
        const r = await this.post('host', { v: version, name });
        if (run !== this.matchRun) return;
        this.room = { url, code: r.code, key: r.key, role: 1, round: 1, ice: r.ice };
        this.match = { ...this.match, state: 2, code: r.code };
      } else {
        const want = String(code || '').toUpperCase().replace(/[^A-Z0-9]/g, '');
        this.room = { url, code: want, key: '', role: 2, round: 0, ice: [] };
        const r = await this.post('join', { v: version, code: want, name });
        if (run !== this.matchRun) return;
        Object.assign(this.room, { key: r.key, ice: r.ice });
        this.match = { ...this.match, code: want, peerName: r.peer.name };
      }
      await this.connectRound(run);
    })().catch(fail);
  }

  // One round of offer and answer through the room (the lobby's, or the session's after the reload); then state 3.
  async connectRound(run) {
    const room = this.room;
    this.iceServers = room.ice || [];
    if (room.role === 1) {
      const offer = await this.offer();
      if (run !== this.matchRun) return;
      await this.post('offer', { code: room.code, key: room.key, round: room.round, sdp: offer });
      for (;;) {
        const p = await this.post('poll', { code: room.code, key: room.key });
        if (run !== this.matchRun) return;
        if (p.peer) this.match.peerName = p.peer.name;
        if (p.answer && p.answer.round === room.round) { await this.accept(p.answer.sdp); break; }
        await new Promise((done) => setTimeout(done, NET_POLL_MS));
      }
    } else {
      for (;;) {
        const p = await this.post('poll', { code: room.code, key: room.key });
        if (run !== this.matchRun) return;
        if (p.offer && p.offer.round > room.round) {
          room.round = p.offer.round;
          const answer = await this.answer(p.offer.sdp);
          if (run !== this.matchRun) return;
          await this.post('answer', { code: room.code, key: room.key, round: room.round, sdp: answer });
          break;
        }
        await new Promise((done) => setTimeout(done, NET_POLL_MS));
      }
    }
    if (run === this.matchRun) this.match = { ...this.match, state: 3 };
  }

  // net_web.c Match_Poll
  matchStatus() { return this.match; }

  // net_web.c Match_Cancel: the game is done with the room (cancelled, or the session starts: net.c Port_LobbyLaunch).
  // Given up a moment later, unless the page reloads for the session meanwhile (relaunch keeps it for the next page).
  matchCancel() {
    const connected = this.match.state === 3;
    if (this.room) netSay(`the game is done with room ${this.room.code} (${connected ? 'connected' : 'not connected'})`);
    this.matchRun++;
    this.match = { state: 0, code: '', error: '', peerName: '' };
    const room = this.room;
    clearTimeout(this.leaveTimer);
    if (room && room.key) this.leaveTimer = setTimeout(() => this.leave(room), connected ? NET_LEAVE_LAUNCH_MS : NET_LEAVE_MS);
  }

  leave(room) {
    netSay(`room ${room.code} given up`);
    if (this.room === room) this.room = null;
    this.post('leave', { code: room.code, key: room.key }, room.url).catch(() => {});
  }

  // At the start of a page loaded for a session: the room the lobby used, to connect again through it (round 2).
  resume() {
    let room = null;
    try { room = JSON.parse(sessionStorage.getItem(NET_ROOM_KEY) || 'null'); sessionStorage.removeItem(NET_ROOM_KEY); } catch (e) { /* (none) */ }
    if (!room || !room.session) return;
    delete room.session;
    if (room.role === 1) room.round++;
    this.room = room;
    const run = ++this.matchRun;
    setTimeout(() => netSay(`connecting again for the session through room ${room.code} (round ${room.role === 1 ? room.round : 'next'})`), 0);
    this.connectRound(run).then(() => {
      netSay(run === this.matchRun ? 'connected again for the session' : 'the reconnection was stopped (the game began or cancelled a room meanwhile)');
      if (this.room === room && room.role === 1) this.leave(room);  // (both are in: the room has done its work)
    }).catch((e) => netSay(`could not connect again for the session: ${e.message || e}`));
  }

  get open() { return !!this.channel && this.channel.readyState === 'open'; }

  close() {
    if (this.channel) this.channel.close();
    if (this.pc) this.pc.close();
    this.pc = this.channel = null;
    this.queue = [];
  }

  connection() {
    this.close();
    const pc = new RTCPeerConnection({ iceServers: this.iceServers });
    pc.addEventListener('iceconnectionstatechange', () => { if (this.pc === pc) netSay(`connection: ${pc.iceConnectionState}`); });
    this.pc = pc;
    return pc;
  }

  use(channel) {
    channel.binaryType = 'arraybuffer';
    channel.onopen = () => { this.stats.opened = performance.now(); netSay('channel open'); };
    channel.onclose = () => { if (this.channel === channel) netSay('channel closed'); };
    channel.onmessage = (e) => {
      this.stats.received++;
      if (this.queue.length >= NET_QUEUE_MAX) { this.queue.shift(); this.stats.dropped++; }
      this.queue.push(new Uint8Array(e.data));
    };
    this.channel = channel;
  }

  async gathered() {  // the local description once its candidates are in (or after NET_GATHER_MS)
    const pc = this.pc;
    if (pc.iceGatheringState !== 'complete') {
      await new Promise((done) => {
        const t = setTimeout(done, NET_GATHER_MS);
        pc.addEventListener('icegatheringstatechange', () => { if (pc.iceGatheringState === 'complete') { clearTimeout(t); done(); } });
      });
    }
    return JSON.stringify(pc.localDescription);
  }

  async offer() {
    const pc = this.connection();
    this.use(pc.createDataChannel('bt3', NET_CHANNEL));
    await pc.setLocalDescription(await pc.createOffer());
    return this.gathered();
  }

  async answer(offer) {
    const pc = this.connection();
    pc.ondatachannel = (e) => this.use(e.channel);
    await pc.setRemoteDescription(JSON.parse(offer));
    await pc.setLocalDescription(await pc.createAnswer());
    return this.gathered();
  }

  async accept(answer) {
    await this.pc.setRemoteDescription(JSON.parse(answer));
  }

  // for net_web.c
  send(bytes) {
    if (!this.open) return false;
    try {
      this.channel.send(bytes);
    } catch (e) {  // (the browser's send buffer is full: lost, as a full socket would lose it)
      return false;
    }
    this.stats.sent++;
    return true;
  }

  recv() { return this.queue.shift(); }

  // The game starts a session, or ends one, by starting the program again (net.c relaunch, net_web.c): here the page
  // loads itself again and starts the game at once with these variables ("NAME=value" lines, every BT3_* one the
  // program had), as the next program would get them (index.html). The connection does not survive a reload: it is
  // made again after it: through the room the lobby used, if there is one (kept for the next page, see resume), else
  // by hand (tests).
  relaunch(envText) {
    try {
      sessionStorage.setItem(NET_RELAUNCH_KEY, envText);
      clearTimeout(this.leaveTimer);
      if (this.room && this.room.key && /(^|\n)BT3_NET_SESSION=1(\n|$)/.test(envText)) {
        sessionStorage.setItem(NET_ROOM_KEY, JSON.stringify({ ...this.room, session: true }));
      } else if (this.room && this.room.key) {
        this.leave(this.room);
      }
    } catch (e) { /* (no storage: the page starts as it was) */ }
    this.close();
    location.reload();
  }

  // index.html, at start: the variables of a relaunch (and forgets them), or null
  static takeRelaunch() {
    let text = null;
    try { text = sessionStorage.getItem(NET_RELAUNCH_KEY); sessionStorage.removeItem(NET_RELAUNCH_KEY); } catch (e) { /* (none) */ }
    if (text === null) return null;
    const env = {};
    for (const line of text.split('\n')) { const i = line.indexOf('='); if (i > 0) env[line.slice(0, i)] = line.slice(i + 1); }
    return env;
  }

  static relaunchPending() {
    try { return sessionStorage.getItem(NET_RELAUNCH_KEY) !== null; } catch (e) { return false; }
  }
}

globalThis.bt3Net = new Bt3Net();
