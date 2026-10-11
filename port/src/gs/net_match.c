/*
 * Matchmaking with room codes: the client side of port/matchmaking/worker.js.
 *
 * The lobby (net.c) opens the UDP socket the match will use and hands it over. A thread then:
 *   1. asks a STUN server, from that socket, which address and port the rest of the internet sees it under;
 *   2. tells the Worker (host: "make a room", and then asks every two seconds whether someone has joined; the one who
 *      joins: "join room <code>");
 *   3. ends with the other player's addresses, which the lobby sends its greetings to (both sides at once: that is
 *      what lets the two routers pass each other's packets).
 * The Worker is spoken to over HTTPS with what the system has: WinHTTP on Windows, the curl library on Linux, and
 * failing those the `curl` program (BT3_MATCH_CURL=1 asks for the program). No game data goes that way.
 *
 * BT3_MATCH_URL=<address> names another Worker (one's own; see port/matchmaking/README.md).
 */
#include <SDL3/SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#define MATCH_URL_DEFAULT "https://odd-surf-0af7.bt3decomp.workers.dev"
#include "net_match.h"
#include "../port_release.h"
#include "net_relay.h"

static struct {
    SDL_Mutex *lock;
    SDL_Thread *thread;
    SDL_AtomicInt cancel, gen; /* gen: which search is the current one (a thread of an earlier one writes nothing) */
    MatchStatus st;
    int role, sock, localPort, version;
    char code[8], name[20]; /* (role 3, a quick match: `code` is the queue's name until the service gives a room) */
    char queue[8];
} sM;

/* Quick match: what this game calls itself to the service (so that a room it left behind is not given back to
   it): sixteen letters made up once per run. */
static const char *match_who(void) {
    static char who[20];
    if (who[0] == '\0') {
        int i;
        SDL_srand(SDL_GetTicksNS() ^ (Uint64)(uintptr_t)&who);
        for (i = 0; i < 16; i++) {
            who[i] = (char)('a' + SDL_rand(26));
        }
        who[16] = '\0';
    }
    return who;
}

static SDL_TLSID sMine; /* the thread's own search number */
#define STOPPED() (SDL_GetAtomicInt(&sM.cancel) || (int)(intptr_t)SDL_GetTLS(&sMine) != SDL_GetAtomicInt(&sM.gen))

static const char *match_url(void) {
    const char *e = SDL_getenv("BT3_MATCH_URL");
    return e != NULL && e[0] != '\0' ? e : MATCH_URL_DEFAULT;
}

static void set_state(int state, const char *error) {
    if ((int)(intptr_t)SDL_GetTLS(&sMine) != SDL_GetAtomicInt(&sM.gen)) {
        return;
    }
    SDL_LockMutex(sM.lock);
    sM.st.state = state;
    if (error != NULL) {
        SDL_strlcpy(sM.st.error, error, sizeof(sM.st.error));
    }
    SDL_UnlockMutex(sM.lock);
}

/* ---- the Worker, through curl ---- */

#ifdef _WIN32
/* Windows: its own HTTPS (WinHTTP), so nothing has to be installed and no other program is started. */
#include <winhttp.h>

static int http_post_native(const char *url, const char *json, char *out, size_t size) {
    wchar_t wurl[512], host[256], path[256];
    URL_COMPONENTS uc;
    HINTERNET ses = NULL, con = NULL, req = NULL;
    DWORD got = 0, used = 0, status = 0, len = sizeof(status);
    int ok = 0;

    if (MultiByteToWideChar(CP_UTF8, 0, url, -1, wurl, 512) == 0) {
        return 0;
    }
    memset(&uc, 0, sizeof(uc));
    uc.dwStructSize = sizeof(uc);
    uc.lpszHostName = host;
    uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = 256;
    if (!WinHttpCrackUrl(wurl, 0, 0, &uc)) {
        return 0;
    }
    ses = WinHttpOpen(L"Tenkaichi3Decomp", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (ses != NULL) {
        WinHttpSetTimeouts(ses, 8000, 8000, 8000, 12000);
        con = WinHttpConnect(ses, host, uc.nPort, 0);
    }
    if (con != NULL) {
        req = WinHttpOpenRequest(con, L"POST", path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
    }
    if (req != NULL && WinHttpSendRequest(req, L"Content-Type: application/json\r\n", (DWORD)-1, (LPVOID)json, (DWORD)strlen(json), (DWORD)strlen(json), 0) &&
        WinHttpReceiveResponse(req, NULL)) {
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &len, WINHTTP_NO_HEADER_INDEX);
        while (used + 1 < size && WinHttpReadData(req, out + used, (DWORD)(size - 1 - used), &got) && got != 0) {
            used += got;
        }
        out[used] = '\0';
        ok = used > 0;
    }
    if (req != NULL) { WinHttpCloseHandle(req); }
    if (con != NULL) { WinHttpCloseHandle(con); }
    if (ses != NULL) { WinHttpCloseHandle(ses); }
    return ok;
}
#else
/* Linux: the system's curl library, if it is there (it nearly always is), loaded when first needed. */
static int http_post_native(const char *url, const char *json, char *out, size_t size) {
    typedef void *(*init_fn)(void);
    typedef int (*setopt_fn)(void *, int, ...);
    typedef int (*perform_fn)(void *);
    typedef void (*cleanup_fn)(void *);
    static SDL_SharedObject *lib;
    static int tried;
    static init_fn init;
    static setopt_fn setopt;
    static perform_fn perform;
    static cleanup_fn cleanup;
    void *h;
    SDL_IOStream *mem;
    FILE *fp;
    int rc;
    long n;

    if (!tried) {
        static const char *const names[] = {"libcurl.so.4", "libcurl-gnutls.so.4", "libcurl.so"};
        size_t i;
        tried = 1;
        for (i = 0; lib == NULL && i < sizeof(names) / sizeof(names[0]); i++) {
            lib = SDL_LoadObject(names[i]);
        }
        if (lib != NULL) {
            init = (init_fn)SDL_LoadFunction(lib, "curl_easy_init");
            setopt = (setopt_fn)SDL_LoadFunction(lib, "curl_easy_setopt");
            perform = (perform_fn)SDL_LoadFunction(lib, "curl_easy_perform");
            cleanup = (cleanup_fn)SDL_LoadFunction(lib, "curl_easy_cleanup");
        }
    }
    (void)mem;
    if (init == NULL || setopt == NULL || perform == NULL || cleanup == NULL || (h = init()) == NULL) {
        return 0;
    }
    fp = tmpfile(); /* (the library writes the answer with fwrite by default: into a file that is never on disk by name) */
    if (fp == NULL) {
        cleanup(h);
        return 0;
    }
    setopt(h, 10002 /* CURLOPT_URL */, url);
    setopt(h, 10015 /* CURLOPT_POSTFIELDS */, json);
    setopt(h, 10001 /* CURLOPT_WRITEDATA */, fp);
    setopt(h, 13 /* CURLOPT_TIMEOUT */, 12L);
    setopt(h, 99 /* CURLOPT_NOSIGNAL */, 1L);
    rc = perform(h);
    cleanup(h);
    n = 0;
    if (rc == 0) {
        rewind(fp);
        n = (long)fread(out, 1, size - 1, fp);
        out[n > 0 ? n : 0] = '\0';
    }
    fclose(fp);
    return rc == 0 && n > 0;
}
#endif

/* POSTs `json` to <url>/v1/<what>; the answer's text in out. 0 = no answer. The system's own HTTPS first (above); if
   that is not to be had, the `curl` program. */
static int http_post(const char *what, const char *json, char *out, size_t size) {
    char url[256];
    const char *args[12];
    SDL_Process *p;
    size_t got = 0;
    int code = 0, n = 0;
    void *data;

    SDL_snprintf(url, sizeof(url), "%s/v1/%s", match_url(), what);
    if (SDL_getenv("BT3_MATCH_CURL") == NULL && http_post_native(url, json, out, size)) {
        return 1;
    }
#ifdef _WIN32
    args[n++] = "curl.exe";
#else
    args[n++] = "curl";
#endif
    args[n++] = "-s";
    args[n++] = "-m";
    args[n++] = "12";
    args[n++] = "-X";
    args[n++] = "POST";
    args[n++] = url;
    args[n++] = "-d";
    args[n++] = json;
    args[n] = NULL;
    p = SDL_CreateProcess(args, true);
    if (p == NULL) {
        return 0;
    }
    data = SDL_ReadProcess(p, &got, &code);
    SDL_DestroyProcess(p);
    if (data == NULL) {
        return 0;
    }
    SDL_strlcpy(out, (const char *)data, size);
    SDL_free(data);
    return got > 0;
}

/* The text of "key":"..." in a flat JSON answer. 0 = not there. */
static int json_str(const char *json, const char *key, char *out, size_t size) {
    char pat[40];
    const char *p, *e;

    SDL_snprintf(pat, sizeof(pat), "\"%s\":\"", key);
    p = SDL_strstr(json, pat);
    if (p == NULL) {
        return 0;
    }
    p += SDL_strlen(pat);
    e = SDL_strchr(p, '"');
    if (e == NULL || (size_t)(e - p) >= size) {
        return 0;
    }
    SDL_memcpy(out, p, (size_t)(e - p));
    out[e - p] = '\0';
    return 1;
}

/* The peer of an answer: "peer":{"name":"..","addrs":["..",".."]}. 0 = none ("peer":null, or no such part). */
static int json_peer(const char *json, MatchStatus *st) {
    const char *p = SDL_strstr(json, "\"peer\":{"), *a;
    int n = 0;

    if (p == NULL) {
        return 0;
    }
    json_str(p, "name", st->peerName, sizeof(st->peerName));
    a = SDL_strstr(p, "\"addrs\":[");
    if (a == NULL) {
        return 0;
    }
    a += 9;
    while (*a == '"' && n < MATCH_ADDRS) {
        const char *e = SDL_strchr(a + 1, '"');
        if (e == NULL || (size_t)(e - a - 1) >= sizeof(st->peerAddr[0])) {
            break;
        }
        SDL_memcpy(st->peerAddr[n], a + 1, (size_t)(e - a - 1));
        st->peerAddr[n][e - a - 1] = '\0';
        n++;
        a = e + 1;
        if (*a == ',') {
            a++;
        }
    }
    st->peerCount = n;
    return n > 0;
}

/* ---- STUN (RFC 5389): one question, "which address do you see me under?" ---- */

static int stun_ask(int sock, const char *host, int port, char *out, size_t size) {
    struct addrinfo hints, *res = NULL;
    uint8_t req[20], pkt[512];
    char portText[8];
    int tries, i;

    SDL_memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    SDL_snprintf(portText, sizeof(portText), "%d", port);
    if (getaddrinfo(host, portText, &hints, &res) != 0 || res == NULL) {
        return 0;
    }
    SDL_memset(req, 0, sizeof(req));
    req[1] = 0x01; /* binding request, no attributes */
    req[4] = 0x21; req[5] = 0x12; req[6] = 0xA4; req[7] = 0x42; /* the magic cookie */
    for (i = 8; i < 20; i++) {
        req[i] = (uint8_t)SDL_rand(256);
    }
    for (tries = 0; tries < 3 && !STOPPED(); tries++) {
        int waited;
        sendto(sock, (const char *)req, sizeof(req), 0, res->ai_addr, (int)res->ai_addrlen);
        for (waited = 0; waited < 500; waited += 10) {
            struct sockaddr_in from;
            socklen_t fromLen = sizeof(from);
            int n = (int)recvfrom(sock, (char *)pkt, sizeof(pkt), 0, (struct sockaddr *)&from, &fromLen), at;
            if (n < 20 || pkt[0] != 0x01 || pkt[1] != 0x01 || SDL_memcmp(pkt + 8, req + 8, 12) != 0) {
                SDL_Delay(10);
                continue;
            }
            for (at = 20; at + 4 <= n;) {
                int type = pkt[at] << 8 | pkt[at + 1], len = pkt[at + 2] << 8 | pkt[at + 3];
                if (at + 4 + len > n) {
                    break;
                }
                if ((type == 0x0020 || type == 0x0001) && len >= 8 && pkt[at + 5] == 0x01) {
                    const uint8_t *v = &pkt[at + 4];
                    unsigned p = (unsigned)(v[2] << 8 | v[3]);
                    uint8_t ip[4] = {v[4], v[5], v[6], v[7]};
                    if (type == 0x0020) { /* XOR-MAPPED-ADDRESS: with the cookie */
                        p ^= 0x2112;
                        ip[0] ^= 0x21; ip[1] ^= 0x12; ip[2] ^= 0xA4; ip[3] ^= 0x42;
                    }
                    SDL_snprintf(out, size, "%u.%u.%u.%u:%u", ip[0], ip[1], ip[2], ip[3], p);
                    freeaddrinfo(res);
                    return 1;
                }
                at += 4 + ((len + 3) & ~3);
            }
        }
    }
    freeaddrinfo(res);
    return 0;
}

/* This machine's address on its own network (for two players behind the same router): the address a socket would
   send from, found without sending anything. */
static int lan_address(int localPort, char *out, size_t size) {
    struct sockaddr_in to, me;
    socklen_t len = sizeof(me);
    int s = (int)socket(AF_INET, SOCK_DGRAM, 0), ok = 0;
    char text[32];

    if (s < 0) {
        return 0;
    }
    SDL_memset(&to, 0, sizeof(to));
    to.sin_family = AF_INET;
    to.sin_port = htons(9);
    inet_pton(AF_INET, "192.0.2.1", &to.sin_addr); /* (a documentation address: nothing is sent) */
    if (connect(s, (struct sockaddr *)&to, sizeof(to)) == 0 && getsockname(s, (struct sockaddr *)&me, &len) == 0 &&
        inet_ntop(AF_INET, &me.sin_addr, text, sizeof(text)) != NULL && SDL_strcmp(text, "0.0.0.0") != 0) {
        SDL_snprintf(out, size, "%s:%d", text, localPort);
        ok = 1;
    }
#ifdef _WIN32
    closesocket(s);
#else
    close(s);
#endif
    return ok;
}

/* ---- the thread ---- */

static int SDLCALL match_thread(void *arg) {
    char pub[64] = "", lan[64] = "", addrs[160], body[512], answer[1024], key[40] = "", you[64] = "", name[20], relayAddr[64] = "";
    int i, n = 0, hosting, searches = 0;

    SDL_SetTLS(&sMine, arg, NULL);
    for (i = 0; sM.name[i] != '\0' && n < (int)sizeof(name) - 1; i++) { /* (a name that needs no escaping) */
        char c = sM.name[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '_' || c == '-' || c == '.') {
            name[n++] = c;
        }
    }
    name[n] = '\0';
    if (!stun_ask(sM.sock, "stun.cloudflare.com", 3478, pub, sizeof(pub))) {
        stun_ask(sM.sock, "stun.l.google.com", 19302, pub, sizeof(pub));
    }
    lan_address(sM.localPort, lan, sizeof(lan));
    if (STOPPED()) {
        return 0;
    }
    /* (no public address found: the Worker's view of this machine's address with the local port is tried in its
       place, below, once the Worker has said what it sees) */
    SDL_snprintf(addrs, sizeof(addrs), "%s%s%s%s%s%s%s", pub[0] ? "\"" : "", pub, pub[0] ? "\"" : "", pub[0] && lan[0] ? "," : "", lan[0] ? "\"" : "", lan,
                 lan[0] ? "\"" : "");
    if (addrs[0] == '\0') {
        set_state(-1, "This computer's address could not be found (no network?).");
        return 0;
    }
again:
    hosting = sM.role == 1;
    if (sM.role == 3) {
        /* A quick match: the service puts this player with whoever waits longest in the queue (then this is the
           one who joins), or gives it a room of its own in the queue to wait in (then it is the host). A room
           lasts ten minutes; when it runs out the search simply starts again. */
        MatchStatus st;
        SDL_snprintf(body, sizeof(body), "{\"v\":%d,\"rel\":%d,\"name\":\"%s\",\"addrs\":[%s],\"queue\":\"%s\",\"who\":\"%s\"%s}", sM.version, PORT_RELEASE, name, addrs, sM.queue,
                     match_who(), relayAddr[0] != '\0' ? ",\"relay\":false" : "");
        if (!http_post("quick", body, answer, sizeof(answer))) {
            set_state(-1, "The matchmaking service could not be reached (is the network up?).");
            return 0;
        }
        if (STOPPED()) {
            return 0;
        }
        searches++;
        SDL_memset(&st, 0, sizeof(st));
        if (SDL_strstr(answer, "\"role\":\"join\"") != NULL && json_peer(answer, &st)) {
            SDL_LockMutex(sM.lock);
            SDL_strlcpy(sM.st.peerName, st.peerName, sizeof(sM.st.peerName));
            SDL_memcpy(sM.st.peerAddr, st.peerAddr, sizeof(st.peerAddr));
            sM.st.peerCount = st.peerCount;
            sM.st.role = 2;
            sM.st.state = 3;
            SDL_UnlockMutex(sM.lock);
            return 0;
        }
        if (SDL_strstr(answer, "\"role\":\"host\"") == NULL) {
            set_state(-1, SDL_strstr(answer, "\"old\"") != NULL ? "This release is too old for online play through the matchmaking service. Get the newest release." :
                          SDL_strstr(answer, "busy") != NULL ? "Too many searches from this address; try again in a few minutes." :
                          SDL_strstr(answer, "bad request") != NULL || SDL_strstr(answer, "error") != NULL ? "The matchmaking service does not know quick match yet." :
                          "The matchmaking service gave no answer.");
            return 0;
        }
        hosting = 1;
    }
    if (hosting) {
        if (sM.role == 1) {
            SDL_snprintf(body, sizeof(body), "{\"v\":%d,\"rel\":%d,\"name\":\"%s\",\"addrs\":[%s]}", sM.version, PORT_RELEASE, name, addrs);
            if (!http_post("host", body, answer, sizeof(answer))) {
                set_state(-1, "The matchmaking service could not be reached (is the network up?).");
                return 0;
            }
            if (STOPPED()) {
                return 0;
            }
        }
        if (!json_str(answer, "code", sM.code, sizeof(sM.code)) || !json_str(answer, "key", key, sizeof(key))) {
            set_state(-1, SDL_strstr(answer, "\"old\"") != NULL ? "This release is too old for online play through the matchmaking service. Get the newest release." :
                          SDL_strstr(answer, "busy") != NULL ? "Too many rooms from this address; try again in a few minutes." : "The matchmaking service gave no room.");
            return 0;
        }
        if (relayAddr[0] != '\0') { /* (a search begun again: the relay address it holds already goes with the new room) */
            SDL_snprintf(body, sizeof(body), "{\"code\":\"%s\",\"key\":\"%s\",\"addrs\":[%s,\"relay=%s\"]}", sM.code, key, addrs, relayAddr);
            http_post("addrs", body, answer, sizeof(answer));
        } else {
            /* A relay address, if the service gives access to one: published with the others, for a player who
               cannot reach this one directly. The code is shown only after this, so nobody joins before. */
            char server[128], user[160], pass[160], relay[64];
            if (SDL_getenv("BT3_RELAY_LOG") != NULL) {
                char why[160] = "";
                json_str(answer, "noRelay", why, sizeof(why));
                fprintf(stderr, "relay: the service %s%s\n", SDL_strstr(answer, "\"turn\"") != NULL ? "gave access" : "gave no access: ", why);
            }
            if (SDL_getenv("BT3_NO_RELAY") == NULL && json_str(answer, "server", server, sizeof(server)) && json_str(answer, "username", user, sizeof(user)) &&
                json_str(answer, "credential", pass, sizeof(pass)) && Relay_Allocate(sM.sock, server, user, pass, relay, sizeof(relay)) && !STOPPED()) {
                SDL_snprintf(body, sizeof(body), "{\"code\":\"%s\",\"key\":\"%s\",\"addrs\":[%s,\"relay=%s\"]}", sM.code, key, addrs, relay);
                http_post("addrs", body, answer, sizeof(answer));
                SDL_strlcpy(relayAddr, relay, sizeof(relayAddr));
            }
        }
        if (STOPPED()) {
            SDL_snprintf(body, sizeof(body), "{\"code\":\"%s\",\"key\":\"%s\"}", sM.code, key);
            http_post("leave", body, answer, sizeof(answer));
            return 0;
        }
        SDL_LockMutex(sM.lock);
        SDL_strlcpy(sM.st.code, sM.code, sizeof(sM.st.code)); /* (shown from here on) */
        sM.st.role = 1;
        sM.st.searches = searches;
        sM.st.state = 2;
        SDL_UnlockMutex(sM.lock);
        SDL_snprintf(body, sizeof(body), "{\"code\":\"%s\",\"key\":\"%s\"}", sM.code, key);
        for (i = 0; i < 165 && !STOPPED(); i++) { /* a room lasts ten minutes */
            int k;
            /* asked every two seconds in the first minute, every four after it (the service's daily requests) */
            for (k = 0; k < (i < 30 ? 20 : 40) && !STOPPED(); k++) {
                SDL_Delay(100);
            }
            if (STOPPED()) {
                break;
            }
            if (http_post("poll", body, answer, sizeof(answer))) {
                MatchStatus st;
                SDL_memset(&st, 0, sizeof(st));
                if (json_peer(answer, &st)) {
                    if (STOPPED()) {
                        break;
                    }
                    SDL_LockMutex(sM.lock);
                    SDL_strlcpy(sM.st.peerName, st.peerName, sizeof(sM.st.peerName));
                    SDL_memcpy(sM.st.peerAddr, st.peerAddr, sizeof(st.peerAddr));
                    sM.st.peerCount = st.peerCount;
                    sM.st.state = 3;
                    SDL_UnlockMutex(sM.lock);
                    http_post("leave", body, answer, sizeof(answer)); /* the room has done its work: it is given up at once */
                    return 0;
                }
                if (SDL_strstr(answer, "no room") != NULL) {
                    break;
                }
            }
        }
        if (STOPPED()) {
            http_post("leave", body, answer, sizeof(answer));
            return 0;
        }
        if (sM.role == 3) { /* (a quick match waits for as long as the player does) */
            http_post("leave", body, answer, sizeof(answer));
            goto again;
        }
        set_state(-1, "Nobody joined: the room code has run out. Host again for a new one.");
    } else {
        MatchStatus st;
        SDL_snprintf(body, sizeof(body), "{\"v\":%d,\"rel\":%d,\"code\":\"%s\",\"name\":\"%s\",\"addrs\":[%s]}", sM.version, PORT_RELEASE, sM.code, name, addrs);
        if (!http_post("join", body, answer, sizeof(answer))) {
            set_state(-1, "The matchmaking service could not be reached (is the network up?).");
            return 0;
        }
        (void)you;
        SDL_memset(&st, 0, sizeof(st));
        if (!json_peer(answer, &st)) {
            set_state(-1, SDL_strstr(answer, "\"old\"") != NULL ? "This release is too old for online play through the matchmaking service. Get the newest release." :
                          SDL_strstr(answer, "version") != NULL ? "The host's game is a different version: both players need the same release." :
                          SDL_strstr(answer, "full") != NULL ? "Someone has already joined that room." :
                          SDL_strstr(answer, "no room") != NULL ? "No room with that code (codes last ten minutes)." :
                          SDL_strstr(answer, "busy") != NULL ? "Too many wrong codes from this address; try again in an hour." : "The matchmaking service gave no answer.");
            return 0;
        }
        if (STOPPED()) {
            return 0;
        }
        SDL_LockMutex(sM.lock);
        SDL_strlcpy(sM.st.peerName, st.peerName, sizeof(sM.st.peerName));
        SDL_memcpy(sM.st.peerAddr, st.peerAddr, sizeof(st.peerAddr));
        sM.st.peerCount = st.peerCount;
        sM.st.state = 3;
        SDL_UnlockMutex(sM.lock);
    }
    return 0;
}

/* ---- for the lobby (net.c) ---- */

/* Starts looking: role 1 makes a room, role 2 joins room `code`. `sock` is the match's own UDP socket (not blocking),
   bound to `localPort`. */
void Match_Begin(int role, int sock, int localPort, const char *code, const char *name, int version) {
    int i, n = 0;

    if (sM.lock == NULL) {
        sM.lock = SDL_CreateMutex();
    }
    if (sM.thread != NULL) {
        SDL_DetachThread(sM.thread); /* (it stops by itself: its search number is no longer the current one) */
        sM.thread = NULL;
    }
    SDL_AddAtomicInt(&sM.gen, 1);
    SDL_SetAtomicInt(&sM.cancel, 0);
    SDL_memset(&sM.st, 0, sizeof(sM.st));
    sM.st.state = 1;
    sM.role = role;
    sM.sock = sock;
    sM.localPort = localPort;
    sM.version = version;
    sM.code[0] = '\0';
    for (i = 0; code != NULL && code[i] != '\0' && n < 6; i++) { /* letters and digits, capitals */
        char c = code[i];
        if (c >= 'a' && c <= 'z') {
            c = (char)(c - 'a' + 'A');
        }
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
            sM.code[n++] = c;
        }
    }
    sM.code[n] = '\0';
    SDL_strlcpy(sM.queue, role == 3 ? sM.code : "", sizeof(sM.queue));
    SDL_strlcpy(sM.name, name != NULL ? name : "Player", sizeof(sM.name));
    sM.thread = SDL_CreateThread(match_thread, "bt3-match", (void *)(intptr_t)SDL_GetAtomicInt(&sM.gen));
    if (sM.thread == NULL) {
        set_state(-1, "The matchmaking could not be started.");
    }
}

/* ---- how many wait in each quick-match queue (for the window): asked in a thread of its own, now and then ---- */
static SDL_AtomicInt sQueueCount[MATCH_QUEUES], sQueueBusy;
static int sQueueVersion;
const char *const kMatchQueues[MATCH_QUEUES] = {"S", "T", "D10", "D15", "D20"};

static int SDLCALL queues_thread(void *arg) {
    char body[64], answer[512];
    int i;

    (void)arg;
    SDL_snprintf(body, sizeof(body), "{\"v\":%d}", sQueueVersion);
    if (http_post("queues", body, answer, sizeof(answer)) && SDL_strstr(answer, "\"waiting\"") != NULL) {
        for (i = 0; i < MATCH_QUEUES; i++) {
            char key[16];
            const char *p;
            SDL_snprintf(key, sizeof(key), "\"%s\":", kMatchQueues[i]);
            p = SDL_strstr(answer, key);
            SDL_SetAtomicInt(&sQueueCount[i], p != NULL ? SDL_atoi(p + SDL_strlen(key)) + 1 : 0);
        }
    }
    SDL_SetAtomicInt(&sQueueBusy, 0);
    return 0;
}

/* Asks the service (nothing happens if an answer is still on its way). */
void Match_QueuesAsk(int version) {
    SDL_Thread *t;
    if (SDL_GetAtomicInt(&sQueueBusy)) {
        return;
    }
    SDL_SetAtomicInt(&sQueueBusy, 1);
    sQueueVersion = version;
    t = SDL_CreateThread(queues_thread, "bt3-queues", NULL);
    if (t == NULL) {
        SDL_SetAtomicInt(&sQueueBusy, 0);
    } else {
        SDL_DetachThread(t);
    }
}

/* Players waiting in queue i as last heard, -1 if not known. */
int Match_QueueWaiting(int i) {
    return i >= 0 && i < MATCH_QUEUES ? SDL_GetAtomicInt(&sQueueCount[i]) - 1 : -1;
}

/* Where it stands. */
void Match_Poll(MatchStatus *out) {
    if (sM.lock == NULL) {
        SDL_memset(out, 0, sizeof(*out));
        return;
    }
    SDL_LockMutex(sM.lock);
    *out = sM.st;
    SDL_UnlockMutex(sM.lock);
}

/* Stops looking (and, for a host, gives the room up). Does not wait for the thread: a request to the Worker that is
   under way ends by itself. */
void Match_Cancel(void) {
    if (sM.thread != NULL) {
        SDL_SetAtomicInt(&sM.cancel, 1);
        SDL_DetachThread(sM.thread);
        sM.thread = NULL;
    }
    if (sM.lock != NULL) {
        SDL_LockMutex(sM.lock);
        sM.st.state = 0;
        SDL_UnlockMutex(sM.lock);
    }
}
