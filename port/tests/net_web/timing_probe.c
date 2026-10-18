// P0 probe (docs/BROWSER_ONLINE_PLAN.md, test O1): in a page built like the game (Emscripten, JSPI, SDL3),
//  1. how long SDL_Delay(1) and SDL_DelayNS(200000) (net.c's waits) really take;
//  2. a lockstep loop at 30 ticks a second over a real WebRTC data channel (unordered, no retransmits, as planned):
//     tick t sends t, and goes on only when the echo of tick t - DELAY has come back (the other side's input for it),
//     waiting as net.c does (poll, SDL_DelayNS(200000)); then sleeps to the next tick as the game does.
// The other side is a second RTCPeerConnection in the same page that echoes every message at once.
#include <SDL3/SDL.h>
#include <emscripten.h>
#include <stdio.h>
#include <string.h>

EM_JS(void, rtc_open, (void), {
    Module.q = []; Module.rtcReady = 0;
    const a = new RTCPeerConnection(), b = new RTCPeerConnection();
    a.onicecandidate = (e) => { if (e.candidate) b.addIceCandidate(e.candidate); };
    b.onicecandidate = (e) => { if (e.candidate) a.addIceCandidate(e.candidate); };
    b.ondatachannel = (e) => { e.channel.binaryType = 'arraybuffer'; e.channel.onmessage = (m) => e.channel.send(m.data); };
    const dc = a.createDataChannel('game', { ordered: false, maxRetransmits: 0 });
    dc.binaryType = 'arraybuffer';
    dc.onopen = () => { Module.rtcReady = 1; };
    Module.sentAt = {}; Module.rtts = [];
    dc.onmessage = (m) => { const t = new Uint32Array(m.data)[0]; if (Module.sentAt[t] !== undefined) Module.rtts.push(performance.now() - Module.sentAt[t]); Module.q.push(t); };
    Module.dc = dc;
    (async () => {
        const offer = await a.createOffer(); await a.setLocalDescription(offer); await b.setRemoteDescription(offer);
        const answer = await b.createAnswer(); await b.setLocalDescription(answer); await a.setRemoteDescription(answer);
    })().catch((e) => { Module.rtcError = String(e); });
});
EM_JS(int, rtc_ready, (void), { return Module.rtcReady | 0; });
EM_JS(void, rtc_send, (int tick), { Module.sentAt[tick] = performance.now(); Module.dc.send(new Uint32Array([tick]).buffer); });
EM_JS(int, rtc_recv, (void), { return Module.q.length ? Module.q.shift() : -1; });
EM_JS(int, has_asyncify, (void), { return 0; });
EM_JS(void, report, (const char *s), { const r = Module.rtts.slice().sort((a, b) => a - b); const q = (f) => r.length ? r[Math.min(r.length - 1, Math.floor(r.length * f))].toFixed(2) : '-';
    Module.result = UTF8ToString(s) + '  true round trip (measured at arrival): median ' + q(0.5) + ' ms, p95 ' + q(0.95) + ', max ' + q(1) + ' (' + r.length + ' echoes)'; });

static double ms(Uint64 ns) { return (double)ns / 1e6; }

static void delays(const char *what, Uint64 ns, char *out, size_t cap) {
    double lo = 1e9, hi = 0, sum = 0;
    for (int i = 0; i < 200; i++) {
        Uint64 t0 = SDL_GetTicksNS();
        SDL_DelayNS(ns);
        double d = ms(SDL_GetTicksNS() - t0);
        lo = d < lo ? d : lo; hi = d > hi ? d : hi; sum += d;
    }
    size_t n = strlen(out);
    snprintf(out + n, cap - n, "%s: mean %.2f ms, min %.2f, max %.2f\n", what, sum / 200, lo, hi);
}

int main(void) {
    static char out[4096];
    enum { DELAY = 2, TICKS = 300 };
    static Uint64 sent[TICKS], got[TICKS];
    Uint64 t0, next, tick_ns = 1000000000ull / 30, waited = 0;
    int waits = 0, maxwait_t = -1;
    double maxwait = 0;

    snprintf(out, sizeof(out), "emscripten_has_asyncify: %d\n", emscripten_has_asyncify());
    delays("SDL_Delay(1)", 1000000, out, sizeof(out));
    delays("SDL_DelayNS(200000)", 200000, out, sizeof(out));
    delays("SDL_Delay(0)", 0, out, sizeof(out));

    rtc_open();
    t0 = SDL_GetTicksNS();
    while (!rtc_ready() && SDL_GetTicksNS() - t0 < 10000000000ull) SDL_Delay(10);
    if (!rtc_ready()) { strcat(out, "data channel did not open\n"); report(out); return 1; }
    snprintf(out + strlen(out), sizeof(out) - strlen(out), "data channel open after %.1f ms\n", ms(SDL_GetTicksNS() - t0));

    t0 = next = SDL_GetTicksNS();
    for (int t = 0; t < TICKS; t++) {
        sent[t] = SDL_GetTicksNS();
        rtc_send(t);
        for (int r; (r = rtc_recv()) >= 0;) if (r < TICKS && !got[r]) got[r] = SDL_GetTicksNS();
        if (t >= DELAY && !got[t - DELAY]) {       /* the other side's input for t - DELAY: wait for it as net.c does */
            Uint64 w0 = SDL_GetTicksNS();
            while (!got[t - DELAY]) {
                SDL_DelayNS(200000);
                for (int r; (r = rtc_recv()) >= 0;) if (r < TICKS && !got[r]) got[r] = SDL_GetTicksNS();
                if (SDL_GetTicksNS() - w0 > 2000000000ull) break;
            }
            double w = ms(SDL_GetTicksNS() - w0);
            waits++; waited += SDL_GetTicksNS() - w0;
            if (w > maxwait) { maxwait = w; maxwait_t = t; }
        }
        next += tick_ns;                                /* to the next tick, as the game's vertical blank does */
        Uint64 now = SDL_GetTicksNS();
        if (next > now) emscripten_sleep((unsigned)((next - now) / 1000000ull));
    }
    double secs = ms(SDL_GetTicksNS() - t0) / 1000.0;
    double rtt[TICKS]; int n = 0, lost = 0;
    for (int t = 0; t < TICKS; t++) { if (got[t]) rtt[n++] = ms(got[t] - sent[t]); else lost++; }
    for (int i = 1; i < n; i++) for (int j = i; j > 0 && rtt[j] < rtt[j - 1]; j--) { double x = rtt[j]; rtt[j] = rtt[j - 1]; rtt[j - 1] = x; }
    snprintf(out + strlen(out), sizeof(out) - strlen(out),
             "lockstep: %d ticks in %.2f s = %.2f ticks/s (target 30), delay %d\n"
             "  echo round trip: median %.2f ms, p95 %.2f, max %.2f; lost %d\n"
             "  waits for the other side: %d, total %.1f ms, longest %.2f ms (tick %d)\n",
             TICKS, secs, TICKS / secs, DELAY, n ? rtt[n / 2] : -1, n ? rtt[n * 95 / 100] : -1, n ? rtt[n - 1] : -1, lost,
             waits, ms(waited), maxwait, maxwait_t);
    report(out);
    return 0;
}
