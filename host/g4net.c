/*
 * g4net.c -- online play for the native Gauntlet 4 host: lockstep input exchange over TCP.
 *
 * Every machine runs the whole game; only the buttons travel. The C build is deterministic
 * (no wall clock, the Z80 and YM2612 emulated in fixed steps), so the same inputs on the
 * same frames give the same game everywhere.
 *
 * Star: the host is player 1; clients join as players 2-4 in the order they connect. A
 * player's buttons sampled on frame F are used on frame F + delay: each client sends them to
 * the host, the host collects every player's for a frame and sends the set to everyone. Nobody
 * runs a frame before its set is in. Frames 0 .. delay-1 have no buttons.
 *
 * Checks: the ROM (hash at join, mismatch refused) and, every 60 frames, a hash of 68000 RAM
 * and Z80 RAM, compared by the host; a difference is reported with the frame number.
 *
 * Seats: each player also has a seat (controller port 0-3 = colour: 1P red, 2P blue, 3P yellow,
 * 4P green), chosen in the lobby; the host is seat 0. Players are numbered 0..players-1 by when
 * they joined; their buttons go to their seat's controller (net_seat).
 *
 * Messages: 1 byte type + payload, integers big-endian.
 *   HELLO  c->h  'G4NP' version rom_hash seat
 *   WELCOME h->c player players delay teamplayer seat[4]   (sent when the last player is in)
 *   INPUT  c->h  frame buttons
 *   FRAME  h->c  frame b1 b2 b3 b4
 *   HASH   c->h  frame hash
 *   DESYNC h->c  frame
 *   REFUSE h->c  reason
 */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200112L                /* getaddrinfo under -std=c99 */
#endif
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET sock_t;
#define BADSOCK INVALID_SOCKET
#define sock_close closesocket
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <unistd.h>
typedef int sock_t;
#define BADSOCK (-1)
#define sock_close close
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "g4net.h"

enum { M_HELLO = 1, M_WELCOME, M_INPUT, M_FRAME, M_HASH, M_DESYNC, M_REFUSE,
       L_HELLO = 16, L_WANT, L_STATE, L_GO };       /* the lobby (lobby_*) */
enum { VERSION = 3, RING = 256 };       /* 2: extras (g4mods.c); 3: seats */

int net_active, net_player, net_players, net_delay = 4, net_teamplayer;
int net_seat[4] = {0, 1, 2, 3};               /* player -> seat (controller port) */
void (*net_idle)(void);                       /* called while waiting (window events) */

static sock_t peer[4];                        /* host: clients by player 1..3; client: peer[0] = host */
static struct { uint8_t buf[512]; int len; } rx[4];
static uint8_t inputs[RING][4];               /* buttons for frame % RING, per player */
static uint8_t have[RING];                    /* bit per player: present (host) / 0xFF = set in (client) */
static long have_frame[RING];
static struct { long frame; uint32_t hash[4]; uint8_t got, told; } hashes[16];   /* by player */

static void die(const char *msg) { fprintf(stderr, "net: %s\n", msg); exit(1); }

static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static uint32_t get32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

static int msg_len(uint8_t type)
{
    switch (type) {
    case M_HELLO: return 1 + 13;
    case M_WELCOME: return 1 + 8;
    case L_HELLO: return 1 + 12;
    case L_WANT: return 1 + 1;
    case L_STATE: return 1 + 5;
    case L_GO: return 1 + 3;
    case M_INPUT: return 1 + 5;
    case M_FRAME: return 1 + 8;
    case M_HASH: return 1 + 8;
    case M_DESYNC: return 1 + 4;
    case M_REFUSE: return 1 + 1;
    }
    return -1;
}

static void send_all(sock_t s, const uint8_t *p, int n)
{
    while (n > 0) {
        int k = (int)send(s, (const char *)p, n, 0);
        if (k <= 0) die("connection lost (send)");
        p += k; n -= k;
    }
}

static void sock_setup(sock_t s)
{
    int one = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof one);
}

/* wait up to ms for data on the open sockets; read what is there into rx[] */
static void pump(int ms)
{
    fd_set fds; struct timeval tv; int i; sock_t mx = 0;
    FD_ZERO(&fds);
    for (i = 0; i < 4; i++) if (peer[i] != BADSOCK) { FD_SET(peer[i], &fds); if (peer[i] > mx) mx = peer[i]; }
    tv.tv_sec = 0; tv.tv_usec = ms * 1000;
    if (select((int)mx + 1, &fds, NULL, NULL, &tv) <= 0) return;
    for (i = 0; i < 4; i++)
        if (peer[i] != BADSOCK && FD_ISSET(peer[i], &fds)) {
            int k = (int)recv(peer[i], (char *)rx[i].buf + rx[i].len, (int)sizeof rx[i].buf - rx[i].len, 0);
            if (k <= 0) {
                fprintf(stderr, "net: player %d disconnected\n", net_player == 0 ? i + 1 : 1);
                exit(1);
            }
            rx[i].len += k;
        }
}

/* next whole message from peer i, or 0 */
static int take(int i, uint8_t *m)
{
    int n;
    if (rx[i].len < 1) return 0;
    n = msg_len(rx[i].buf[0]);
    if (n < 0) die("bad message");
    if (rx[i].len < n) return 0;
    memcpy(m, rx[i].buf, (size_t)n);
    memmove(rx[i].buf, rx[i].buf + n, (size_t)(rx[i].len - n));
    rx[i].len -= n;
    return 1;
}

static void idle(void) { if (net_idle) net_idle(); }

/* ------------------------------------------------------------------ setup */
static void sockets_init(void)
{
    int i;
#ifdef _WIN32
    WSADATA w;
    if (WSAStartup(MAKEWORD(2, 2), &w) != 0) die("winsock");
#endif
    for (i = 0; i < 4; i++) peer[i] = BADSOCK;
}

void net_host(int port, int players, uint32_t rom_hash)
{
    sock_t ls; struct sockaddr_in a; int one = 1, joined = 1, confirmed = 0, i; uint8_t m[16], seat_of[4] = {0, 0, 0, 0};
    sockets_init();
    if (players < 2 || players > 4) die("--players must be 2-4");
    ls = socket(AF_INET, SOCK_STREAM, 0);
    if (ls == BADSOCK) die("socket");
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, sizeof one);
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_ANY); a.sin_port = htons((uint16_t)port);
    if (bind(ls, (struct sockaddr *)&a, sizeof a) != 0) die("cannot listen on that port (in use?)");
    listen(ls, 4);
    net_active = 1; net_player = 0; net_players = players;
    fprintf(stderr, "net: hosting on port %d, waiting for %d more player(s)\n", port, players - 1);
    while (joined < players) {
        fd_set fds; struct timeval tv = {0, 50000};
        FD_ZERO(&fds); FD_SET(ls, &fds);
        idle();
        if (select((int)ls + 1, &fds, NULL, NULL, &tv) > 0) {
            sock_t s = accept(ls, NULL, NULL);
            if (s == BADSOCK) continue;
            sock_setup(s);
            for (i = 1; i < 4 && peer[i] != BADSOCK; i++) {}
            if (i < 4) peer[i] = s; else sock_close(s);
        }
        /* a newly connected peer must say hello; players are numbered in the order confirmed */
        for (i = 1; i < 4; i++) {
            if (peer[i] == BADSOCK || (confirmed & (1 << i))) continue;
            pump(0);
            if (take(i, m)) {
                if (m[0] != M_HELLO || memcmp(m + 1, "G4NP", 4) || get32(m + 5) != VERSION) {
                    uint8_t r[2] = {M_REFUSE, 1}; send_all(peer[i], r, 2); sock_close(peer[i]); peer[i] = BADSOCK; continue;
                }
                if (get32(m + 9) != rom_hash) {
                    uint8_t r[2] = {M_REFUSE, 2};
                    fprintf(stderr, "net: refused a player with a different ROM\n");
                    send_all(peer[i], r, 2); sock_close(peer[i]); peer[i] = BADSOCK; continue;
                }
                confirmed |= 1 << i;
                seat_of[i] = m[13] >= 1 && m[13] <= 3 ? m[13] : 0;
                joined++;
                fprintf(stderr, "net: a player joined (%d of %d)\n", joined, players);
            }
        }
    }
    sock_close(ls);
    for (i = 1; i < 4; i++)                              /* connected but never said hello */
        if (peer[i] != BADSOCK && !(confirmed & (1 << i))) { sock_close(peer[i]); peer[i] = BADSOCK; rx[i].len = 0; }
    {   /* slots 1..players-1 in order, so peer[i] is player i + 1; seats as asked (else the first free) */
        int j = 1, used = 1, k;
        uint8_t seats[4] = {0, 0, 0, 0};
        for (i = 1; i < 4; i++) if (peer[i] != BADSOCK) {
            sock_t t = peer[i]; uint8_t want = seat_of[i];
            peer[i] = BADSOCK; peer[j] = t;
            if (!want || (used & (1 << want))) for (want = 1; want < 4 && (used & (1 << want)); want++) {}
            seats[j] = want; used |= 1 << want; j++;
        }
        net_teamplayer = 0;
        for (k = 0; k < players; k++) { net_seat[k] = seats[k]; if (seats[k] >= 2) net_teamplayer = 1; }
    }
    for (i = 1; i < players; i++) {
        uint8_t w[9] = {M_WELCOME, (uint8_t)i, (uint8_t)players, (uint8_t)net_delay, (uint8_t)net_teamplayer,
                        (uint8_t)net_seat[0], (uint8_t)net_seat[1], (uint8_t)net_seat[2], (uint8_t)net_seat[3]};
        send_all(peer[i], w, 9);
    }
    fprintf(stderr, "net: %d players, input delay %d frames%s -- starting\n", players, net_delay,
            net_teamplayer ? ", Team Player" : "");
}

static sock_t connect_to(const char *host, int port)
{
    struct addrinfo hints, *res, *r; char ps[16]; sock_t got = BADSOCK;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM;
    snprintf(ps, sizeof ps, "%d", port);
    if (getaddrinfo(host, ps, &hints, &res) != 0) return BADSOCK;
    for (r = res; r; r = r->ai_next) {
        sock_t s = socket(r->ai_family, r->ai_socktype, r->ai_protocol);
        if (s == BADSOCK) continue;
        if (connect(s, r->ai_addr, (int)r->ai_addrlen) == 0) { got = s; break; }
        sock_close(s);
    }
    freeaddrinfo(res);
    return got;
}
static void sleep_ms(int ms) { struct timeval tv; tv.tv_sec = 0; tv.tv_usec = ms * 1000; select(0, NULL, NULL, NULL, &tv); }
void net_join(const char *host, int port, uint32_t rom_hash, int seat)
{
    uint8_t m[16]; int tries;
    sockets_init();
    for (tries = 0; tries < 60 && peer[0] == BADSOCK; tries++) {   /* the host may still be restarting:
                                                                     keep trying for about 15 seconds */
        peer[0] = connect_to(host, port);
        if (peer[0] == BADSOCK) { idle(); sleep_ms(250); }
    }
    if (peer[0] == BADSOCK) die("cannot connect to the host");
    sock_setup(peer[0]);
    m[0] = M_HELLO; memcpy(m + 1, "G4NP", 4); put32(m + 5, VERSION); put32(m + 9, rom_hash); m[13] = (uint8_t)seat;
    send_all(peer[0], m, 14);
    fprintf(stderr, "net: connected, waiting for the other players\n");
    for (;;) {
        idle(); pump(50);
        if (take(0, m)) {
            if (m[0] == M_REFUSE) die(m[1] == 2 ? "the host has a different ROM" : "the host refused (version)");
            if (m[0] != M_WELCOME) die("unexpected message");
            net_player = m[1]; net_players = m[2]; net_delay = m[3]; net_teamplayer = m[4];
            net_seat[0] = m[5]; net_seat[1] = m[6]; net_seat[2] = m[7]; net_seat[3] = m[8];
            break;
        }
    }
    net_active = 1;
    fprintf(stderr, "net: you are player %d of %d, input delay %d frames -- starting\n",
            net_player + 1, net_players, net_delay);
}

/* ------------------------------------------------------------------ per frame */
/* host: hashes by player for a frame; any that differs from the host's own is a desync */
static void record_hash(long frame, uint32_t h, int player)
{
    int k = (int)((frame / 60) % 16), i;
    if (hashes[k].frame != frame) { hashes[k].frame = frame; hashes[k].got = 0; hashes[k].told = 0; }
    hashes[k].hash[player] = h; hashes[k].got |= (uint8_t)(1 << player);
    if (!(hashes[k].got & 1)) return;
    for (i = 1; i < net_players; i++)
        if ((hashes[k].got & (1 << i)) && !(hashes[k].told & (1 << i)) && hashes[k].hash[i] != hashes[k].hash[0]) {
            uint8_t d[5]; int j;
            hashes[k].told |= (uint8_t)(1 << i);
            fprintf(stderr, "net: DESYNC with player %d at frame %ld -- the games differ from here on\n", i + 1, frame);
            d[0] = M_DESYNC; put32(d + 1, (uint32_t)frame);
            for (j = 1; j < 4; j++) if (peer[j] != BADSOCK) send_all(peer[j], d, 5);
        }
}

static void host_messages(void)
{
    uint8_t m[16]; int i;
    for (i = 1; i < 4; i++)
        while (peer[i] != BADSOCK && take(i, m)) {
            if (m[0] == M_INPUT) {
                long f = (long)get32(m + 1); int k = (int)(f % RING);
                if (have_frame[k] != f) { have_frame[k] = f; have[k] = 0; memset(inputs[k], 0, 4); }
                inputs[k][i] = m[5]; have[k] |= (uint8_t)(1 << i);
            } else if (m[0] == M_HASH) record_hash((long)get32(m + 1), get32(m + 5), i);
        }
}

static void client_messages(void)
{
    uint8_t m[16];
    while (take(0, m)) {
        if (m[0] == M_FRAME) {
            long f = (long)get32(m + 1); int k = (int)(f % RING);
            have_frame[k] = f; have[k] = 0xFF; memcpy(inputs[k], m + 5, 4);
        } else if (m[0] == M_DESYNC) fprintf(stderr, "net: DESYNC reported at frame %ld\n", (long)get32(m + 1));
    }
}

void net_frame(long frame, uint8_t local, uint8_t held[4])
{
    long target = frame + net_delay; int k, i, all = (1 << net_players) - 1;
    if (net_player == 0) {
        k = (int)(target % RING);
        if (have_frame[k] != target) { have_frame[k] = target; have[k] = 0; memset(inputs[k], 0, 4); }
        inputs[k][0] = local; have[k] |= 1;
    } else {
        uint8_t m[6]; m[0] = M_INPUT; put32(m + 1, (uint32_t)target); m[5] = local;
        send_all(peer[0], m, 6);
    }
    memset(held, 0, 4);
    if (frame < net_delay) return;                       /* the first frames have no buttons */
    k = (int)(frame % RING);
    for (;;) {
        if (net_player == 0) {
            host_messages();
            if (have_frame[k] == frame && (have[k] & all) == all) {
                uint8_t m[9]; m[0] = M_FRAME; put32(m + 1, (uint32_t)frame); memcpy(m + 5, inputs[k], 4);
                for (i = 1; i < 4; i++) if (peer[i] != BADSOCK) send_all(peer[i], m, 9);
                break;
            }
        } else {
            client_messages();
            if (have_frame[k] == frame && have[k] == 0xFF) break;
        }
        idle();
        pump(2);
    }
    for (i = 0; i < net_players; i++) held[net_seat[i] & 3] = inputs[k][i];   /* each player's buttons on their seat */
}

void net_hash(long frame, uint32_t h)
{
    if (net_player == 0) { host_messages(); record_hash(frame, h, 0); }
    else { uint8_t m[9]; m[0] = M_HASH; put32(m + 1, (uint32_t)frame); put32(m + 5, h); send_all(peer[0], m, 9); }
}

/* ------------------------------------------------------------------ the lobby (in-game ONLINE menu)
   Before the online game: the host listens while the game's menu keeps running (nothing here
   blocks), joiners connect and pick a free seat (colour); when the host starts, each joiner gets
   GO with its seat and the player count, and every copy restarts the program into net_host /
   net_join (g4host.c host_relaunch). Lobby messages: L_HELLO c->h 'G4LB' version rom_hash;
   L_WANT c->h seat; L_STATE h->c your-seat occupied-mask 0 0 0; L_GO h->c seat players delay;
   M_REFUSE h->c reason. */
static sock_t lobby_ls = BADSOCK;
static int lobby_host_side, lobby_ok[4], lobby_seat_of[4], lobby_my_seat, lobby_mask, lobby_status;
static uint32_t lobby_hash;
static int lobby_recv(int i)                    /* 0 = the peer is gone */
{
    fd_set fds; struct timeval tv = {0, 0}; int k;
    FD_ZERO(&fds); FD_SET(peer[i], &fds);
    if (select((int)peer[i] + 1, &fds, NULL, NULL, &tv) <= 0) return 1;
    k = (int)recv(peer[i], (char *)rx[i].buf + rx[i].len, (int)sizeof rx[i].buf - rx[i].len, 0);
    if (k <= 0) return 0;
    rx[i].len += k;
    return 1;
}
static int lobby_send(sock_t s, const uint8_t *p, int n)   /* 0 = failed */
{
    while (n > 0) { int k = (int)send(s, (const char *)p, n, 0); if (k <= 0) return 0; p += k; n -= k; }
    return 1;
}
static void lobby_drop(int i) { if (peer[i] != BADSOCK) sock_close(peer[i]); peer[i] = BADSOCK; rx[i].len = 0; lobby_ok[i] = 0; lobby_seat_of[i] = 0; }
static void lobby_broadcast(void)
{
    int i; lobby_mask = 1;
    for (i = 1; i < 4; i++) if (lobby_ok[i]) lobby_mask |= 1 << lobby_seat_of[i];
    for (i = 1; i < 4; i++) if (lobby_ok[i]) {
        uint8_t m[6] = {L_STATE, (uint8_t)lobby_seat_of[i], (uint8_t)lobby_mask, 0, 0, 0};
        if (!lobby_send(peer[i], m, 6)) lobby_drop(i);
    }
}
int lobby_host_open(int port, uint32_t rom_hash)       /* 1 = listening */
{
    struct sockaddr_in a; int one = 1, i;
    sockets_init();
    lobby_ls = socket(AF_INET, SOCK_STREAM, 0);
    if (lobby_ls == BADSOCK) return 0;
    setsockopt(lobby_ls, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, sizeof one);
    memset(&a, 0, sizeof a); a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_ANY); a.sin_port = htons((uint16_t)port);
    if (bind(lobby_ls, (struct sockaddr *)&a, sizeof a) != 0) { sock_close(lobby_ls); lobby_ls = BADSOCK; return 0; }
    listen(lobby_ls, 4);
    lobby_host_side = 1; lobby_hash = rom_hash; lobby_mask = 1; lobby_status = 0;
    for (i = 0; i < 4; i++) { lobby_ok[i] = 0; lobby_seat_of[i] = 0; }
    return 1;
}
int lobby_join_open(const char *host, int port, uint32_t rom_hash)   /* 1 = connected */
{
    uint8_t m[13];
    sockets_init();
    peer[0] = connect_to(host, port);
    if (peer[0] == BADSOCK) return 0;
    sock_setup(peer[0]);
    lobby_host_side = 0; lobby_my_seat = 0; lobby_mask = 0; lobby_status = 0;
    m[0] = L_HELLO; memcpy(m + 1, "G4LB", 4); put32(m + 5, VERSION); put32(m + 9, rom_hash);
    if (!lobby_send(peer[0], m, 13)) { lobby_drop(0); return 0; }
    return 1;
}
/* once a frame. Returns: 0 waiting, 1 GO (joiner), -1 the host refused (rom), -2 connection lost */
int lobby_poll(void)
{
    uint8_t m[16]; int i;
    if (lobby_host_side) {
        int changed = 0;
        if (lobby_ls != BADSOCK) {
            fd_set fds; struct timeval tv = {0, 0};
            FD_ZERO(&fds); FD_SET(lobby_ls, &fds);
            if (select((int)lobby_ls + 1, &fds, NULL, NULL, &tv) > 0) {
                sock_t s = accept(lobby_ls, NULL, NULL);
                if (s != BADSOCK) {
                    sock_setup(s);
                    for (i = 1; i < 4 && peer[i] != BADSOCK; i++) {}
                    if (i < 4) { peer[i] = s; rx[i].len = 0; lobby_ok[i] = 0; } else sock_close(s);
                }
            }
        }
        for (i = 1; i < 4; i++) {
            if (peer[i] == BADSOCK) continue;
            if (!lobby_recv(i)) { lobby_drop(i); changed = 1; continue; }
            while (peer[i] != BADSOCK && rx[i].len >= 1) {
                int n = msg_len(rx[i].buf[0]);
                if (n < 0) { lobby_drop(i); changed = 1; break; }
                if (rx[i].len < n) break;
                memcpy(m, rx[i].buf, (size_t)n); memmove(rx[i].buf, rx[i].buf + n, (size_t)(rx[i].len - n)); rx[i].len -= n;
                if (m[0] == L_HELLO && !lobby_ok[i]) {
                    if (memcmp(m + 1, "G4LB", 4) || get32(m + 5) != VERSION || get32(m + 9) != lobby_hash) {
                        uint8_t r[2] = {M_REFUSE, 2}; lobby_send(peer[i], r, 2); lobby_drop(i); changed = 1; break;
                    }
                    {   int s2, used = 1, j;
                        for (j = 1; j < 4; j++) if (lobby_ok[j]) used |= 1 << lobby_seat_of[j];
                        for (s2 = 1; s2 < 4 && (used & (1 << s2)); s2++) {}
                        if (s2 == 4) { uint8_t r[2] = {M_REFUSE, 3}; lobby_send(peer[i], r, 2); lobby_drop(i); changed = 1; break; }
                        lobby_ok[i] = 1; lobby_seat_of[i] = s2; changed = 1;
                    }
                } else if (m[0] == L_WANT && lobby_ok[i] && m[1] >= 1 && m[1] <= 3) {
                    int j, free = 1;
                    for (j = 1; j < 4; j++) if (j != i && lobby_ok[j] && lobby_seat_of[j] == m[1]) free = 0;
                    if (free) { lobby_seat_of[i] = m[1]; }
                    changed = 1;                       /* (tell them either way) */
                }
            }
        }
        if (changed) lobby_broadcast();
        return 0;
    }
    if (peer[0] == BADSOCK) return -2;
    if (!lobby_recv(0)) { lobby_drop(0); return -2; }
    while (rx[0].len >= 1) {
        int n = msg_len(rx[0].buf[0]);
        if (n < 0) { lobby_drop(0); return -2; }
        if (rx[0].len < n) break;
        memcpy(m, rx[0].buf, (size_t)n); memmove(rx[0].buf, rx[0].buf + n, (size_t)(rx[0].len - n)); rx[0].len -= n;
        if (m[0] == M_REFUSE) { lobby_drop(0); return m[1] == 3 ? -3 : -1; }
        if (m[0] == L_STATE) { lobby_my_seat = m[1]; lobby_mask = m[2]; }
        if (m[0] == L_GO) { lobby_my_seat = m[1]; lobby_status = m[2]; lobby_drop(0); return 1; }   /* status = players */
    }
    return 0;
}
void lobby_want(int seat) { uint8_t m[2] = {L_WANT, (uint8_t)seat}; if (peer[0] != BADSOCK && !lobby_send(peer[0], m, 2)) lobby_drop(0); }
int lobby_go(void)                                 /* host: tell everyone; returns the player count */
{
    int i, players = 1;
    for (i = 1; i < 4; i++) if (lobby_ok[i]) players++;
    for (i = 1; i < 4; i++) if (lobby_ok[i]) {
        uint8_t m[4] = {L_GO, (uint8_t)lobby_seat_of[i], (uint8_t)players, (uint8_t)net_delay};
        lobby_send(peer[i], m, 4);
    }
    return players;
}
void lobby_close(void)
{
    int i;
    for (i = 0; i < 4; i++) lobby_drop(i);
    if (lobby_ls != BADSOCK) { sock_close(lobby_ls); lobby_ls = BADSOCK; }
}
int lobby_seats(void) { return lobby_mask; }       /* occupied seats (bit per seat) */
int lobby_my(void) { return lobby_my_seat; }
int lobby_players(void) { return lobby_status; }
