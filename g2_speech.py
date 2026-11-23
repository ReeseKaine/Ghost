"""
g2_speech.py -- decode arcade Gauntlet II's narration (TMS5220 speech) into WAV clips.

The sound board's 6502 program (136043-1120.16r at $4000, 136043-1119.16s at $8000) streams
speech to the TMS5220 at $1820 (routine $58D5): phrase p starts at the word in the table at
$8449 + 2p and its byte count is the word at $85C3 + 2p (189 phrases); sound command y plays
phrase $63B2[y]. Each phrase is sent with "Speak External" (0x60) then the LPC bytes.

The decoder follows the TMS5220's documented frame format and coefficient tables (as in MAME's
tms5220 emulation): bits are taken from each byte least-significant first; a frame is energy (4
bits; 0 = silence, 15 = stop), repeat (1), pitch (6), then unless repeating K1-K4 (5,5,4,4) and,
when voiced, K5-K10 (4,4,3,3,3,3). 8000 samples a second, 200 per frame, parameters
interpolated over 8 steps; voiced excitation is the chip's chirp, unvoiced is noise; a 10-stage
lattice filter.

Usage: python3 -I g2_speech.py <gauntlet2.zip> <out folder>
Writes phrase_NNN.wav for every phrase and phrases.txt (address, length, duration).
       python3 -I g2_speech.py <gauntlet2.zip> --bin <g2_speech.bin>
Writes only the packed file the game plays the narration from (see write_bin).
"""
import os, struct, sys, wave, zipfile

ENERGY = [0, 1, 2, 3, 4, 6, 8, 11, 16, 23, 33, 47, 63, 85, 114, 0]
PITCH = [0, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37,
         38, 39, 40, 41, 42, 44, 46, 48, 50, 52, 53, 56, 58, 60, 62, 65, 68, 70, 72, 76, 78, 80, 84, 86,
         91, 94, 98, 101, 105, 109, 114, 118, 122, 127, 132, 137, 142, 148, 153, 159]
K = [
    [-501, -498, -497, -495, -493, -491, -488, -482, -478, -474, -469, -464, -459, -452, -445, -437,
     -412, -380, -339, -288, -227, -158, -81, -1, 80, 157, 226, 287, 337, 379, 411, 436],
    [-328, -303, -274, -244, -211, -175, -138, -99, -59, -18, 24, 64, 105, 143, 180, 215,
     248, 278, 306, 331, 354, 374, 392, 408, 422, 435, 445, 455, 463, 470, 476, 506],
    [-441, -387, -333, -279, -225, -171, -117, -63, -9, 45, 98, 152, 206, 260, 314, 368],
    [-328, -273, -217, -161, -106, -50, 5, 61, 116, 172, 228, 283, 339, 394, 450, 506],
    [-328, -282, -235, -189, -142, -96, -50, -3, 43, 90, 136, 182, 229, 275, 322, 368],
    [-256, -212, -168, -123, -79, -35, 10, 54, 98, 143, 187, 232, 276, 320, 365, 409],
    [-308, -260, -212, -164, -117, -69, -21, 27, 75, 122, 170, 218, 266, 314, 361, 409],
    [-256, -161, -66, 29, 124, 219, 314, 409],
    [-256, -176, -96, -15, 65, 146, 226, 307],
    [-205, -132, -59, 14, 87, 160, 234, 307]]
KBITS = [5, 5, 4, 4, 4, 4, 4, 3, 3, 3]
CHIRP = [0x00, 0x03, 0x0f, 0x28, 0x4c, 0x6c, 0x71, 0x50, 0x25, 0x26, 0x4c, 0x44, 0x1a, 0x32, 0x3b,
         0x13, 0x37, 0x1a, 0x25, 0x1f, 0x1d]
CHIRP = [(c - 256 if c > 127 else c) for c in CHIRP] + [0] * 30

def frames(data):
    pos = 0
    def bits(n):
        nonlocal pos
        v = 0
        for _ in range(n):
            if pos >= len(data) * 8: raise EOFError
            v = (v << 1) | ((data[pos >> 3] >> (pos & 7)) & 1); pos += 1
        return v
    out = []
    try:
        while True:
            e = bits(4)
            if e == 15: break
            if e == 0: out.append((0, None, None)); continue
            rep = bits(1); p = bits(6)
            if rep: out.append((ENERGY[e], PITCH[p], "repeat")); continue
            ks = [bits(KBITS[i]) for i in range(4)]
            if p: ks += [bits(KBITS[i]) for i in range(4, 10)]
            out.append((ENERGY[e], PITCH[p], [K[i][v] / 512.0 for i, v in enumerate(ks)] + [0.0] * (10 - len(ks))))
    except EOFError:
        pass
    return out

def synth(fr):
    import random
    rnd = random.Random(1)
    x = [0.0] * 10
    cur_e, cur_p, cur_k = 0.0, 0, [0.0] * 10
    pc = 0; samples = []
    for e, p, ks in fr:
        if ks == "repeat": ks = cur_k
        if e == 0: ks, p = cur_k, cur_p
        tgt_e, tgt_p, tgt_k = float(e), p, ks
        jump = (cur_p == 0) != (tgt_p == 0)                     # voiced <-> unvoiced: no interpolation
        for step in range(8):
            f = (step + 1) / 8.0
            se = cur_e + (tgt_e - cur_e) * f
            sp = tgt_p if jump else round(cur_p + (tgt_p - cur_p) * f)
            sk = tgt_k if jump else [c + (t - c) * f for c, t in zip(cur_k, tgt_k)]
            for _ in range(25):
                if sp:
                    exc = (CHIRP[pc] if pc < len(CHIRP) else 0) * se / 64.0
                    pc += 1
                    if pc >= sp: pc = 0
                else:
                    exc = (se if rnd.random() < 0.5 else -se) / 2.0
                u = [0.0] * 11; u[10] = exc
                for i in range(9, -1, -1): u[i] = u[i + 1] - sk[i] * x[i]
                for i in range(9, 0, -1): x[i] = x[i - 1] + sk[i - 1] * u[i - 1]
                x[0] = u[0]
                samples.append(u[0])
        cur_e, cur_p, cur_k = tgt_e, tgt_p, tgt_k[:]
    return samples

def level(pcm):
    """Even out a phrase's loudness for the game's mix: quiet words (the colour in "Red Warrior")
    are raised towards the loudest, up to 6 times; silence stays silent. Gain from the peak of
    each 25 ms (200-sample) window, smoothed across neighbours."""
    if not pcm: return pcm
    win = 200; peaks = []
    for i in range(0, len(pcm), win): peaks.append(max(abs(v) for v in pcm[i:i + win]))
    top = max(peaks) or 1; gains = []
    for k, pk in enumerate(peaks):
        p = max(peaks[max(0, k - 1):k + 2])
        gains.append(1.0 if p < top * 0.03 else min(6.0, top * 0.8 / p))
    out = []
    for i, v in enumerate(pcm):
        k = i // win; f = (i % win) / win
        g = gains[k] * (1 - f) + gains[min(k + 1, len(gains) - 1)] * f
        out.append(v * g)
    return out

def write_bin(mem, n, path):
    """The packed file the game loads (g2_speech.bin): "G2VO", u16 count, u16 rate (8000), then
    count x (u32 offset from the file start, u32 samples), then the phrases as signed 8-bit
    samples, each scaled to its own peak."""
    clips = []
    for ph in range(n):
        start = struct.unpack("<H", mem[0x8449 + 2 * ph:0x844B + 2 * ph])[0]
        length = struct.unpack("<H", mem[0x85C3 + 2 * ph:0x85C5 + 2 * ph])[0]
        pcm = level(synth(frames(bytes(mem[start:start + length + 1]))))
        peak = max((abs(v) for v in pcm), default=0) or 1
        clips.append(bytes(int(max(-1, min(1, v / peak * 0.9)) * 127) & 0xFF for v in pcm))
    head = b"G2VO" + struct.pack(">HH", n, 8000)
    pos = len(head) + 8 * n; table = b""
    for c in clips: table += struct.pack(">II", pos, len(c)); pos += len(c)
    open(path, "wb").write(head + table + b"".join(clips))
    print("%d phrases, %d bytes -> %s" % (n, pos, path))

def main():
    zip_path, out = sys.argv[1:3]
    z = zipfile.ZipFile(zip_path)
    mem = bytearray(0x10000)
    mem[0x4000:0x8000] = z.read("136043-1120.16r"); mem[0x8000:0x10000] = z.read("136043-1119.16s")
    n = (0x85C3 - 0x8449) // 2
    if out == "--bin": write_bin(mem, n, sys.argv[3]); return
    os.makedirs(out, exist_ok=True)
    lines = []
    for ph in range(n):
        start = struct.unpack("<H", mem[0x8449 + 2 * ph:0x844B + 2 * ph])[0]
        length = struct.unpack("<H", mem[0x85C3 + 2 * ph:0x85C5 + 2 * ph])[0]
        data = bytes(mem[start:start + length + 1])
        fr = frames(data)
        pcm = synth(fr)
        peak = max((abs(v) for v in pcm), default=0) or 1
        with wave.open(os.path.join(out, "phrase_%03d.wav" % ph), "wb") as w:
            w.setnchannels(1); w.setsampwidth(2); w.setframerate(8000)
            w.writeframes(b"".join(struct.pack("<h", int(max(-1, min(1, v / peak * 0.9)) * 32767)) for v in pcm))
        lines.append("%3d  $%04x  %5d bytes  %4d frames  %.2f s" % (ph, start, length, len(fr), len(pcm) / 8000))
    open(os.path.join(out, "phrases.txt"), "w").write("\n".join(lines) + "\n")
    print("\n".join(lines[:12])); print("... %d phrases written to %s" % (n, out))

if __name__ == "__main__":
    main()
