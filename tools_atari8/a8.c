/* a8.c -- tiny Atari 8-bit stand-in: a 6502 + just enough OS (boot, SIO disk reads, vblank) to
   watch Gauntlet's own loader. Reads an ATR given on the command line (data only).
   Usage: a8 disk1.atr [disk2.atr] [options]
     -n N        run N million instructions (default 50)
     -swap K     after the K-th disk read past boot... (manual: 'swap' when it reads sector S of drive)
     -dump F     write the 64K RAM to F at the end
     -log        log every disk read
     -trace A    print registers each time PC == A (hex)
     -start M    hold START from M million instructions on (for 0.2M)
     -key M:C    press key code C at M million */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static uint8_t mem[65536];
static uint8_t *disk[2]; static long disk_size[2]; static int cur_disk;
static uint8_t A, X, Y, S = 0xFF, P = 0x24; static uint16_t PC;
static long icount; static int logsio, nreads;
static long start_at = -1, key_at = -1, key_every = 0; static int key_code;
static int key_down(long t) { if (key_at < 0 || t < key_at) return 0; if (key_every) return (t - key_at) % key_every < 200000; return t < key_at + 200000; }
static uint8_t vcount;
static long trace_pc[16]; static int ntrace;
static long swap_after = -1;
static long wfirst; static unsigned wlo, whi; static long wlog_from = -1, wlog_to = 1L<<62; static uint32_t wpc[65536]; static uint8_t wpage_pc_seen[256]; static uint16_t cur_op_pc; static uint32_t wpage[256];

enum { FC = 1, FZ = 2, FI = 4, FD = 8, FB = 16, FU = 32, FV = 64, FN = 128 };

static uint8_t rd(uint16_t a)
{
    if (a >= 0xD000 && a < 0xD800) {
        switch (a & 0xFF1F) {
        case 0xD01F: return (start_at >= 0 && icount >= start_at && icount < start_at + 200000) ? 6 : 7;   /* CONSOL */
        case 0xD010: return key_down(icount) ? 0 : 1; case 0xD011: return 1;   /* TRIG */
        case 0xD014: return 1;                        /* PAL */
        }
        if ((a & 0xFF00) == 0xD200) {
            switch (a & 0x0F) {
            case 0x0A: return (uint8_t)rand();        /* RANDOM */
            case 0x09: return key_down(icount) ? (uint8_t)key_code : 0xFF;
            case 0x0F: return key_down(icount) ? 0xFB : 0xFF;   /* SKSTAT */
            case 0x0E: return 0xFF;                   /* IRQST: nothing pending */
            }
            return 0xFF;
        }
        if ((a & 0xFF00) == 0xD300) { if ((a & 3) == 0) return 0xFF; return mem[a]; }   /* PIA: sticks centred */
        if ((a & 0xFF00) == 0xD400) { if ((a & 0x0F) == 0x0B) return vcount; return 0; }
        if ((a & 0xFF00) == 0xD000) return 0;
    }
    return mem[a];
}
static void wr(uint16_t a, uint8_t v)
{
    if (icount >= wlog_from && icount < wlog_to && (!wlo || (a >= wlo && a < whi))) { wpc[cur_op_pc]++; wpage[a >> 8]++; if (!wfirst) { wfirst = icount; fprintf(stderr, "first write in range at %ld by %04X to %04X\n", icount, cur_op_pc, a); } }
    if (a >= 0xD000 && a < 0xD800) { if ((a & 0xFF00) == 0xD300) mem[a] = v; return; }
    if (a >= 0xD800 && getenv("A8ROMPROT")) return;
    mem[a] = v;
}
static uint16_t rd16(uint16_t a) { return (uint16_t)(rd(a) | rd((uint16_t)(a + 1)) << 8); }
static void push(uint8_t v) { mem[0x100 + S--] = v; }
static uint8_t pull(void) { return mem[0x100 + ++S]; }
static void nz(uint8_t v) { P = (uint8_t)((P & ~(FN | FZ)) | (v & 0x80) | (v ? 0 : FZ)); }

static int read_sector(int drive, int sec, uint8_t *buf)
{
    long off; int d = cur_disk;
    (void)drive;
    if (!disk[d] || sec < 1) return 0;
    off = 16 + 128L * (sec - 1);
    if (off + 128 > disk_size[d]) return 0;
    memcpy(buf, disk[d] + off, 128);
    nreads++;
    if (swap_after >= 0 && nreads == swap_after && disk[1]) { cur_disk = 1; fprintf(stderr, "[swap to disk 2]\n"); }
    return 1;
}
static void siov(void)                                /* the DCB at $0300 */
{
    uint8_t dev = mem[0x300], unit = mem[0x301], cmd = mem[0x302];
    uint16_t buf = (uint16_t)(mem[0x304] | mem[0x305] << 8), len = (uint16_t)(mem[0x308] | mem[0x309] << 8);
    int sec = mem[0x30A] | mem[0x30B] << 8; uint8_t tmp[128]; int i;
    if (dev != 0x31) { fprintf(stderr, "SIO dev %02X cmd %02X\n", dev, cmd); mem[0x303] = 1; Y = 1; nz(1); return; }
    if (cmd == 0x52) {
        int ok = read_sector(unit, sec, tmp);
        if (logsio) fprintf(stderr, "%8ld read D%d sector %3d -> %04X (%d) [disk %d] PC %04X\n", icount, unit, sec, buf, len, cur_disk + 1, rd16((uint16_t)(0x101 + S)) + 1);
        for (i = 0; i < len && i < 128; i++) mem[(uint16_t)(buf + i)] = tmp[i];
        mem[0x303] = ok ? 1 : 0x90; Y = mem[0x303];
    } else if (cmd == 0x53) {                         /* status */
        for (i = 0; i < len; i++) mem[(uint16_t)(buf + i)] = i == 0 ? 0x10 : (i == 1 ? 0xFF : 0xE0);
        mem[0x303] = 1; Y = 1;
    } else { if (logsio) fprintf(stderr, "SIO cmd %02X sector %d\n", cmd, sec); mem[0x303] = 1; Y = 1; }
    nz(Y); P = (uint8_t)(Y & 0x80 ? P | FN : P & ~FN);
}
static void nmi_vbl(void)
{
    push((uint8_t)(PC >> 8)); push((uint8_t)PC); push((uint8_t)(P & ~FB));
    P |= FI;
    push(A); push(X); push(Y);                         /* as the OS VBI does before VVBLKI */
    if (!++mem[0x14]) if (!++mem[0x13]) ++mem[0x12];   /* RTCLOK */
    mem[0x284] = key_down(icount) ? 0 : 1; mem[0x285] = 1; mem[0x278] = 15; mem[0x279] = 15;   /* STRIG/STICK shadows */
    mem[0x2FC] = key_down(icount) ? (uint8_t)key_code : 0xFF;
    PC = rd16(0x222);
}
static int os_trap(void)                              /* OS entry points we stand in for */
{
    switch (PC) {
    case 0xE453: case 0xE459:                          /* DSKINV / SIOV */
        if (PC == 0xE453) { mem[0x300] = 0x31; if (!mem[0x301]) mem[0x301] = 1; if (mem[0x302] == 0x52 || mem[0x302] == 0x53) mem[0x303] = 0x40; mem[0x308] = 128; mem[0x309] = 0; }
        siov();
        PC = (uint16_t)(pull() | pull() << 8); PC++;
        return 1;
    case 0xE45C:                                       /* SETVBV: A = 6 immediate / 7 deferred, Y lo X hi */
        if (A == 6) { mem[0x222] = Y; mem[0x223] = X; } else if (A == 7) { mem[0x224] = Y; mem[0x225] = X; }
        else { mem[0x218 + 2 * (A - 1)] = Y; mem[0x219 + 2 * (A - 1)] = X; }
        PC = (uint16_t)(pull() | pull() << 8); PC++;
        return 1;
    case 0xE45F: PC = rd16(0x224); return 1;          /* SYSVBV -> deferred */
    case 0xE462: Y = pull(); X = pull(); A = pull(); P = (uint8_t)((pull() & ~FB) | FU); PC = (uint16_t)(pull() | pull() << 8); return 1;
    case 0xE456: fprintf(stderr, "CIOV called (IOCB %02X cmd %02X)\n", X, mem[0x342 + X]); Y = 1; PC = (uint16_t)(pull() | pull() << 8); PC++; return 1;
    case 0xE465: case 0xE468: case 0xE46B: case 0xE46E: case 0xE471: case 0xE474: case 0xE477:
        fprintf(stderr, "OS vector %04X called\n", PC); PC = (uint16_t)(pull() | pull() << 8); PC++; return 1;
    }
    return 0;
}

static uint16_t ea(int mode)
{
    uint16_t a, b;
    switch (mode) {
    case 0: return PC++;                                         /* imm */
    case 1: return rd(PC++);                                     /* zp */
    case 2: return (uint8_t)(rd(PC++) + X);                      /* zp,x */
    case 3: return (uint8_t)(rd(PC++) + Y);                      /* zp,y */
    case 4: a = rd16(PC); PC += 2; return a;                     /* abs */
    case 5: a = rd16(PC); PC += 2; return (uint16_t)(a + X);     /* abs,x */
    case 6: a = rd16(PC); PC += 2; return (uint16_t)(a + Y);     /* abs,y */
    case 7: b = (uint8_t)(rd(PC++) + X); return (uint16_t)(mem[b] | mem[(uint8_t)(b + 1)] << 8);   /* (zp,x) */
    case 8: b = rd(PC++); return (uint16_t)((mem[b] | mem[(uint8_t)(b + 1)] << 8) + Y);           /* (zp),y */
    }
    return 0;
}
static void adc(uint8_t v)
{
    int c = P & FC;
    if (P & FD) {
        int lo = (A & 15) + (v & 15) + c, hi = (A >> 4) + (v >> 4), r;
        if (lo > 9) { lo += 6; hi++; }
        r = (uint8_t)(A + v + c);
        P = (uint8_t)((P & ~(FZ | FN | FV | FC)) | (r ? 0 : FZ) | ((hi << 4) & 0x80) | ((~(A ^ v) & (A ^ (hi << 4)) & 0x80) ? FV : 0));
        if (hi > 9) hi += 6;
        if (hi > 15) P |= FC;
        A = (uint8_t)((hi << 4) | (lo & 15));
    } else {
        int r = A + v + c;
        P = (uint8_t)((P & ~(FC | FV)) | (r > 255 ? FC : 0) | ((~(A ^ v) & (A ^ r) & 0x80) ? FV : 0));
        A = (uint8_t)r; nz(A);
    }
}
static void sbc(uint8_t v)
{
    if (P & FD) {
        int c = (P & FC) ? 0 : 1, r = A - v - c;
        int lo = (A & 15) - (v & 15) - c, hi = (A >> 4) - (v >> 4);
        P = (uint8_t)((P & ~(FC | FV)) | (r >= 0 ? FC : 0) | (((A ^ v) & (A ^ r) & 0x80) ? FV : 0));
        if (lo < 0) { lo -= 6; hi--; }
        if (hi < 0) hi -= 6;
        nz((uint8_t)r);
        A = (uint8_t)((hi << 4) | (lo & 15));
    } else adc((uint8_t)~v);
}
static void cmp(uint8_t r, uint8_t v) { int t = r - v; P = (uint8_t)((P & ~FC) | (t >= 0 ? FC : 0)); nz((uint8_t)t); }
static void branch(int cond) { int8_t o = (int8_t)rd(PC++); if (cond) PC = (uint16_t)(PC + o); }

static int step(void)
{
    uint8_t op, v; uint16_t a;
    if (os_trap()) return 1;
    for (int i = 0; i < ntrace; i++) if (PC == trace_pc[i]) fprintf(stderr, "%8ld PC %04X A %02X X %02X Y %02X S %02X P %02X\n", icount, PC, A, X, Y, S, P);
    cur_op_pc = PC;
    op = rd(PC++);
    switch (op) {
    /* loads/stores */
    case 0xA9: A = rd(ea(0)); nz(A); break; case 0xA5: A = rd(ea(1)); nz(A); break; case 0xB5: A = rd(ea(2)); nz(A); break;
    case 0xAD: A = rd(ea(4)); nz(A); break; case 0xBD: A = rd(ea(5)); nz(A); break; case 0xB9: A = rd(ea(6)); nz(A); break;
    case 0xA1: A = rd(ea(7)); nz(A); break; case 0xB1: A = rd(ea(8)); nz(A); break;
    case 0xA2: X = rd(ea(0)); nz(X); break; case 0xA6: X = rd(ea(1)); nz(X); break; case 0xB6: X = rd(ea(3)); nz(X); break;
    case 0xAE: X = rd(ea(4)); nz(X); break; case 0xBE: X = rd(ea(6)); nz(X); break;
    case 0xA0: Y = rd(ea(0)); nz(Y); break; case 0xA4: Y = rd(ea(1)); nz(Y); break; case 0xB4: Y = rd(ea(2)); nz(Y); break;
    case 0xAC: Y = rd(ea(4)); nz(Y); break; case 0xBC: Y = rd(ea(5)); nz(Y); break;
    case 0x85: wr(ea(1), A); break; case 0x95: wr(ea(2), A); break; case 0x8D: wr(ea(4), A); break;
    case 0x9D: wr(ea(5), A); break; case 0x99: wr(ea(6), A); break; case 0x81: wr(ea(7), A); break; case 0x91: wr(ea(8), A); break;
    case 0x86: wr(ea(1), X); break; case 0x96: wr(ea(3), X); break; case 0x8E: wr(ea(4), X); break;
    case 0x84: wr(ea(1), Y); break; case 0x94: wr(ea(2), Y); break; case 0x8C: wr(ea(4), Y); break;
    /* transfers */
    case 0xAA: X = A; nz(X); break; case 0xA8: Y = A; nz(Y); break; case 0x8A: A = X; nz(A); break; case 0x98: A = Y; nz(A); break;
    case 0xBA: X = S; nz(X); break; case 0x9A: S = X; break;
    case 0x48: push(A); break; case 0x68: A = pull(); nz(A); break; case 0x08: push(P | FB | FU); break; case 0x28: P = (uint8_t)(pull() | FU); break;
    /* arithmetic */
#define ALU(base, fn) case base+0x09-0x09+0x09: fn(rd(ea(0))); break; case base+0x05: fn(rd(ea(1))); break; case base+0x15: fn(rd(ea(2))); break; \
    case base+0x0D: fn(rd(ea(4))); break; case base+0x1D: fn(rd(ea(5))); break; case base+0x19: fn(rd(ea(6))); break; \
    case base+0x01: fn(rd(ea(7))); break; case base+0x11: fn(rd(ea(8))); break;
    case 0x69: adc(rd(ea(0))); break; case 0x65: adc(rd(ea(1))); break; case 0x75: adc(rd(ea(2))); break; case 0x6D: adc(rd(ea(4))); break;
    case 0x7D: adc(rd(ea(5))); break; case 0x79: adc(rd(ea(6))); break; case 0x61: adc(rd(ea(7))); break; case 0x71: adc(rd(ea(8))); break;
    case 0xE9: sbc(rd(ea(0))); break; case 0xE5: sbc(rd(ea(1))); break; case 0xF5: sbc(rd(ea(2))); break; case 0xED: sbc(rd(ea(4))); break;
    case 0xFD: sbc(rd(ea(5))); break; case 0xF9: sbc(rd(ea(6))); break; case 0xE1: sbc(rd(ea(7))); break; case 0xF1: sbc(rd(ea(8))); break;
    case 0x29: A &= rd(ea(0)); nz(A); break; case 0x25: A &= rd(ea(1)); nz(A); break; case 0x35: A &= rd(ea(2)); nz(A); break; case 0x2D: A &= rd(ea(4)); nz(A); break;
    case 0x3D: A &= rd(ea(5)); nz(A); break; case 0x39: A &= rd(ea(6)); nz(A); break; case 0x21: A &= rd(ea(7)); nz(A); break; case 0x31: A &= rd(ea(8)); nz(A); break;
    case 0x09: A |= rd(ea(0)); nz(A); break; case 0x05: A |= rd(ea(1)); nz(A); break; case 0x15: A |= rd(ea(2)); nz(A); break; case 0x0D: A |= rd(ea(4)); nz(A); break;
    case 0x1D: A |= rd(ea(5)); nz(A); break; case 0x19: A |= rd(ea(6)); nz(A); break; case 0x01: A |= rd(ea(7)); nz(A); break; case 0x11: A |= rd(ea(8)); nz(A); break;
    case 0x49: A ^= rd(ea(0)); nz(A); break; case 0x45: A ^= rd(ea(1)); nz(A); break; case 0x55: A ^= rd(ea(2)); nz(A); break; case 0x4D: A ^= rd(ea(4)); nz(A); break;
    case 0x5D: A ^= rd(ea(5)); nz(A); break; case 0x59: A ^= rd(ea(6)); nz(A); break; case 0x41: A ^= rd(ea(7)); nz(A); break; case 0x51: A ^= rd(ea(8)); nz(A); break;
    case 0xC9: cmp(A, rd(ea(0))); break; case 0xC5: cmp(A, rd(ea(1))); break; case 0xD5: cmp(A, rd(ea(2))); break; case 0xCD: cmp(A, rd(ea(4))); break;
    case 0xDD: cmp(A, rd(ea(5))); break; case 0xD9: cmp(A, rd(ea(6))); break; case 0xC1: cmp(A, rd(ea(7))); break; case 0xD1: cmp(A, rd(ea(8))); break;
    case 0xE0: cmp(X, rd(ea(0))); break; case 0xE4: cmp(X, rd(ea(1))); break; case 0xEC: cmp(X, rd(ea(4))); break;
    case 0xC0: cmp(Y, rd(ea(0))); break; case 0xC4: cmp(Y, rd(ea(1))); break; case 0xCC: cmp(Y, rd(ea(4))); break;
    case 0x24: v = rd(ea(1)); P = (uint8_t)((P & ~(FN | FV | FZ)) | (v & 0xC0) | ((A & v) ? 0 : FZ)); break;
    case 0x2C: v = rd(ea(4)); P = (uint8_t)((P & ~(FN | FV | FZ)) | (v & 0xC0) | ((A & v) ? 0 : FZ)); break;
    /* inc/dec */
    case 0xE6: a = ea(1); v = (uint8_t)(rd(a) + 1); wr(a, v); nz(v); break; case 0xF6: a = ea(2); v = (uint8_t)(rd(a) + 1); wr(a, v); nz(v); break;
    case 0xEE: a = ea(4); v = (uint8_t)(rd(a) + 1); wr(a, v); nz(v); break; case 0xFE: a = ea(5); v = (uint8_t)(rd(a) + 1); wr(a, v); nz(v); break;
    case 0xC6: a = ea(1); v = (uint8_t)(rd(a) - 1); wr(a, v); nz(v); break; case 0xD6: a = ea(2); v = (uint8_t)(rd(a) - 1); wr(a, v); nz(v); break;
    case 0xCE: a = ea(4); v = (uint8_t)(rd(a) - 1); wr(a, v); nz(v); break; case 0xDE: a = ea(5); v = (uint8_t)(rd(a) - 1); wr(a, v); nz(v); break;
    case 0xE8: X++; nz(X); break; case 0xC8: Y++; nz(Y); break; case 0xCA: X--; nz(X); break; case 0x88: Y--; nz(Y); break;
    /* shifts */
#define SHIFT(opA, opz, opzx, opab, opabx, expr) \
    case opA: { uint8_t t = A; expr; A = t; nz(A); } break; \
    case opz: a = ea(1); { uint8_t t = rd(a); expr; wr(a, t); nz(t); } break; \
    case opzx: a = ea(2); { uint8_t t = rd(a); expr; wr(a, t); nz(t); } break; \
    case opab: a = ea(4); { uint8_t t = rd(a); expr; wr(a, t); nz(t); } break; \
    case opabx: a = ea(5); { uint8_t t = rd(a); expr; wr(a, t); nz(t); } break;
    SHIFT(0x0A, 0x06, 0x16, 0x0E, 0x1E, { P = (uint8_t)((P & ~FC) | (t >> 7)); t = (uint8_t)(t << 1); })
    SHIFT(0x4A, 0x46, 0x56, 0x4E, 0x5E, { P = (uint8_t)((P & ~FC) | (t & 1)); t = (uint8_t)(t >> 1); })
    SHIFT(0x2A, 0x26, 0x36, 0x2E, 0x3E, { int c = P & FC; P = (uint8_t)((P & ~FC) | (t >> 7)); t = (uint8_t)(t << 1 | c); })
    SHIFT(0x6A, 0x66, 0x76, 0x6E, 0x7E, { int c = P & FC; P = (uint8_t)((P & ~FC) | (t & 1)); t = (uint8_t)(t >> 1 | c << 7); })
    /* jumps */
    case 0x4C: PC = rd16(PC); break;
    case 0x6C: a = rd16(PC); PC = (uint16_t)(rd(a) | rd((uint16_t)((a & 0xFF00) | ((a + 1) & 0xFF))) << 8); break;
    case 0x20: a = rd16(PC); PC++; push((uint8_t)(PC >> 8)); push((uint8_t)PC); PC = a; break;
    case 0x60: PC = (uint16_t)(pull() | pull() << 8); PC++; break;
    case 0x40: P = (uint8_t)(pull() | FU); PC = (uint16_t)(pull() | pull() << 8); break;
    case 0x00: PC++; push((uint8_t)(PC >> 8)); push((uint8_t)PC); push(P | FB | FU); P |= FI; fprintf(stderr, "BRK at %04X\n", PC - 2); return 0;
    case 0x10: branch(!(P & FN)); break; case 0x30: branch(P & FN); break; case 0x50: branch(!(P & FV)); break; case 0x70: branch(P & FV); break;
    case 0x90: branch(!(P & FC)); break; case 0xB0: branch(P & FC); break; case 0xD0: branch(!(P & FZ)); break; case 0xF0: branch(P & FZ); break;
    /* flags */
    case 0x18: P &= ~FC; break; case 0x38: P |= FC; break; case 0x58: P &= ~FI; break; case 0x78: P |= FI; break;
    case 0xB8: P &= ~FV; break; case 0xD8: P &= ~FD; break; case 0xF8: P |= FD; break; case 0xEA: break;
    default: fprintf(stderr, "unknown opcode %02X at %04X\n", op, PC - 1); return 0;
    }
    return 1;
}

static uint8_t *load(const char *fn, long *n)
{
    FILE *f = fopen(fn, "rb"); uint8_t *b; if (!f) { perror(fn); exit(1); }
    fseek(f, 0, SEEK_END); *n = ftell(f); fseek(f, 0, SEEK_SET);
    b = malloc((size_t)*n); if (fread(b, 1, (size_t)*n, f) != (size_t)*n) exit(1); fclose(f); return b;
}
int main(int argc, char **argv)
{
    long max = 50000000; const char *dumpfn = NULL; int i, ndisk = 0; uint8_t buf[128]; int nsec; uint16_t la;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-n")) max = (long)(atof(argv[++i]) * 1e6);
        else if (!strcmp(argv[i], "-dump")) dumpfn = argv[++i];
        else if (!strcmp(argv[i], "-log")) logsio = 1;
        else if (!strcmp(argv[i], "-trace")) trace_pc[ntrace++] = strtol(argv[++i], NULL, 16);
        else if (!strcmp(argv[i], "-start")) start_at = (long)(atof(argv[++i]) * 1e6);
        else if (!strcmp(argv[i], "-key")) { char *c; key_at = (long)(strtod(argv[++i], &c) * 1e6); key_code = (int)strtol(c + 1, NULL, 16); }
        else if (!strcmp(argv[i], "-every")) key_every = (long)(atof(argv[++i]) * 1e6);
        else if (!strcmp(argv[i], "-wlog")) { wlog_from = (long)(atof(argv[++i]) * 1e6); wlog_to = (long)(atof(argv[++i]) * 1e6); }
        else if (!strcmp(argv[i], "-wrange")) { wlo = strtoul(argv[++i], NULL, 16); whi = strtoul(argv[++i], NULL, 16); }
        else if (!strcmp(argv[i], "-swap")) swap_after = atol(argv[++i]);
        else { disk[ndisk] = load(argv[i], &disk_size[ndisk]); ndisk++; }
    }
    /* minimal OS state */
    mem[0x6A] = 0xC0; mem[0x302] = 0x52; mem[0x301] = 1;   /* RAMTOP 48K; DCB left as after the boot read */
    mem[0x2E4] = 0xC0; mem[0x2E5] = 0x00; mem[0x2E6] = 0xC0;   /* RAMSIZ, MEMTOP $C000? */
    mem[0x2E5] = 0x00; mem[0x2E6] = 0xBC;
    mem[0xFFFA] = 0x00;            /* OS signature byte the loader looks at */
    mem[0x222] = 0x5F; mem[0x223] = 0xE4; mem[0x224] = 0x62; mem[0x225] = 0xE4;   /* VBI -> SYSVBV -> XITVBV */
    /* boot: sector 1 header */
    read_sector(1, 1, buf); nsec = buf[1]; la = (uint16_t)(buf[2] | buf[3] << 8);
    for (i = 0; i < nsec; i++) { read_sector(1, i + 1, buf); memcpy(mem + la + 128 * i, buf, 128); }
    mem[0x0C] = mem[la + 4]; mem[0x0D] = mem[la + 5];  /* DOSINI */
    fprintf(stderr, "boot: %d sectors at %04X, init %04X\n", nsec, la, mem[0x0C] | mem[0x0D] << 8);
    /* JSR load+6, then JSR (DOSINI), then JMP (DOSVEC): a fake return chain at $E4F0.. */
    mem[0xE4F0] = 0x20; mem[0xE4F1] = 0xF8; mem[0xE4F2] = 0xE4;                     /* JSR $E4F8 */
    mem[0xE4F3] = 0x6C; mem[0xE4F4] = 0x0A; mem[0xE4F5] = 0x00;                     /* JMP ($000A) */
    mem[0xE4F8] = 0x6C; mem[0xE4F9] = 0x0C; mem[0xE4FA] = 0x00;                     /* JMP ($000C) */
    push(0xE4); push(0xEF);                            /* return to E4F0 */
    PC = (uint16_t)(la + 6); P = 0x24;
    for (icount = 0; icount < max; icount++) {
        if (icount % 30000 == 0) vcount = 0;
        if (icount % 300 == 0) vcount++;
        if (icount % 30000 == 29999 && !getenv("A8NONMI")) nmi_vbl();
        if (!step()) break;
    }
    if (getenv("A8DECODE") && disk[1]) {            /* run the game's maze unpacker on each maze of disk 2 */
        static uint8_t snap[65536]; int n, maxn = atoi(getenv("A8DECODE")); FILE *o = fopen("mazes.bin", "wb");
        uint8_t S0 = S, A0 = A, X0 = X, Y0 = Y, P0 = P;
        memcpy(snap, mem, 65536);
        for (n = 1; n <= maxn; n++) {
            long k; int sec;
            memcpy(mem, snap, 65536); S = S0; A = A0; X = X0; Y = Y0; P = P0;
            for (sec = 0; sec < 4; sec++) { long off = 16 + 128L * (4 * n + sec - 1); memset(mem + 0x9F00 + 128 * sec, 0, 128); if (off + 128 <= disk_size[1]) memcpy(mem + 0x9F00 + 128 * sec, disk[1] + off, 128); }
            mem[0x12A2] = (uint8_t)n;
            push(0xFF); push(0xEF); PC = 0x56DE; P |= FI;
            for (k = 0; k < 20000000 && PC != 0xFFF0; k++) { icount++; if (!step()) break; }
            if (PC != 0xFFF0) fprintf(stderr, "maze %d: did not return (PC %04X)\n", n, PC);
            fwrite(mem + 0x9F00, 1, 8, o); fwrite(mem + 0xD800, 1, 1024, o);
        }
        fclose(o); fprintf(stderr, "decoded %d mazes -> mazes.bin\n", maxn);
    }
    fprintf(stderr, "stopped at %ld instructions, PC %04X, %d sector reads\n", icount, PC, nreads);
    if (wlog_from >= 0) { int k; for (k = 0; k < 65536; k++) if (wpc[k] > 100) fprintf(stderr, "wpc %04X %u\n", k, wpc[k]); for (k = 0; k < 256; k++) if (wpage[k] > 200) fprintf(stderr, "wpage %02X %u\n", k, wpage[k]); }
    if (dumpfn) { FILE *f = fopen(dumpfn, "wb"); fwrite(mem, 1, 65536, f); fclose(f); }
    return 0;
}
