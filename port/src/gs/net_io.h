/* The transport of online play (net.c): its UDP socket behind a few calls, so that another build can put another
   transport behind them (the browser: docs/BROWSER_ONLINE_PLAN.md).

   Natively (net_io.c) each call is the socket call net.c made itself, with the same arguments and the same results:
   the handle IS the socket. net_match.c (STUN, asking for a relay address) and net_relay.c (TURN) keep using that
   socket directly, on purpose: they must speak from the same local port as the match, or the routers on the way
   would not take it for the same conversation. */
#ifndef NET_IO_H
#define NET_IO_H

struct sockaddr_in;

/* A UDP socket that does not block (Windows: WSAStartup first, every time, as before). The handle, or -1. */
int NetIo_Open(void);
/* bind(): 0 = done. */
int NetIo_Bind(int h, const struct sockaddr_in *local);
/* getsockname(): the local address and port the socket has. 0 = done. */
int NetIo_LocalAddr(int h, struct sockaddr_in *out);
/* sendto(): the bytes sent (n when all went), or < 0. */
int NetIo_SendTo(int h, const void *buf, int n, const struct sockaddr_in *to);
/* recvfrom(), not blocking: the bytes of the next packet and who sent it, or <= 0 when there is none. */
int NetIo_RecvFrom(int h, void *buf, int size, struct sockaddr_in *from);
/* closesocket() / close(). */
void NetIo_Close(int h);

#endif
