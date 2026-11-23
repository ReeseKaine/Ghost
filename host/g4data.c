/* g4data.c -- the launcher's data steps in C (launcher step 3; user, 2026-11-22): the files the
   Python tools (make_data.sh) made, now built by the program itself from the player's ROMs, so
   players need nothing installed. Each step must give the same bytes as its Python tool.
     data_speech   g2_speech.bin   (g2_speech.py --bin)
   Shared pieces: reading members of the arcade zips (zip_read, with a small inflate for
   "deflate"-packed members), and Python's random generator (MT19937, as random.Random(seed)),
   which the speech decoder's noise uses. Nothing from the games is in this file. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include "g4data.h"

/* ------------------------------------------------------------------ inflate (RFC 1951) */
typedef struct { const uint8_t *in; size_t n, pos; uint32_t bitbuf; int bitcnt; uint8_t *out; size_t outn, outcap; int err; } inf_t;
static int inf_bits(inf_t *s, int need)
{
    uint32_t v = s->bitbuf;
    while (s->bitcnt < need) {
        if (s->pos >= s->n) { s->err = 1; return 0; }
        v |= (uint32_t)s->in[s->pos++] << s->bitcnt; s->bitcnt += 8;
    }
    s->bitbuf = v >> need; s->bitcnt -= need;
    return (int)(v & ((1u << need) - 1));
}
static void inf_put(inf_t *s, uint8_t b)
{
    if (s->outn == s->outcap) { s->outcap = s->outcap ? s->outcap * 2 : 65536; s->out = (uint8_t *)realloc(s->out, s->outcap); }
    s->out[s->outn++] = b;
}
typedef struct { short count[16], symbol[288]; } huff_t;
static int huff_build(huff_t *h, const short *len, int n)
{
    short offs[16]; int i, left = 1;
    memset(h->count, 0, sizeof h->count);
    for (i = 0; i < n; i++) h->count[len[i]]++;
    if (h->count[0] == n) return 0;
    for (i = 1; i < 16; i++) { left <<= 1; left -= h->count[i]; if (left < 0) return -1; }
    offs[1] = 0;
    for (i = 1; i < 15; i++) offs[i + 1] = (short)(offs[i] + h->count[i]);
    for (i = 0; i < n; i++) if (len[i]) h->symbol[offs[len[i]]++] = (short)i;
    return left;
}
static int huff_decode(inf_t *s, const huff_t *h)
{
    int code = 0, first = 0, index = 0, len;
    for (len = 1; len < 16; len++) {
        code |= inf_bits(s, 1); if (s->err) return -1;
        if (code - h->count[len] < first) return h->symbol[index + (code - first)];
        index += h->count[len]; first += h->count[len]; first <<= 1; code <<= 1;
    }
    s->err = 1; return -1;
}
static const short len_base[29] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
static const short len_extra[29] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
static const short dist_base[30] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
static const short dist_extra[30] = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};
static int inf_codes(inf_t *s, const huff_t *lit, const huff_t *dist)
{
    for (;;) {
        int sym = huff_decode(s, lit);
        if (s->err || sym < 0) return -1;
        if (sym < 256) inf_put(s, (uint8_t)sym);
        else if (sym == 256) return 0;
        else {
            int l, d; size_t k;
            sym -= 257; if (sym >= 29) return -1;
            l = len_base[sym] + inf_bits(s, len_extra[sym]);
            sym = huff_decode(s, dist); if (sym < 0 || sym >= 30) return -1;
            d = dist_base[sym] + inf_bits(s, dist_extra[sym]);
            if (s->err || (size_t)d > s->outn) return -1;
            for (k = 0; k < (size_t)l; k++) inf_put(s, s->out[s->outn - (size_t)d]);
        }
    }
}
static uint8_t *inflate_raw(const uint8_t *in, size_t n, size_t *outn)
{
    inf_t s; int last;
    memset(&s, 0, sizeof s); s.in = in; s.n = n;
    do {
        int type;
        last = inf_bits(&s, 1); type = inf_bits(&s, 2);
        if (s.err) break;
        if (type == 0) {                                   /* stored */
            unsigned len;
            s.bitbuf = 0; s.bitcnt = 0;
            if (s.pos + 4 > s.n) { s.err = 1; break; }
            len = (unsigned)s.in[s.pos] | (unsigned)s.in[s.pos + 1] << 8; s.pos += 4;
            if (s.pos + len > s.n) { s.err = 1; break; }
            while (len--) inf_put(&s, s.in[s.pos++]);
        } else if (type == 1 || type == 2) {
            huff_t lit, dist; short lens[320];
            if (type == 1) {                               /* fixed codes */
                int i;
                for (i = 0; i < 144; i++) lens[i] = 8;
                for (; i < 256; i++) lens[i] = 9;
                for (; i < 280; i++) lens[i] = 7;
                for (; i < 288; i++) lens[i] = 8;
                huff_build(&lit, lens, 288);
                for (i = 0; i < 30; i++) lens[i] = 5;
                huff_build(&dist, lens, 30);
            } else {                                       /* dynamic codes */
                static const short order[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
                int nlen = inf_bits(&s, 5) + 257, ndist = inf_bits(&s, 5) + 1, ncode = inf_bits(&s, 4) + 4, i = 0;
                huff_t lencode;
                if (s.err || nlen > 286 || ndist > 30) { s.err = 1; break; }
                for (i = 0; i < 19; i++) lens[order[i]] = i < ncode ? (short)inf_bits(&s, 3) : 0;
                if (huff_build(&lencode, lens, 19) != 0) { s.err = 1; break; }
                i = 0;
                while (i < nlen + ndist) {
                    int sym = huff_decode(&s, &lencode), rep = 0; short v = 0;
                    if (sym < 0) { s.err = 1; break; }
                    if (sym < 16) { lens[i++] = (short)sym; continue; }
                    if (sym == 16) { if (!i) { s.err = 1; break; } v = lens[i - 1]; rep = 3 + inf_bits(&s, 2); }
                    else if (sym == 17) rep = 3 + inf_bits(&s, 3);
                    else rep = 11 + inf_bits(&s, 7);
                    if (i + rep > nlen + ndist) { s.err = 1; break; }
                    while (rep--) lens[i++] = v;
                }
                if (s.err) break;
                if (huff_build(&lit, lens, nlen) < 0 || huff_build(&dist, lens + nlen, ndist) < 0) { s.err = 1; break; }
            }
            if (inf_codes(&s, &lit, &dist)) { s.err = 1; break; }
        } else { s.err = 1; break; }
    } while (!last);
    if (s.err) { free(s.out); return NULL; }
    *outn = s.outn;
    return s.out;
}

/* ------------------------------------------------------------------ zip members */
static uint32_t crc_tab[256];
static uint32_t crc32_of(const uint8_t *p, size_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    if (!crc_tab[1]) { uint32_t k, v; int b; for (k = 0; k < 256; k++) { v = k; for (b = 0; b < 8; b++) v = v & 1 ? 0xEDB88320u ^ (v >> 1) : v >> 1; crc_tab[k] = v; } }
    while (n--) c = crc_tab[(c ^ *p++) & 255] ^ (c >> 8);
    return ~c;
}
static unsigned rd16le(const uint8_t *p) { return (unsigned)p[0] | (unsigned)p[1] << 8; }
static uint32_t rd32le(const uint8_t *p) { return rd16le(p) | (uint32_t)rd16le(p + 2) << 16; }
uint8_t *zip_read(const char *zip, const char *name, size_t *size)
{
    FILE *f = fopen(zip, "rb"); long fsize, start; uint8_t *buf = NULL, *e = NULL, *out = NULL; uint32_t cd_off, cd_size, o; unsigned n, k;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); fsize = ftell(f);
    start = fsize > 65557 ? fsize - 65557 : 0;
    buf = (uint8_t *)malloc((size_t)(fsize - start)); fseek(f, start, SEEK_SET);
    if (!buf || fread(buf, 1, (size_t)(fsize - start), f) != (size_t)(fsize - start)) goto fail;
    for (k = (unsigned)(fsize - start) >= 22 ? (unsigned)(fsize - start) - 21 : 0; k-- > 0; ) if (rd32le(buf + k) == 0x06054b50) { e = buf + k; break; }
    if (!e) goto fail;
    n = rd16le(e + 10); cd_size = rd32le(e + 12); cd_off = rd32le(e + 16);
    free(buf); buf = (uint8_t *)malloc(cd_size ? cd_size : 1);
    fseek(f, (long)cd_off, SEEK_SET);
    if (!buf || fread(buf, 1, cd_size, f) != cd_size) goto fail;
    for (k = 0, o = 0; k < n && o + 46 <= cd_size; k++) {
        unsigned nl = rd16le(buf + o + 28), xl = rd16le(buf + o + 30), cl = rd16le(buf + o + 32), method = rd16le(buf + o + 10);
        uint32_t crc = rd32le(buf + o + 16), csize = rd32le(buf + o + 20), usize = rd32le(buf + o + 24), loff = rd32le(buf + o + 42);
        const char *mn = (const char *)buf + o + 46, *slash; size_t ml = nl;
        for (slash = mn + nl; slash > mn && slash[-1] != '/'; slash--) ;   /* members in a sub-folder count by their name */
        ml = (size_t)(mn + nl - slash);
        if (ml == strlen(name) && !memcmp(slash, name, ml)) {
            uint8_t lh[30], *comp; size_t got = 0;
            fseek(f, (long)loff, SEEK_SET);
            if (fread(lh, 1, 30, f) != 30 || rd32le(lh) != 0x04034b50) goto fail;
            fseek(f, (long)(loff + 30 + rd16le(lh + 26) + rd16le(lh + 28)), SEEK_SET);
            comp = (uint8_t *)malloc(csize ? csize : 1);
            if (!comp || fread(comp, 1, csize, f) != csize) { free(comp); goto fail; }
            if (method == 0) { out = comp; got = csize; }
            else if (method == 8) { out = inflate_raw(comp, csize, &got); free(comp); }
            else { free(comp); goto fail; }
            if (!out || got != usize || crc32_of(out, got) != crc) { free(out); out = NULL; goto fail; }
            *size = got; free(buf); fclose(f);
            return out;
        }
        o += 46 + nl + xl + cl;
    }
fail:
    free(buf); fclose(f);
    return NULL;
}

/* ------------------------------------------------------------------ Python's random.Random(seed) */
typedef struct { uint32_t mt[624]; int idx; } pyrand;
static void mt_init(pyrand *r, uint32_t s)
{
    int i; r->mt[0] = s;
    for (i = 1; i < 624; i++) r->mt[i] = 1812433253u * (r->mt[i - 1] ^ (r->mt[i - 1] >> 30)) + (uint32_t)i;
    r->idx = 624;
}
static void py_seed(pyrand *r, uint32_t seed)          /* random.Random(int): init_by_array([seed]) */
{
    int i = 1, j = 0, k;
    mt_init(r, 19650218u);
    for (k = 624; k; k--) {
        r->mt[i] = (r->mt[i] ^ ((r->mt[i - 1] ^ (r->mt[i - 1] >> 30)) * 1664525u)) + seed + (uint32_t)j;
        i++; j++;
        if (i >= 624) { r->mt[0] = r->mt[623]; i = 1; }
        if (j >= 1) j = 0;
    }
    for (k = 623; k; k--) {
        r->mt[i] = (r->mt[i] ^ ((r->mt[i - 1] ^ (r->mt[i - 1] >> 30)) * 1566083941u)) - (uint32_t)i;
        i++;
        if (i >= 624) { r->mt[0] = r->mt[623]; i = 1; }
    }
    r->mt[0] = 0x80000000u;
}
static uint32_t mt_next(pyrand *r)
{
    uint32_t y;
    if (r->idx >= 624) {
        int k;
        for (k = 0; k < 624; k++) {
            y = (r->mt[k] & 0x80000000u) | (r->mt[(k + 1) % 624] & 0x7fffffffu);
            r->mt[k] = r->mt[(k + 397) % 624] ^ (y >> 1) ^ (y & 1 ? 0x9908b0dfu : 0);
        }
        r->idx = 0;
    }
    y = r->mt[r->idx++];
    y ^= y >> 11; y ^= (y << 7) & 0x9d2c5680u; y ^= (y << 15) & 0xefc60000u; y ^= y >> 18;
    return y;
}
static double py_random(pyrand *r) { uint32_t a = mt_next(r) >> 5, b = mt_next(r) >> 6; return (a * 67108864.0 + b) * (1.0 / 9007199254740992.0); }

/* ------------------------------------------------------------------ g2_speech.bin */
/* Gauntlet II's narration: the sound board's TMS5220 LPC phrases, decoded as g2_speech.py does
   (frame format and coefficient tables as in MAME's tms5220: chip constants). */
static const int ENERGY[16] = {0, 1, 2, 3, 4, 6, 8, 11, 16, 23, 33, 47, 63, 85, 114, 0};
static const int PITCH[64] = {0, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37,
    38, 39, 40, 41, 42, 44, 46, 48, 50, 52, 53, 56, 58, 60, 62, 65, 68, 70, 72, 76, 78, 80, 84, 86,
    91, 94, 98, 101, 105, 109, 114, 118, 122, 127, 132, 137, 142, 148, 153, 159};
static const short KT[10][32] = {
    {-501, -498, -497, -495, -493, -491, -488, -482, -478, -474, -469, -464, -459, -452, -445, -437,
     -412, -380, -339, -288, -227, -158, -81, -1, 80, 157, 226, 287, 337, 379, 411, 436},
    {-328, -303, -274, -244, -211, -175, -138, -99, -59, -18, 24, 64, 105, 143, 180, 215,
     248, 278, 306, 331, 354, 374, 392, 408, 422, 435, 445, 455, 463, 470, 476, 506},
    {-441, -387, -333, -279, -225, -171, -117, -63, -9, 45, 98, 152, 206, 260, 314, 368},
    {-328, -273, -217, -161, -106, -50, 5, 61, 116, 172, 228, 283, 339, 394, 450, 506},
    {-328, -282, -235, -189, -142, -96, -50, -3, 43, 90, 136, 182, 229, 275, 322, 368},
    {-256, -212, -168, -123, -79, -35, 10, 54, 98, 143, 187, 232, 276, 320, 365, 409},
    {-308, -260, -212, -164, -117, -69, -21, 27, 75, 122, 170, 218, 266, 314, 361, 409},
    {-256, -161, -66, 29, 124, 219, 314, 409},
    {-256, -176, -96, -15, 65, 146, 226, 307},
    {-205, -132, -59, 14, 87, 160, 234, 307}};
static const int KBITS[10] = {5, 5, 4, 4, 4, 4, 4, 3, 3, 3};
static const signed char CHIRP[21] = {0x00, 0x03, 0x0f, 0x28, 0x4c, 0x6c, 0x71, 0x50, 0x25, 0x26, 0x4c, 0x44, 0x1a, 0x32, 0x3b,
    0x13, 0x37, 0x1a, 0x25, 0x1f, 0x1d};
#define CHIRP_LEN 51                                     /* 21 values, then 30 zeros */

typedef struct { int e, p, kind; double k[10]; } lpc_frame;   /* kind: 0 new K, 1 repeat, 2 silence */
static int lpc_frames(const uint8_t *data, size_t n, lpc_frame **out)
{
    size_t pos = 0, nbits = n * 8; int cnt = 0, cap = 64;
    lpc_frame *fr = (lpc_frame *)malloc(sizeof *fr * (size_t)cap);
#define BITS(var, cntb) do { int _i; var = 0; for (_i = 0; _i < (cntb); _i++) { if (pos >= nbits) goto end; var = (var << 1) | ((data[pos >> 3] >> (pos & 7)) & 1); pos++; } } while (0)
    for (;;) {
        int e, rep, p, i, ks[10];
        lpc_frame f;
        BITS(e, 4);
        if (e == 15) break;
        memset(&f, 0, sizeof f);
        if (e == 0) { f.kind = 2; }
        else {
            BITS(rep, 1); BITS(p, 6);
            f.e = ENERGY[e]; f.p = PITCH[p];
            if (rep) f.kind = 1;
            else {
                for (i = 0; i < 4; i++) BITS(ks[i], KBITS[i]);
                if (p) for (i = 4; i < 10; i++) BITS(ks[i], KBITS[i]);
                for (i = 0; i < 10; i++) f.k[i] = (i < 4 || p) ? KT[i][ks[i]] / 512.0 : 0.0;
            }
        }
        if (cnt == cap) { cap *= 2; fr = (lpc_frame *)realloc(fr, sizeof *fr * (size_t)cap); }
        fr[cnt++] = f;
    }
end:
#undef BITS
    *out = fr;
    return cnt;
}
static double *lpc_synth(const lpc_frame *fr, int nfr, size_t *nout)
{
    pyrand rnd; double x[10] = {0}, cur_e = 0, cur_k[10] = {0}, *s; int cur_p = 0, pc = 0, f, step, i, j;
    size_t n = 0;
    py_seed(&rnd, 1);
    s = (double *)malloc(sizeof *s * (size_t)(nfr * 200 + 1));
    for (f = 0; f < nfr; f++) {
        double tgt_e, tgt_k[10]; int tgt_p, jump;
        const double *ks = fr[f].k; int p = fr[f].p;
        if (fr[f].kind == 1) ks = cur_k;
        if (fr[f].kind == 2) { ks = cur_k; p = cur_p; }
        tgt_e = (double)fr[f].e; tgt_p = p;
        for (i = 0; i < 10; i++) tgt_k[i] = ks[i];
        jump = (cur_p == 0) != (tgt_p == 0);
        for (step = 0; step < 8; step++) {
            double fa = (step + 1) / 8.0, se = cur_e + (tgt_e - cur_e) * fa, sk[10]; int sp;
            sp = jump ? tgt_p : (int)nearbyint(cur_p + (tgt_p - cur_p) * fa);   /* Python round(): half to even */
            for (i = 0; i < 10; i++) sk[i] = jump ? tgt_k[i] : cur_k[i] + (tgt_k[i] - cur_k[i]) * fa;
            for (j = 0; j < 25; j++) {
                double exc, u[11];
                if (sp) { exc = (pc < 21 ? CHIRP[pc] : 0) * se / 64.0; pc++; if (pc >= sp) pc = 0; (void)CHIRP_LEN; }
                else exc = (py_random(&rnd) < 0.5 ? se : -se) / 2.0;
                u[10] = exc;
                for (i = 9; i >= 0; i--) u[i] = u[i + 1] - sk[i] * x[i];
                for (i = 9; i >= 1; i--) x[i] = x[i - 1] + sk[i - 1] * u[i - 1];
                x[0] = u[0];
                s[n++] = u[0];
            }
        }
        cur_e = tgt_e; cur_p = tgt_p;
        for (i = 0; i < 10; i++) cur_k[i] = tgt_k[i];
    }
    *nout = n;
    return s;
}
static void lpc_level(double *pcm, size_t n)           /* g2_speech.py level() */
{
    size_t win = 200, nw = (n + win - 1) / win, k, i; double top = 0, *peaks, *gains;
    if (!n) return;
    peaks = (double *)calloc(nw, sizeof *peaks); gains = (double *)calloc(nw, sizeof *gains);
    for (k = 0; k < nw; k++) { double m = 0; for (i = k * win; i < (k + 1) * win && i < n; i++) if (fabs(pcm[i]) > m) m = fabs(pcm[i]); peaks[k] = m; if (m > top) top = m; }
    if (top == 0) top = 1;
    for (k = 0; k < nw; k++) {
        double p = peaks[k];
        if (k > 0 && peaks[k - 1] > p) p = peaks[k - 1];
        if (k + 1 < nw && peaks[k + 1] > p) p = peaks[k + 1];
        gains[k] = p < top * 0.03 ? 1.0 : (top * 0.8 / p < 6.0 ? top * 0.8 / p : 6.0);
    }
    for (i = 0; i < n; i++) {
        size_t kk = i / win; double fa = (double)(i % win) / (double)win;
        double g = gains[kk] * (1 - fa) + gains[kk + 1 < nw ? kk + 1 : nw - 1] * fa;
        pcm[i] = pcm[i] * g;
    }
    free(peaks); free(gains);
}
static void be16(uint8_t *p, unsigned v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void be32(uint8_t *p, uint32_t v) { be16(p, v >> 16); be16(p + 2, v & 0xFFFF); }
int data_speech(const char *zip_16r, const char *zip_16s, const char *out_path)
{
    uint8_t *mem = (uint8_t *)calloc(1, 0x10000), *a, *b, *file; size_t na = 0, nb = 0, total, pos;
    int n = (0x85C3 - 0x8449) / 2, ph; FILE *f;
    uint8_t **clip; size_t *clen;
    a = zip_read(zip_16r, "136043-1120.16r", &na); b = zip_read(zip_16s, "136043-1119.16s", &nb);
    if (!a || !b || na != 0x4000 || nb != 0x8000) { free(a); free(b); free(mem); return 0; }
    memcpy(mem + 0x4000, a, na); memcpy(mem + 0x8000, b, nb); free(a); free(b);
    clip = (uint8_t **)calloc((size_t)n, sizeof *clip); clen = (size_t *)calloc((size_t)n, sizeof *clen);
    total = 8 + 8 * (size_t)n;
    for (ph = 0; ph < n; ph++) {
        unsigned start = rd16le(mem + 0x8449 + 2 * ph), length = rd16le(mem + 0x85C3 + 2 * ph);
        lpc_frame *fr; int nfr; size_t ns, i; double *pcm, peak = 0;
        size_t avail = start < 0x10000 ? 0x10000 - start : 0, take = length + 1 < avail ? length + 1 : avail;
        nfr = lpc_frames(mem + start, take, &fr);
        pcm = lpc_synth(fr, nfr, &ns); free(fr);
        lpc_level(pcm, ns);
        for (i = 0; i < ns; i++) if (fabs(pcm[i]) > peak) peak = fabs(pcm[i]);
        if (peak == 0) peak = 1;
        clip[ph] = (uint8_t *)malloc(ns ? ns : 1); clen[ph] = ns;
        for (i = 0; i < ns; i++) {
            double v = pcm[i] / peak * 0.9;
            if (v > 1) v = 1; if (v < -1) v = -1;
            clip[ph][i] = (uint8_t)((int)(v * 127) & 0xFF);
        }
        free(pcm);
        total += ns;
    }
    file = (uint8_t *)malloc(total);
    memcpy(file, "G2VO", 4); be16(file + 4, (unsigned)n); be16(file + 6, 8000);
    pos = 8 + 8 * (size_t)n;
    for (ph = 0; ph < n; ph++) {
        be32(file + 8 + 8 * ph, (uint32_t)pos); be32(file + 12 + 8 * ph, (uint32_t)clen[ph]);
        memcpy(file + pos, clip[ph], clen[ph]); pos += clen[ph]; free(clip[ph]);
    }
    free(clip); free(clen); free(mem);
    f = fopen(out_path, "wb");
    if (!f || fwrite(file, 1, total, f) != total) { if (f) fclose(f); free(file); return 0; }
    fclose(f); free(file);
    return 1;
}
