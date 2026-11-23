/* cpu6502.h -- a small 6502 (the port's own, from tools_atari8/a8.c): official opcodes, decimal mode,
   base cycle counts (page-crossing extras not counted). The includer defines rd() and wr().
   Included by one file only (all static). */
static uint8_t A, X, Y, S = 0xFF, P = 0x24; static uint16_t PC;
enum { FC = 1, FZ = 2, FI = 4, FD = 8, FB = 16, FU = 32, FV = 64, FN = 128 };
static uint8_t rd(uint16_t a); static void wr(uint16_t a, uint8_t v);
static uint16_t rd16(uint16_t a) { return (uint16_t)(rd(a) | rd((uint16_t)(a + 1)) << 8); }
static const uint8_t CYC6502[256] = {
 7,6,2,8,3,3,5,5,3,2,2,2,4,4,6,6, 2,5,2,8,4,4,6,6,2,4,2,7,4,4,7,7,
 6,6,2,8,3,3,5,5,4,2,2,2,4,4,6,6, 2,5,2,8,4,4,6,6,2,4,2,7,4,4,7,7,
 6,6,2,8,3,3,5,5,3,2,2,2,3,4,6,6, 2,5,2,8,4,4,6,6,2,4,2,7,4,4,7,7,
 6,6,2,8,3,3,5,5,4,2,2,2,5,4,6,6, 2,5,2,8,4,4,6,6,2,4,2,7,4,4,7,7,
 2,6,2,6,3,3,3,3,2,2,2,2,4,4,4,4, 2,6,2,6,4,4,4,4,2,5,2,5,5,5,5,5,
 2,6,2,6,3,3,3,3,2,2,2,2,4,4,4,4, 2,5,2,5,4,4,4,4,2,4,2,4,4,4,4,4,
 2,6,2,8,3,3,5,5,2,2,2,2,4,4,6,6, 2,5,2,8,4,4,6,6,2,4,2,7,4,4,7,7,
 2,6,2,8,3,3,5,5,2,2,2,2,4,4,6,6, 2,5,2,8,4,4,6,6,2,4,2,7,4,4,7,7};
static void push(uint8_t v) { wr((uint16_t)(0x100 + S--), v); }
static uint8_t pull(void) { return rd((uint16_t)(0x100 + ++S)); }
static void nz(uint8_t v) { P = (uint8_t)((P & ~(FN | FZ)) | (v & 0x80) | (v ? 0 : FZ)); }
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
    case 7: b = (uint8_t)(rd(PC++) + X); return (uint16_t)(rd(b) | rd((uint8_t)(b + 1)) << 8);   /* (zp,x) */
    case 8: b = rd(PC++); return (uint16_t)((rd(b) | rd((uint8_t)(b + 1)) << 8) + Y);           /* (zp),y */
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

static void irq6502(int nmi)
{
    if (!nmi && (P & FI)) return;
    push((uint8_t)(PC >> 8)); push((uint8_t)PC); push((uint8_t)((P & ~FB) | FU));
    P |= FI; PC = rd16(nmi ? 0xFFFA : 0xFFFE);
}
