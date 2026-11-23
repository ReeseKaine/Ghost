# Gauntlet II -> Gauntlet 4 cell code mapping (DRAFT)

G2 code | G2 meaning | confidence | uses (117 mazes) | Gauntlet 4 | category | notes
---|---|---|---|---|---|---
01 | stun tile | medium | 467 | NEW stun tile | new | stuns players standing on it
02 | wall | certain | 25519 | 09 wall | direct | Gauntlet 4 draws wall shapes from neighbours itself
03 | moveable wall | high | 182 | NEW moveable wall | new | pushed by players
04 | wall variant (static 0x8000) | low | 197 | 09 wall | adapt | unidentified wall variant; plain wall until known
05 | destructible wall | high | 1516 | 10 destructible wall (3 stages: 10 -> 0F -> 0E) | direct | Gauntlet 4 already has it (shot table 0x8828: 0E-10 -> 0x8EBE subtracts shot damage, removes below 0E); G2 rule flag 0x800000 (one-shot walls) not carried over
06 | special coloured wall (static 0x8000) | low | 403 | 09 wall | adapt | unidentified coloured wall variant
07 | wall variant (static 0x8000) | medium | 1038 | 0e special wall (trap wall?) | adapt | hypothesis: 07/08/09 are the walls traps 0A/0B/0C remove; to verify
08 | wall variant (static 0x8000) | medium | 933 | 0f special wall (trap wall?) | adapt | see 07
09 | wall variant (static 0x8000) | medium | 1068 | 11 special wall (trap wall?) | adapt | see 07
0a | trap | high | 167 | 14 trap | direct | removes trap walls
0b | trap (variant) | medium | 179 | 15 trap | adapt | trap variant
0c | trap (variant) | medium | 206 | 16 trap | adapt | trap variant
0d | door | high | 1736 | 07 door | direct | 
0e | door | high | 1842 | 08 door | direct | 
0f | player start (one chosen at random; others removed) | certain | 134 | fc player start | direct | 7 mazes have several: the arcade picks one at random each time; import: the first, others floor (or random at load later)
10 | exit (with header flags: one random exit kept, others removed or made fake) | certain | 611 | 04 exit | direct | multiple exits stay; the random-exit/fake-exit rule (header flags) is a later NEW feature
11 | exit to 6 | certain | 1 | NEW exit to 6 | adapt | Gauntlet 4 has exit to 4/8 (05/06); a general 'exit to N' is small
12 | ghost | high | 325 | 82 ghost (level 3) | direct | Gauntlet II places every monster at full strength (start colour = base, 3 health steps)
13 | grunt | high | 307 | 8a grunt (level 3) | direct | Gauntlet II places every monster at full strength (start colour = base, 3 health steps)
14 | demon | high | 144 | 92 demon (level 3) | direct | Gauntlet II places every monster at full strength (start colour = base, 3 health steps)
15 | lobber | medium | 264 | a2 lobber (level 3) | direct | Gauntlet II places every monster at full strength (start colour = base, 3 health steps)
16 | sorcerer | high | 40 | 9a sorcerer (level 3) | direct | Gauntlet II places every monster at full strength (start colour = base, 3 health steps)
17 | grunt (second code, same picture and handler as 13) | high | 2 | 8a grunt (level 3) | direct | Gauntlet II places every monster at full strength (start colour = base, 3 health steps)
18 | death | high | 325 | a8 death | direct | 
19 | acid puddle | high | 364 | NEW acid puddle | new | wandering hazard, magic only stuns
1a | super sorcerer | medium | 196 | NEW super sorcerer (fallback: sorcerer 9a) | new | teleports, shoots fireballs
1b | IT | high | 25 | NEW IT | new | tag: monsters chase whoever is IT
1c | ghost generator, level 1 | certain | 616 | 84 ghost generator level 1 | direct | type from spawn table 0x5798A (used by spawn routine 0x49300); level from shot handlers
1d | ghost generator, level 2 | certain | 699 | 85 ghost generator level 2 | direct | type from spawn table 0x5798A (used by spawn routine 0x49300); level from shot handlers
1e | ghost generator, level 3 | certain | 343 | 86 ghost generator level 3 | direct | type from spawn table 0x5798A (used by spawn routine 0x49300); level from shot handlers
1f | grunt generator, level 1 | certain | 271 | 8c grunt generator level 1 | direct | type from spawn table 0x5798A (used by spawn routine 0x49300); level from shot handlers
20 | grunt generator, level 2 | certain | 268 | 8d grunt generator level 2 | direct | type from spawn table 0x5798A (used by spawn routine 0x49300); level from shot handlers
21 | grunt generator, level 3 | certain | 185 | 8e grunt generator level 3 | direct | type from spawn table 0x5798A (used by spawn routine 0x49300); level from shot handlers
22 | demon generator, level 1 | certain | 208 | 94 demon generator level 1 | direct | type from spawn table 0x5798A (used by spawn routine 0x49300); level from shot handlers
23 | demon generator, level 2 | certain | 138 | 95 demon generator level 2 | direct | type from spawn table 0x5798A (used by spawn routine 0x49300); level from shot handlers
24 | demon generator, level 3 | certain | 100 | 96 demon generator level 3 | direct | type from spawn table 0x5798A (used by spawn routine 0x49300); level from shot handlers
25 | lobber generator, level 1 | certain | 73 | a4 lobber generator level 1 | direct | type from spawn table 0x5798A (used by spawn routine 0x49300); level from shot handlers
26 | lobber generator, level 2 | certain | 19 | a5 lobber generator level 2 | direct | type from spawn table 0x5798A (used by spawn routine 0x49300); level from shot handlers
27 | lobber generator, level 3 | certain | 52 | a6 lobber generator level 3 | direct | type from spawn table 0x5798A (used by spawn routine 0x49300); level from shot handlers
28 | sorcerer generator, level 1 | certain | 84 | 9c sorcerer generator level 1 | direct | type from spawn table 0x5798A (used by spawn routine 0x49300); level from shot handlers
29 | sorcerer generator, level 2 | certain | 99 | 9d sorcerer generator level 2 | direct | type from spawn table 0x5798A (used by spawn routine 0x49300); level from shot handlers
2a | sorcerer generator, level 3 | certain | 63 | 9e sorcerer generator level 3 | direct | type from spawn table 0x5798A (used by spawn routine 0x49300); level from shot handlers
2b | grunt (second code 17) generator, level 1 | certain | 19 | 8c grunt generator level 1 | direct | type from spawn table 0x5798A (used by spawn routine 0x49300); level from shot handlers
2c | grunt (second code 17) generator, level 2 | certain | 48 | 8d grunt generator level 2 | direct | type from spawn table 0x5798A (used by spawn routine 0x49300); level from shot handlers
2d | grunt (second code 17) generator, level 3 | certain | 12 | 8e grunt generator level 3 | direct | type from spawn table 0x5798A (used by spawn routine 0x49300); level from shot handlers
2e | treasure | high | 2305 | 1e treasure | direct | 
2f | solid object, probably locked treasure chest | medium | 234 | NEW locked treasure chest | new | as 0f
30 | treasure (score scales with level) | high | 0 | 1c treasure (bonus multiplier) | adapt | arcade: points scale with level
31 | food | high | 363 | 22 food | direct | 
32 | random food | high | 160 | 25 food | adapt | arcade: random food picture
33 | magic potion | high | 127 | 23 magic potion | direct | arcade may swap some for permanent power-up potions (picture 20fc) -> 26-2b
34 | magic potion | high | 32 | 24 magic potion | direct | see 33
35 | key | high | 679 | 1b key | direct | 
36 | amulet: invisibility | certain | 32 | 1d invisibility | direct | Gauntlet 4 already has it
37 | amulet: repulsiveness | certain | 34 | NEW amulet: repulsiveness | new | monsters flee
38 | amulet: reflective shots | certain | 68 | NEW amulet: reflective shots | new | shots bounce off walls
39 | amulet: transportability | certain | 19 | NEW amulet: transportability | new | walk through walls
3a | amulet: super shots (x10) | certain | 66 | NEW amulet: super shots | new | 10 shots that go through monsters
3b | amulet: invulnerability | certain | 29 | NEW amulet: invulnerability | new | no damage, timed
3c | dragon (4x4, special placement) | high | 0 | NEW dragon | new | 4x4, shoot the head
3d | unused (picture 0bfc) | - | 0 | drop | drop | no maze uses it
3e | transporter | high | 205 | 13 transporter | direct | arcade: moves you to the closest visible transporter
3f | force field | high | 180 | NEW force field | new | switches on and off, hurts

## Header conversion (Gauntlet II 11 bytes -> Gauntlet 4 14 bytes)

G2 byte | meaning (from the G2 code) | Gauntlet 4
---|---|---
0 | level type 0-17 -> $904065 (treasure/special checks) | not used (bytes 0-2 = initials, not read: write "g2" + type)
1-4 | rule flags, packed into $90491C (bits 26/27 mirror x/y, ORed with random mirroring in play; low byte $90491F: bit 3 trap shuffle, bits 4/5 wrap vertical/horizontal, bit 6 fake exits; bits 14/15 exit modes; more unnamed) | byte 7 options = 0 for now; rules kept for the NEW features list
5 | high: floor pattern 0-8 ($904B5C), low: wall pattern ($904B5E, 0-4 in 106 mazes, 6-15 in 11) | byte 8 = same nibbles; wall pattern > 4 -> 0 (to check visually)
6 | high: floor colour set (palette loader 0x43486 arg a, table 0x5D5C8), low: wall colour set (arg b, table 0x5D7E8) | byte 9 = nearest first-game set per nibble (levels/g2_colour_to_g4.json; Gauntlet II renumbered: floor 3-10 shifted by one); exact colours via the arcade-colours mode
7-10 | decoder state (byte 8 seeds the first cell) | not needed: the map is taken decoded
- | (G4 bytes 10-13: purpose unknown) | most common value of G4's Arcade records

The game also escalates rules with play count (0x436FE: random extra rule 0x436CC, wrap bits 0x30) and mirrors mazes at random: optional later.
