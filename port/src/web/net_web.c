/*
 * Browser build: online play's transport (port/src/gs/net_io.h) over a WebRTC data channel, and what net.c needs of
 * the two native files that are not in this build (net_match.c, net_relay.c). docs/BROWSER_ONLINE_PLAN.md, P2.
 *
 * The connection itself lives in the page (port/web/net_web.js, `bt3Net`): an RTCPeerConnection with one data channel,
 * unordered and without retransmits (packets as with UDP: late or lost, never held back behind each other). It is set
 * up by the page, before or while net.c waits for the other side (net.c's waits suspend the game: JSPI). Here:
 *   - one peer per session. net.c names peers by address: what comes in is said to come from the address this side
 *     last sent to (the joiner: the host's, as BT3_NET_JOIN gave it), or, before anything was sent (the host, until the
 *     joiner's greeting), from a fixed stand-in address, which the host then sends back to;
 *   - "nothing there" is -1, as recvfrom on a non-blocking socket says it;
 *   - a packet sent while the channel is not open is lost (-1, counted by net.c as a failed send), as UDP would lose it.
 * No relay (WebRTC's ICE uses TURN itself, in the page) and no matchmaking yet (P4): those calls say so.
 */
#include <emscripten.h>
#include <stdio.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "../gs/net_io.h"
#include "../gs/net_match.h"
#include "../gs/net_relay.h"

/* ---- the transport ---- */
EM_JS(int, web_net_send, (const void *buf, int n), {
    const net = globalThis.bt3Net;
    return net && net.send(HEAPU8.slice(buf, buf + n)) ? n : -1;
});
EM_JS(int, web_net_recv, (void *buf, int size), {
    const net = globalThis.bt3Net, m = net && net.recv();
    if (!m) return -1;
    const k = Math.min(m.length, size);
    HEAPU8.set(m.subarray(0, k), buf);
    return k;
});

#define WEB_NET_HANDLE 3 /* (any number >= 0: net.c only keeps it and hands it back) */
static struct sockaddr_in sLastTo;
static int sLastToSet;

int NetIo_Open(void) {
    sLastToSet = 0;
    return WEB_NET_HANDLE;
}

int NetIo_Bind(int h, const struct sockaddr_in *local) {
    (void)h;
    (void)local;
    return 0; /* (no ports: the page's connection is the only one) */
}

int NetIo_LocalAddr(int h, struct sockaddr_in *out) {
    (void)h;
    memset(out, 0, sizeof(*out));
    out->sin_family = AF_INET;
    out->sin_port = htons(1);
    return 0;
}

int NetIo_SendTo(int h, const void *buf, int n, const struct sockaddr_in *to) {
    (void)h;
    sLastTo = *to;
    sLastToSet = 1;
    return web_net_send(buf, n);
}

int NetIo_RecvFrom(int h, void *buf, int size, struct sockaddr_in *from) {
    int n;
    (void)h;
    n = web_net_recv(buf, size);
    if (n <= 0) {
        return -1;
    }
    if (sLastToSet) {
        *from = sLastTo;
    } else {
        memset(from, 0, sizeof(*from));
        from->sin_family = AF_INET;
        from->sin_port = htons(1);
        from->sin_addr.s_addr = htonl(0x7F000002u); /* 127.0.0.2: the stand-in for "the other player" */
    }
    return n;
}

void NetIo_Close(int h) {
    (void)h;
    sLastToSet = 0; /* (the connection is the page's: it stays for the next session) */
}

/* ---- no relay: WebRTC does its own (net_relay.h) ---- */
int Relay_Allocate(int sock, const char *server, const char *username, const char *password, char *out, int size) {
    (void)sock; (void)server; (void)username; (void)password; (void)out; (void)size;
    return 0;
}
void Relay_Permit(int sock, const struct sockaddr_in *peer) { (void)sock; (void)peer; }
int Relay_Active(void) { return 0; }
void Relay_Tick(int sock) { (void)sock; }
int Relay_Unwrap(uint8_t *pkt, int n, struct sockaddr_in *from, uint8_t **data, int *len) {
    (void)pkt; (void)n; (void)from; (void)data; (void)len;
    return 2; /* an ordinary packet */
}
int Relay_IsPeer(const struct sockaddr_in *to) { (void)to; return 0; }
void Relay_Send(int sock, const void *data, int len, const struct sockaddr_in *to) { (void)sock; (void)data; (void)len; (void)to; }
void Relay_SawDirect(const struct sockaddr_in *from) { (void)from; }
void Relay_Close(int sock) { (void)sock; }

/* ---- no matchmaking yet (P4): room codes and quick match say so (net_match.h) ---- */
const char *const kMatchQueues[MATCH_QUEUES] = {"S", "T", "D10", "D15", "D20"};
static const char kNoMatch[] = "Room codes are not available in the browser version yet.";
static int sMatchState;

void Match_Begin(int role, int sock, int localPort, const char *code, const char *name, int version) {
    (void)role; (void)sock; (void)localPort; (void)code; (void)name; (void)version;
    sMatchState = -1;
}
int Match_SocketBusy(void) { return 0; }
void Match_QueuesAsk(int version) { (void)version; }
void Match_HelloAsk(void) {}
void Match_UpdateAsk(void) {}
int Match_UpdateLatest(void) { return 0; }
int Match_Release(int *min, int *latest) { if (min) *min = 0; if (latest) *latest = 0; return 0; }
int Match_QueueWaiting(int queue) { (void)queue; return 0; }
void Match_Poll(MatchStatus *out) {
    memset(out, 0, sizeof(*out));
    out->state = sMatchState;
    if (sMatchState < 0) {
        snprintf(out->error, sizeof(out->error), "%s", kNoMatch);
    }
}
void Match_Cancel(void) { sMatchState = 0; }
int Match_SignTest(void) { return 0; }
