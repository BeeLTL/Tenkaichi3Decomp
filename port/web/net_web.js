// Online play's connection in the page (docs/BROWSER_ONLINE_PLAN.md, P2): one RTCPeerConnection with one data channel,
// unordered and without retransmits (packets as with UDP: late or lost, never held back behind each other). The game's
// transport (port/src/web/net_web.c) sends and takes packets through `bt3Net.send` / `bt3Net.recv`; net.c above it
// keeps its own packets, redundancy, delay, sync check and rollback.
//
// Setting the connection up (signalling) is by hand for now, the matchmaking service comes later (P4):
//   host:  const offer = await bt3Net.offer();          // give it to the other page
//   join:  const answer = await bt3Net.answer(offer);  // give it back
//   host:  await bt3Net.accept(answer);
// Offers and answers are complete (every ICE candidate inside: no trickling). iceServers: none by default (the same
// machine or network); bt3Net.iceServers may name STUN / TURN servers before offer() / answer().
'use strict';

const NET_CHANNEL = { ordered: false, maxRetransmits: 0 };
const NET_GATHER_MS = 5000;  // at most this long for the ICE candidates (then what there is)
const NET_QUEUE_MAX = 4096;  // packets kept while the game does not take them (the oldest go first, as a full socket buffer)
const NET_RELAUNCH_KEY = 'bt3.relaunch';  // (the tab's session storage: what a relaunch hands to the next page)

class Bt3Net {
  constructor() {
    this.pc = null;
    this.channel = null;
    this.queue = [];
    this.iceServers = [];
    this.stats = { sent: 0, received: 0, dropped: 0, opened: 0 };
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
    this.pc = new RTCPeerConnection({ iceServers: this.iceServers });
    return this.pc;
  }

  use(channel) {
    channel.binaryType = 'arraybuffer';
    channel.onopen = () => { this.stats.opened = performance.now(); };
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
  // made again after it (P3: by the test; P4: the matchmaking service).
  relaunch(envText) {
    try { sessionStorage.setItem(NET_RELAUNCH_KEY, envText); } catch (e) { /* (no storage: the page starts as it was) */ }
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
