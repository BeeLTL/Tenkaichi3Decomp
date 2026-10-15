/* The native transport of online play (net_io.h): the UDP socket calls net.c used to make itself, moved here
   unchanged. Not in the browser build (port/tools/wasm_build.py WASM_SKIP). */
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef int socklen_t;
#else
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#define closesocket close
#endif
#include "net_io.h"

int NetIo_Open(void) {
    int s;
#ifdef _WIN32
    WSADATA w;
    u_long on = 1;
    WSAStartup(MAKEWORD(2, 2), &w);
    s = (int)socket(AF_INET, SOCK_DGRAM, 0);
    ioctlsocket(s, FIONBIO, &on);
#else
    s = socket(AF_INET, SOCK_DGRAM, 0);
    fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
#endif
    return s;
}

int NetIo_Bind(int h, const struct sockaddr_in *local) {
    return bind(h, (const struct sockaddr *)local, sizeof(*local));
}

int NetIo_LocalAddr(int h, struct sockaddr_in *out) {
    socklen_t len = sizeof(*out);
    return getsockname(h, (struct sockaddr *)out, &len);
}

int NetIo_SendTo(int h, const void *buf, int n, const struct sockaddr_in *to) {
    return (int)sendto(h, (const char *)buf, n, 0, (const struct sockaddr *)to, sizeof(*to));
}

int NetIo_RecvFrom(int h, void *buf, int size, struct sockaddr_in *from) {
    socklen_t fromLen = sizeof(*from);
    return (int)recvfrom(h, (char *)buf, size, 0, (struct sockaddr *)from, &fromLen);
}

void NetIo_Close(int h) {
    closesocket(h);
}
