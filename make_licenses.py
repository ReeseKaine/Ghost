#!/usr/bin/env python3
"""make_licenses.py -- write host/win/THIRD_PARTY_LICENSES.txt: the licence notices of the other
people's code built into ghost.exe (and SDL3.dll), copied from the files in this project.
Run it from the project root after updating any of those libraries. The file goes in the
release ZIP next to ghost.exe (MIT, BSD and LGPL all ask for their notice to travel with the
program)."""
import os, re

HERE = os.path.dirname(os.path.abspath(__file__))
H = os.path.join(HERE, "host")

def text(p):
    return open(os.path.join(H, p), encoding="utf-8", errors="replace").read().replace("\r\n", "\n")

def between(s, start, end=None):
    i = s.index(start)
    j = s.index(end, i) if end else len(s)
    return s[i:j].rstrip() + "\n"

def comment_block(s, start, end):
    """lines of a C comment from 'start' up to (not including) 'end', comment markers removed"""
    out = []
    for line in between(s, start, end).splitlines():
        out.append(re.sub(r"^\s*(/\*+|\*+/|\*)\s?", "", line).rstrip())
    return "\n".join(out).strip() + "\n"

parts = []
def add(title, what, body):
    parts.append("=" * 78 + "\n" + title + "\n" + what + "\n" + "=" * 78 + "\n\n" + body.strip() + "\n\n")

add("Musashi 68000 CPU emulator", "host/musashi/ (built into ghost.exe; MIT License)",
    between(text("musashi/readme.txt"), "Copyright © 1998-2001 Karl Stenerud", "\n\n\n"))

sf = text("musashi/softfloat/softfloat.c")
add("SoftFloat (part of Musashi)", "host/musashi/softfloat/ (built into ghost.exe)",
    comment_block(sf, "This C source file is part of the SoftFloat", "=====\n*/") if "=====\n*/" in sf
    else comment_block(sf, "This C source file is part of the SoftFloat", "*/"))

add("Nuked-OPN2 YM2612 emulator", "host/ym/ (built into ghost.exe; GNU LGPL 2.1 or later)",
    comment_block(text("ym/ym3438.c"), "Copyright (C) 2017", "*/") +
    "\nThe complete source code of Nuked-OPN2 as used in ghost.exe is in the Ghost source\n"
    "code (folder host/ym/), together with everything needed to rebuild ghost.exe with a\n"
    "changed version of it (host/build_win.sh, host/build.sh). The full licence text follows.\n\n" +
    text("ym/LICENSE"))

add("ymfm YM2151 emulator", "host/ymfm/ (built into ghost.exe; BSD 3-Clause License)", text("ymfm/LICENSE"))

add("QOA audio format", "host/mp3/qoa.h (built into ghost.exe; MIT License)",
    "Copyright (c) 2023, Dominic Szablewski - https://phoboslab.org\nSPDX-License-Identifier: MIT\n\n" +
    between(text("musashi/readme.txt"), "Permission is hereby granted", "\n\n\n"))

add("dr_mp3", "host/mp3/dr_mp3.h (built into ghost.exe; public domain or MIT No Attribution)",
    between(text("mp3/dr_mp3.h"), "This software is available as a choice", "*/"))

add("stb_image and stb_image_write", "host/stb/ (built into ghost.exe; MIT License or public domain)",
    between(text("stb/stb_image.h"), "This software is available under 2 licenses", "*/"))

add("SDL3", "SDL3.dll on Windows, libSDL3.so.0 on Linux (zlib License)", text("win/sdl/SDL3-3.4.20/LICENSE.txt"))

head = ("THIRD-PARTY LICENCES\n====================\n\n"
        "Ghost (ghost.exe on Windows, ghost on Linux) contains code written by other people,\n"
        "listed below with their licence notices. Ghost's own code is under the MIT License\n"
        "(see LICENSE in the source code). None of these licences, nor Ghost's, grant any\n"
        "rights to Gauntlet 4 or the other games: you need your own copies of them.\n\n")
out = head + "".join(parts)
path = os.path.join(H, "win", "THIRD_PARTY_LICENSES.txt")
open(path, "w", encoding="utf-8", newline="\r\n").write(out)
print("wrote", path, len(out.splitlines()), "lines")
