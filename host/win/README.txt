GHOST -- an unofficial PC port of Gauntlet 4
=============================================

This folder plays Gauntlet 4 (Sega Mega Drive / Genesis, 1993) from a C translation of the
original game code. It is not an emulator running the cartridge: the game's own code was
translated to C, and only the console hardware around it (video, sound chips, controllers)
is emulated.


1. WHAT YOU NEED
----------------

  ghost.exe      the game
  SDL3.dll        window, sound and gamepad library (keep it next to ghost.exe)
  THIRD_PARTY_LICENSES.txt   licences of the other people's code inside ghost.exe
  a ROM           your own copy of the Gauntlet 4 cartridge (September 1993 version)

Put your ROM in this folder and name it:

  gauntlet4.bin

All the launchers (.bat files) use gauntlet4.bin when it is here.

GRAPHICS PACKS (new): replacement pictures for the game's graphics, at any resolution.
Choose one in the launcher: Video -> Graphics pack. A pack is a folder inside "packs" next
to ghost.exe, for example packs\MyPack\, with
  pack.txt   three lines:   name = My Pack    author = me    scale = 2
             (scale: how much bigger the pictures are -- 2 = double, 4 = four times)
  tile_XXXXXXXX.png    a background tile: walls, floors, items, doors, the side panel
                       (8 x scale pixels square, e.g. 16x16 at scale 2)
  sprite_XXXXXXXX.png  a sprite tile: heroes, monsters, shots (8 x scale square; a sprite
                       is made of several of these). It may use its whole square, even
                       where the original was see-through.
  g2pic_NN.png         a Gauntlet II picture: 01-10 dragon (asleep, half awake, heads),
                       11-12 and 20-47 the dragon's other sides, 50-59 acid, 60 locked
                       chest, 61 poison, 70-85 force-field posts. Its size = the original
                       size x scale (the dragon is 32x32, acid/chest/poison 24x24, posts 16x16).
                       And the port's own drawn shapes (16x16 unless noted):
                       100 IT, 101 stun tile, 102/103 force-field beam green (across /
                       up-down), 104/105 red, 106/107 switched off, 108 the dragon's
                       fireball (32x32), 110-114 amulets (in the order of the level file's
                       extras 5-9), 115 the EXIT TO 6 square.
  g2pic_NN_F.png       frame F (0, 1, 2 ...) of picture NN: give frames and the picture is
                       animated, a new frame every "frame_ticks" game frames (a line
                       "frame_ticks = 8" in pack.txt; 8 if left out). Any picture or shape
                       may be animated this way -- e.g. sparkling beams or a flickering
                       fireball. Without frames, g2pic_NN.png is used.
  attract_XXXXXXXX.png a still Gauntlet II attract screen (the title, the credits, the
                       high scores ...), its full size x scale. The XXXXXXXX part is a
                       fingerprint of the screen. Screens that keep changing (the demo
                       game, the colour-cycling logo) cannot be replaced this way.
  The game reuses its tile places for different pictures, so each tile is known by a
  fingerprint of its picture (the XXXXXXXX part). Anything the pack leaves out keeps the
  game's own picture, so a pack can be as small as a new set of floors. Transparent pixels:
  in a tile they show the game's own pixel; in a sprite or a Gauntlet II picture they show
  what is behind it (so a redrawn monster or dragon may have its own outline).
  Colours: a replaced picture keeps the colours you drew, but follows the game when it dims,
  flashes or pulses a colour; a replaced hero takes its player's colour (red/blue/yellow/
  green) on its own colour parts; super sorcerers get their maroon tint.
  Making one: double-click dump_tiles.bat and play; every tile, sprite tile and Gauntlet II
  picture shown is saved once into tiles_dump with its correct name (original size, as the
  game draws it), and so is each Gauntlet II attract screen that stays still for half a
  second (leave the game at the title to see them). Copy the ones you want into your pack's folder, redraw them at your scale,
  done. Those saved pictures come from your own ROM: share your own drawings, not them.

ONLINE FROM INSIDE THE GAME (new): after the title, SELECT PLAY offers LOCAL GAME (the usual
MODE SELECT) or ONLINE: HOST GAME or JOIN GAME.
  HOST GAME: you are 1P (red). The screen lists who has joined and in which colour; press
    Start when everyone is in (B cancels). Others join you on port 47100: on a home network
    they type your computer's address (e.g. 192.168.1.20); over the internet your router must
    pass port 47100 (TCP) to your computer. Windows may ask once whether to allow it: allow.
  JOIN GAME: type the host's address on the keyboard and press Enter (Esc goes back; the last
    address is remembered). Pick your colour with left/right (2P blue, 3P yellow, 4P green).
  When the host starts, every copy restarts once by itself (the window closes and reopens),
  connects, skips the title and lands in MODE SELECT. In the menus only the host's controller
  moves the cursor; everyone plays their own hero in the game.
  (host.bat / join.bat still start an online game directly, as before.)

THE LAUNCHER (new): just double-click ghost.exe. A start-up screen opens:
  Play / Video / Audio / ROM folder (pick the folder with your ROMs -- any file names: they
  are recognised by their fingerprints) / Build the data files / Quit. (Online play is
  started from inside the game: SELECT PLAY -> ONLINE.)
  Video: Resolution (a list of standard sizes), Display (window, fullscreen, or borderless --
  a full-screen window), Aspect ratio (4:3 as on a TV, square pixels, stretch to fill, or
  sharp -- whole-number steps only, every pixel the same size), Graphics pack (see below).
  Audio: Music, Effects and Voice volume (0-10, left/right; music 0 = no music).
"Build the data files" makes what it can from the ROMs found: so far the Gauntlet II
narration, sound effects, pictures, level set, attract screens and colours (from the Gauntlet II zip -- the
level set also needs the Gauntlet 4 ROM -- in a few seconds) and the clean Gauntlet 4 music
(from the Gauntlet 4 ROM; about 5 to 10 minutes in the background -- the screen shows which
track it is on, and Esc stops it). With the original arcade Gauntlet zip (and the Gauntlet 4
ROM) it also makes arcade_colours.txt, the arcade colours for Gauntlet 4's own levels.
With Deeper Dungeons -- the Atari 8-bit disk (.atr) or the Commodore 64 tape (side 2,
.tap) -- next to the ROMs, plus the Gauntlet 4 ROM, it makes gauntlet3_levels.bin, the
GAUNTLET III level set (see below); side 2 of the Atari Gauntlet disk (.atr) -- or the
Commodore 64 Gauntlet as a cartridge file (.crt, e.g. a one-load conversion) -- adds the
Atari's own mazes. So the Commodore 64 versions alone (tape + cartridge) are enough. The launcher knows the files by what is on them, so their names don't
matter. With both Deeper Dungeons copies the Atari disk is used (all 11 treasure rooms;
the tape has 6, and its mazes 47 and 53 are slightly different).
Everything make_data used to make now comes from the launcher -- no Python needed.
The pause menu has the same three volumes (left/right); both places share them.
Below it shows what it found: the Gauntlet 4 ROM, the arcade Gauntlet and Gauntlet II
zips, and which
data files are ready. Keys: up/down choose, left/right change, Enter (or A on a gamepad)
starts, Esc (or B) quits. Its settings are kept in launcher.cfg in this folder. The .bat
files still work as before.

ARCADE ZIPS: use a NON-MERGED MAME set. gauntlet.zip (Gauntlet) and gaunt2.zip
(Gauntlet II) must each hold all of their own files. In a merged set, Gauntlet II's files
are packed inside gauntlet.zip instead, and in a split set some shared files are missing
from gaunt2.zip. The zip names themselves don't matter: the launcher knows the files
by what is in them.

To check you have the right version, compare its fingerprint (any ROM manager or hash tool
shows these). The September 1993 ROM is 1,048,576 bytes with
  CRC32  f9d60510
  SHA-1  d28e22207121f0e2980dd409b4fb24f9fb8967ae
The August 1993 ROM (CRC32 3bf46dce) is not supported.

Everyone who plays online needs this whole folder and their own ROM. The ROMs must be the
same version: the host refuses a player whose ROM is different.


2. PLAYING ON YOUR OWN COMPUTER
-------------------------------

Double-click one of these:

  play.bat     normal game (controller ports 1 and 2)
  play4.bat    with the Sega Team Player adapter connected (players 1-4 on one computer)

A console window opens beside the game; it shows messages (gamepads connecting, errors).
It stays open after you quit so you can read it. Press a key to close it.

Esc or closing the game window quits.


3. CONTROLS
-----------

Keyboard, player 1:
  Arrow keys   move
  Z            A button
  X            B button
  C            C button
  Enter        Start

Keyboard, player 2:
  W A S D      move
  F            A button
  G            B button
  H            C button
  Space        Start

Gamepads (Xbox, PlayStation, Switch Pro and most others):
  D-pad or left stick              move
  X / Square   (left face button)  A button
  A / Cross    (bottom)            B button
  B / Circle   (right)             C button
  Y / Triangle (top)               C button as well
  Start / Options                  Start

Gamepads can be plugged in before or while playing. The first one connected is player 1,
the second player 2, and so on (players 3 and 4 only with play4.bat). Unplugging one frees
its slot; the next gamepad plugged in takes it. Until a second player exists (a second
gamepad, or the keyboard's player 2 keys used once), the game is told nothing is plugged
into port 2, so player 1 can choose any hero, the Valkyrie included.

In the game: on the level screen press Start to join, then C to begin (the screen shows
"PUSH START" / "PUSH C-BTN"). To play another hero, move the d-pad up/down on the level
screen BEFORE pressing Start. The menus are moved through with the d-pad and Start.
Story text (Quest mode): hold A or B (or Start) to hurry it, C turns the page.


3a. EXTRAS (NOT IN THE ORIGINAL GAME)
-------------------------------------

  Select Game   After choosing Arcade or Record mode, a SELECT GAME screen asks for
                GAUNTLET, GAUNTLET II (the Gauntlet II mazes; needs gauntlet2_levels.bin)
                or GAUNTLET III (the Atari mazes; needs gauntlet3_levels.bin), each file
                next to ghost.exe -- without either the screen is skipped.

  Start Level   Arcade and Record mode Options: Level 1 (normal) or Level 8. With
                8 the game starts on the real level 8 map, every time, with the level-1
                song. (Through the game's own rules level 8 varies: a random map with a
                hidden power-up the first time after switching on, later the level the
                last game ended on.) Change it in each mode's Options screen before
                starting.

  Hero choice   Arcade and Record mode: every player can be any hero, and two players
                can be the same hero. On "SELECT PLAYER BY LEVER UP/DOWN" (and before
                joining a game in progress) press up/down to change your hero; the name
                on your panel changes with it. Each player keeps their colour: player 1
                red, 2 blue, 3 yellow, 4 green -- a red Elf, a blue Wizard, and so on.
                Quest and Battle mode keep the original choice (one of each hero).

  Logo          In Arcade and Record mode the logo beside the maze names the game you are
                playing: GAUNTLET for the Gauntlet mazes, GAUNTLET II for the Gauntlet II
                mazes, GAUNTLET III for the Atari ones. Quest and Battle mode keep
                GAUNTLET 4.

  Pause menu    Press Start during play to pause: a menu appears under the PAUSE box.
                Up/Down choose, Start (or A / C) confirms: RESUME        carry on EXIT TO
                MENU  end the game and go back (Arcade: title screen, Record/Battle: mode
                select) It uses the game's own unpause and quit. Not in Quest mode, whose
                pause shows the map. Online, any player can steer the menu.

  Arcade colours
                Floors and walls in the colours of the 1985 arcade game, in full colour
                (beyond the Mega Drive's 512). Switch it in the pause menu (COLOURS:
                MEGA DRIVE / ARCADE) at any time; add  --arcade-colours  to a launcher to
                start with it on. It needs arcade_colours.txt next to ghost.exe, made from
                the arcade ROM set (gauntlet.zip) by arcade_colours.py; without the file
                the menu item does not appear. It changes only the picture, so online
                players can each choose their own.

  GAUNTLET III  Arcade and Record mode play the mazes of the Atari 8-bit Gauntlet's
                Deeper Dungeons disk (128 mazes) plus the 20 mazes of the Atari's own
                Gauntlet disk that are not arcade Gauntlet mazes (the rest of that disk
                is the arcade game's): 126 mazes and 22 treasure rooms. Choose GAUNTLET
                III on the SELECT GAME screen (or --level-set gauntlet3). As on the Atari:
                levels 1-7 are mazes 1-7, then the others in a shuffled order, each one
                turned a random way of four; a treasure room on level 14 and then every
                4 to 7 levels. As on the Atari, a treasure room gives you 20 to 30
                seconds: treasure there scores nothing when you pick it up, but get out
                through an exit in time and each hero scores 100 for every treasure they
                took; run out of time and there is no bonus. Some mazes make shots
                hurt or stun other players (shown at the start). Poisoned food (the skull
                flask) costs health and makes you dizzy, as in Gauntlet II. It plays like
                Gauntlet otherwise: no Gauntlet II extras, Gauntlet 4's music and voice.
                Colours as on the Atari: each maze has its walls in one colour (grey,
                gold, red, purple, blue, aqua or green). With COLOURS: MEGA DRIVE the
                nearest Gauntlet 4 wall colours are used; with COLOURS: ARCADE the Atari's
                own shades on a black floor. Needs gauntlet3_levels.bin
                next to ghost.exe (launcher: Build the data files). Online, everyone
                needs the same file.
                Why "GAUNTLET III"? The Deeper Dungeons were the original game's real
                follow-up. The games actually sold as Gauntlet III -- two different ones,
                by two different developers -- play nothing like the original. The less
                spoken about the real Gauntlet III, the better.

  Sound effects In the Gauntlet II mazes the arcade's own sound effects play for the things
                Gauntlet 4 has no sound for: the dragon's head struck (a new pitch each
                time), a chest opening, a stun tile, acid, becoming IT, the exit moving,
                poison, a reflected shot, pushing a wall. They come from g2_sfx.bin, which
                "Build the data files" records from the Gauntlet II zip by running the
                arcade's sound board (YM2151 by ymfm, Aaron Giles, BSD licence). The
                Effects volume sets them.

  Gauntlet II levels
                Arcade and Record mode play the 117 mazes of the 1986 arcade Gauntlet II
                instead of Gauntlet 4's: choose GAUNTLET II on the SELECT GAME screen,
                or start with play_g2.bat (--level-set gauntlet2) to have it chosen
                already. Online, everyone needs the same file.
                Needs gauntlet2_levels.bin next to ghost.exe, made
                from the Gauntlet II ROM set by import_gauntlet2.py and g2_convert.py.
                Working: destructible walls (shoot them: 3 stages), secret walls (look
                like walls; shoot one to find food or a potion), random walls (appear and
                vanish), cycling walls (three groups take turns to vanish, every 2
                seconds), the mazes' trap rules (some groups start open; in some mazes
                each trap opens a random group), a lone transporter (puts you beside it,
                on the side nearest the exit), wrap-around mazes (walk off one side, come
                back on the other), the "exit to 6" shortcut on level 1, stun tiles
                (freeze you briefly, then vanish), force fields (beams between posts that
                switch on and off and hurt while on), amulets: you pick them up (jewels on
                the floor);
                invulnerability (15 s) and transportability (walk into a wall one block
                thick to come out on the other side, until the level ends) work;
                reflective shots work (shots bounce off walls like the Wizard's, up to
                4 times each, until the level ends) and super shots (each amulet gives
                11 shots that destroy any monster or generator in one hit and fly on
                through -- not Death) and repulsiveness (monsters run from you for
                15-20 seconds) work.
                Acid puddles (green, wander about, cannot be shot) take 60 health and
                stun you briefly when you touch them; the puddle is used up.
                Magic stuns the acid puddles near you (they blink and stop); magic again
                while they are stunned destroys them.
                Poison (a flask with a skull) can turn up from level 3: touching it costs
                50 health and makes you dizzy for 10-20 seconds (like Gauntlet 4's confuse
                tile; eating food cures it at once); shooting it
                slows the monsters for 10 seconds.
                Treasure rooms: as in the arcade, the first comes on level 9, 10 or 11,
                then every 4 to 6 levels (an exit to 6 starts the count again); the
                next level is one of Gauntlet II's
                treasure rooms -- collect what you can and reach the exit within 20-26
                seconds for a bonus of 100 x players x treasures collected.
                IT (purple orb marked IT) hunts the nearest hero; whoever it touches
                becomes IT and every monster then chases that hero until the level ends
                (with several players, touching another hero passes IT on).
                Locked chests: walk into one with a key to open it -- treasure, food,
                a red potion, a Death, or (rarely) a hidden power potion.
                Moveable walls: some walls can be pushed (walk into them; they slide
                a block at a time onto open floor).
                Super sorcerers blink in and out like sorcerers and throw fireballs
                (a slight maroon tint tells them apart).
                The thief (Gauntlet 4's own) comes from level 8 on and goes for the
                richest hero; kill him to get the stolen item back, or it turns up on the
                next level. As in Gauntlet II he keeps coming back, but with more time to
                breathe: 90 seconds after one is gone (the arcade waits 60), the next
                one comes in. He always takes the shortest way to you -- through a
                transporter if that is quicker -- and teleporting does not shake him
                off. Sometimes it is a mugger instead (his own Gauntlet II look, read
                from the Gauntlet II zip, and a quarter slower than the thief): he takes
                100 health rather than an item ("KILL MUGGER TO RECOVER FOOD") -- but
                shoot him and all he drops is a bottle of cider, as in the arcade. The first on a level is a mugger half the time; after a
                theft the next one is always a mugger, after a mugging a thief.
                The dragon (Gauntlet II's own picture, from g2_sprites.bin next to
                gauntlet2_levels.bin) guards 18 of the mazes from level 12 on. It sleeps
                until someone comes within 4 squares, wakes up and then stays awake: it
                sways its head and breathes fire in one of five patterns, keeps breathing
                while you stand in front of its mouth, and turns towards the nearest hero
                if that side is open. Only shots at its head hurt it -- 20 hits slay it
                and it leaves treasure. Magic stuns it for a few seconds.
                Invisible walls: in mazes 6, 23 and 101 all walls are invisible, and in
                94, 97, 101 and 115 the trap walls are -- they are still there and still
                block you, as in the arcade.
                Random rules: as in the arcade, very long games get harder. From level
                104 some mazes wrap around; from level 201 a maze may also get a random
                rule (player shots stun or hurt other players, fake exits, or players
                can go off screen), shown in a message when the maze starts. "Off
                screen": the camera follows the middle of the group and nobody is held
                at the screen's edge, so heroes can wander out of sight. The pattern repeats every 400 levels.
                Narration: with g2_speech.bin next to ghost.exe the arcade Gauntlet II
                voice speaks: "Red Warrior" (your colour and hero) when you join, and
                "... needs food, badly" and "... is about to die" as your health runs
                low, "... shot the food", "... is now IT", the maze's rules (invisible
                walls, shots that stun or hurt), the treasure room, and the amulets.
                Gauntlet 4's own sounds stay. make_data.sh makes the file from your
                gauntlet2.zip.
                Attract screens: with g2_attract.bin next to ghost.exe, Gauntlet 4's
                attract loop (left alone at the title) is followed by the arcade
                Gauntlet II screens: the title with its colour-cycling logo, the demo
                game, the LEGEND, MONSTERS, the credits and SCORE PER COIN (about 3
                minutes), then Gauntlet 4's title again.
                Music for these screens: music\g2_attract.mp3 (see "Your own music").
                Your own music: make a folder called "music" next to ghost.exe and put
                MP3s in it, recorded yourself from your own copies of the games. Each
                one replaces a piece of music; anything without a file keeps the game's
                own music. Clean recordings also avoid the Mega Drive's habit of
                dropping instruments from the music while sound effects play.
                  g4_XX.mp3        Gauntlet 4's track XX (see the list below). It starts
                                   whenever the game would start that track, so the
                                   game's own order is kept. To see which number plays
                                   where, run music_log.bat: it lists every track the
                                   game starts (also in music_log.txt).
                Gauntlet 4's tracks (names from the game's own Sound Test, Options ->
                Sound Test -> BGM, in the same order; "once" = plays once, no loop):
                  g4_00.mp3  Main theme (title screen, once)
                  g4_01.mp3  Treasure Room 1P (once)  \
                  g4_02.mp3  Treasure Room 2P (once)   | the treasure room's timer tune,
                  g4_03.mp3  Treasure Room 3P (once)   | one per number of players
                  g4_04.mp3  Treasure Room 4P (once)  /
                  g4_05.mp3  Back of Beyond
                  g4_06.mp3  Whisper of Phantom     \
                  g4_07.mp3  March in the Dark       | the five level tunes: Arcade mode
                  g4_08.mp3  Sortie                  | picks one at random on level 1
                  g4_09.mp3  Transparent Obstacle    | and lets it play on
                  g4_0A.mp3  Adventures of Iron     /
                  g4_0B.mp3  Fire Starter
                  g4_0C.mp3  Fire Starter (second version)
                  g4_0D.mp3  Retribution
                  g4_0E.mp3  Crux
                  g4_0F.mp3  The Spark
                  g4_10.mp3  Betrayer (short, once)
                  g4_11.mp3  Explosion (once)
                  g4_12.mp3  "..." (short loop)
                  (13 and 14 are "BGM stop" and "BGM fade out": no music.)
                Where 05 and 0B-12 play (Quest mode, the boss, game over, ...) is not
                worked out yet: music_log.bat shows it as you play.
                  g2_level.mp3     Gauntlet II levels: starts on level 1 and plays on.
                  g2_treasure.mp3  Gauntlet II treasure rooms.
                  g2_secret.mp3    Gauntlet II secret rooms.
                                   The music changes only when you enter or leave a
                                   treasure or secret room.
                  g2_attract.mp3   the Gauntlet II attract screens.
                Each MP3 loops. --nomusic turns all music off, MP3s included.
                Clean Gauntlet 4 music without recording anything: double-click
                extract_music.bat once (about 5 minutes, no window). It plays every
                Gauntlet 4 tune from your own ROM with no sound effects in the way,
                finds where each one loops, and saves them all in g4_music.bin next to
                ghost.exe (about 58 MB). From then on the game plays those recordings
                instead of the Mega Drive music, so no instrument drops out when a
                sound effect plays. Your own g4_XX.mp3 files still come first. Delete
                g4_music.bin to go back to the original sound.
                To listen to the recordings on their own: double-click music_to_wav.bat.
                It writes every track as a WAV file (g4_00.wav, g4_01.wav, ...) into a
                folder called music_wav; open them in any media player. Looping tunes
                play their loop twice, so you can hear the jump back. Start skips to the mode select, as usual. Not shown in
                online games. make_data.sh makes the file from your gauntlet2.zip.
                Mirroring: as in the arcade, every maze is flipped at random each time
                you play it (left-right, top-bottom, both, or not at all).
                Order: Arcade mode follows Gauntlet II's own order -- mazes 1-6, then
                every second maze up to 102, then round again with a growing skip;
                level 6 (and the exit to 6) becomes the maze your last game ended on
                once you have got past level 6. Record mode keeps its random mazes.
                Exits: in some mazes only one exit is real (chosen anew each time) --
                in some the others are fakes that do nothing, and in some the real exit
                jumps to another spot every 5 seconds (it blinks just before).
                Arcade colours work here too, with Gauntlet II's own floor and wall
                colours for every maze.
                Secret tricks and secret rooms: from level 7 each maze hides a
                "secret trick" (exit while you are IT, save your super shots, don't
                shoot the food, touch acid, fight Death, ...). Do it and the next level
                is one of Gauntlet II's two secret rooms: collect treasure against the
                clock -- when the room's condition is met the walls turn into exits --
                and get out in time for a bonus. With 2+ players, collecting treasure
                raises your score multiplier (and lowers the others').
                (Gauntlet 4's own countdown treasure rooms are off in this mode.)

  Original layouts (built into Gauntlet 4, not an extra): Arcade Options "Game Type"
                switches a few levels between Tengen's versions and the arcade originals.

The extras can all be switched off: add  --original  to the ghost.exe line of a launcher
for the game exactly as on the cartridge. For online play everyone must use the same
setting (the host refuses a player whose setting differs, with the "different ROM" message).


4. PLAYING ONLINE
-----------------

Up to four players, each on their own computer. Each computer runs the whole game, and
only the button presses travel over the internet, so very little data is sent.

One person is the HOST (always player 1). The others JOIN (players 2, 3, 4, in the order
they connect).

  HOST:  double-click host.bat
         type the total number of players (2, 3 or 4) and press Enter
         wait: the game starts when everyone has joined

  JOIN:  double-click join.bat
         type the host's address and press Enter

With 3 or 4 players the Team Player adapter is switched on automatically.

Your own controls (keyboard player 1 keys, player 2 keys or any gamepad) all control
your player, whichever number you are. The window title shows which player you are.


   4a. Letting your friends reach the host

The joining players must be able to reach the host's computer on TCP port 47100. There
are two ways:

  EASIEST -- a virtual network (no router settings needed)
    Install Tailscale (https://tailscale.com, free for personal use) on every computer and
    sign in to the same network (the host can invite the others). Each computer gets an
    address like 100.x.y.z; the joining players type the HOST's Tailscale address in
    join.bat. ZeroTier works the same way.

  OR -- port forwarding on the host's router
    In the router's settings, forward TCP port 47100 to the host computer's local address
    (shown by running ipconfig in a command prompt, "IPv4 Address", e.g. 192.168.1.23).
    The joining players then type the host's public address (search "what is my IP" on
    the host computer).

  On the same home network, the joining players can type the host's local address
  (the 192.168... one) directly.

The first time you host, Windows Firewall asks whether ghost.exe may use the network.
Allow it (Private networks is enough for Tailscale or a home network; allow Public too
if you use port forwarding and it does not connect).


   4b. Input delay

A button press takes effect a few frames after you press it. That gives time for every
player's presses to reach every computer, so all of the games stay identical. The default
is 4 frames (about 1/15 of a second), which suits most connections.

If the game stutters or keeps pausing, the host can raise it: open host.bat in Notepad and
add  --delay 6  (or 8) at the end of the ghost.exe line. Lower (2 or 3) feels snappier on
a fast connection. Only the host's setting counts; it is sent to everyone.


   4c. Online messages (in the console window)

  "net: hosting on port 47100, waiting for N more player(s)"   host is ready
  "net: a player joined (2 of 3)"                              someone connected
  "net: you are player 2 of 3 ... starting"                    the game begins
  "net: refused a player with a different ROM"                 ROM versions differ
  "net: player N disconnected"                                 someone left: the game ends
  "net: DESYNC ... at frame N"                                 the games have stopped
                                                               matching (should not happen:
                                                               please note the frame number)


5. TROUBLESHOOTING
------------------

  The window opens and closes at once
      A message box (and the console window) says why. Usually the ROM was not found: check
      that gauntlet4.bin is in this folder.

  "Wrong Gauntlet 4 version"
      Your ROM is the August 1993 version. Only the September 1993 version works (CRC32
      f9d60510).

  "Unrecognised ROM"
      The ROM is not the plain September 1993 one: it may be patched (for example with the
      SRAM save patch), hacked, or a different game. The program tries it anyway, but it may
      glitch or crash; the unpatched September 1993 ROM is the safe choice.

  "call into untranslated code at XXXXXX"
      The game reached a piece of code that has not been translated to C yet. Note the
      address and what you were doing; it can be added.

  No sound
      The console says "no sound: ..." if Windows offered no sound device. The game still
      runs (paced by a timer instead).

  The joining player cannot connect ("cannot connect to the host")
      Check the address, that the host started host.bat first, the firewall question on the
      host, and the port forwarding / Tailscale setup in 4a.

  Crackling or stuttering sound
      The game keeps about 60 ms of sound queued. On a slow or busy computer, more helps:
      add  set G4LATENCY=120  (milliseconds) as a line before ghost.exe in the launcher.

  "cannot listen on that port (in use?)"
      Another copy of the game is still hosting. Close it and try again.


6. COMMAND LINE (for the curious)
---------------------------------

  ghost.exe ROM [options]

  --window              play in a window (the launchers use this)
  --frames N            stop after N frames (0 = never; the default with --window)
  --teamplayer          Sega Team Player on port 1, players 1-4
  --original            no extras (section 3a): the game as on the cartridge
  --arcade-colours      start with the arcade colours on (needs arcade_colours.txt)
  --colours-file FILE   arcade_colours.txt somewhere else
  --nomusic             No music: sound effects and voices only (play_g2_nomusic.bat)
  --level-set gauntlet2 Arcade/Record play the Gauntlet II mazes (gauntlet2_levels.bin)
  --g2-levels FILE      gauntlet2_levels.bin somewhere else
  --nosound             no sound (60 Hz timer pacing)
  --wav FILE            record the sound to a .wav file
  --host PORT           host an online game on PORT
  --players N           with --host: total players, 2-4
  --join HOST:PORT      join an online game
  --delay N             with --host: input delay in frames (default 4)
  --shot F,F,...        save those frames as pictures (.ppm)
  --out DIR             folder for the pictures
  --press [P:]BUTTON@F-F   hold a button from frame F to frame F (player P, default 1);
                        buttons: up down left right a b c start
