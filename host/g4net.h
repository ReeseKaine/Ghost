/* g4net.h -- online play (lockstep input exchange), see g4net.c */
#ifndef G4NET_H
#define G4NET_H
#include <stdint.h>
extern int net_active, net_player, net_players, net_delay, net_teamplayer, net_seat[4];
extern void (*net_idle)(void);
void net_host(int port, int players, uint32_t rom_hash);    /* blocks until everyone has joined */
void net_join(const char *host, int port, uint32_t rom_hash, int seat);   /* seat 1-3 asked for (0 any) */
void net_frame(long frame, uint8_t local, uint8_t held[4]); /* send ours, wait for everyone's */
void net_hash(long frame, uint32_t h);                      /* every 60 frames: state check */
/* the lobby (g4net.c) */
int  lobby_host_open(int port, uint32_t rom_hash);
int  lobby_join_open(const char *host, int port, uint32_t rom_hash);
int  lobby_poll(void);             /* 0 waiting, 1 GO (joiner), -1 refused (ROM), -2 lost, -3 full */
void lobby_want(int seat);
int  lobby_go(void);               /* host: returns the players */
void lobby_close(void);
int  lobby_seats(void), lobby_my(void), lobby_players(void);
#endif
