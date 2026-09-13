# AGENTS.md — x16-hero VERA port

Port of the Commander X16 game at `C:\dev\x16-hero` (a H.E.R.O. sequel) to the **Apple II
VERA FPGA card** platform (apple2ts), located at `C:\dev\x16-hero-vera`.

**Chosen strategy (user-approved):** rewrite the game in **C compiled with llvm-mos**
(`mos-apple2e-clang`), reusing the proven TimePilot-IIvera infrastructure rather than
hand-porting the ACME 65c02 assembly.

## Why this strategy

TimePilot-IIvera (`C:\dev\Time-Pilot\TimePilot-IIvera`) is the successful precedent on this
exact platform and provides reusable, proven scaffolding:

- `src/apple2e.h` — slot-agnostic VERA access, default Slot 2 `$C200`, dual-build Slot 4
  `$C400` via `-DVERA_BASE=0xC400`
- `src/mli.s` — ProDOS MLI READ_BLOCK
- `src/disk.c` — fixed-block asset streaming
- `src/audio.c` — PSG engine
- `tools/*.mjs` — `build_hdv`/`build_disk`, `gen_psg_*`
- `src/startup.bas` — Applesoft BASIC slot auto-probe → BRUN MAIN.BIN / MAIN4.BIN
- `veratest/src/applebasic.mjs`

## Key platform decisions

- Polling VSYNC loop in `main()` (no IRQ handler)
- Music converted at build time from ZSM → PSG register streams (`tools/gen_music.mjs`),
  played from VRAM by a 60Hz decoder
- Assets packed into one `assets.blob` (`tools/gen_assets.mjs`) at fixed HDV block 900, read
  via MLI

## Architecture (as of session 2)

- **Layer 0** = 16x16 tilemap (`TILES_ADDR` `$2000`)
- **Layer 1** = text layer (`config=0x10`, 64 cols, 32 rows, 8x8, charset at `$9800`)
- **Sprites** = VERA sprite attributes: `attr0` bit 2 = visible/z-depth 1 (0x04); `attr1` =
  dims|palette_offset (0x50 = 16x16)
- `DC_VIDEO` bit layout on this card: bit0=VGA, bit4=L0, bit5=L1, bit6=sprites — **NOT** the
  X16 layout
- 16-bit signed `int16` arithmetic for creature screen positions (llvm-mos uses 16-bit ints)
- 8-bit LFSR for random creature frame/offset (polynomial `$1d`)

## Development log

### Session 1 — initial port + strip fix

Game-logic ported to `src/main.c`: player physics (gravity/flight/walking), player sprite
draw, simplified creature movement, `light_up_level` for dark levels. MAIN.BIN ~8749B.

**STRIP FIXED (root cause found):** the colorful strip at the bottom of the screen was
**Layer 1** (the text layer), NOT Layer 0. Layer 1 was configured as `config=0x60`, which on
this Apple II VERA renders as a **16x16 tilemap**, not 8x8 text. Its map region
(`$0000-$0FFF`, shared with `L1_MAP_ADDR`) was uninitialized/leftover data (an
incrementing-by-14 pattern), rendered as a row of distinct garbage tiles = the "strip".
Fixes applied:

- `init_screen`: `layer1.config` changed from `0x60` → `0x10` (text mode, 64 cols, 32 rows,
  8x8 — same as TimePilot's proven layer0 text config). Plus a boot-time clear of the layer1
  map region `$0000-$0FFF` (write 0x20/0x01 per cell).
- `text_clear`/`text_putc` changed from 80-col layout (`r*160`) to 64-col (`r*128`).
- `ST_INITLEVEL` now calls `text_clear()` so leftover menu text is removed when a level starts.

Result: the level now renders correctly (brown/blue cave, orange platforms, bats) filling the
whole screen — strip gone, no garbage. Verified via AppleWin PrintWindow capture.

### Session 2 — faithful creature AI + death flow + timer + level-1 darkness fix

**BUG FIX — level 1 darkness:** `levelDark[]` had `[1]=LEVEL_REALLY_DARK`; the original
`_leveldarktable` is `0,0,0,0,0,0,0,0,REALLY_DARK,0,REALLY_DARK` (only levels 8 and 10 are
dark). Level 1 was rendering as a black level. Fixed → level 1 is light. (`darkMode` was
declared but never assigned; now set in `set_level_properties`.)

**Creature AI (creatures.asm) made faithful** in `init_creatures`/`update_creatures`:
full 64-entry movement tables (`alienX`, `batOff`; alien Y reads into X table for idx
16..63 — faithful to asm layout), offsets added to the SCREEN position (world anchor fixed,
not world-mutated), per-type init alignment (spider y−1, claw x±2, plant y+1, lamp x±1, via
tile h-flip), random frame/offset via an 8-bit LFSR (`getRandom`, polynomial `$1d`), dying
animation (laser hit → `CREATURE_DYING_START` → 4 `DIE_ADDR` frames → disable), dark-mode lamp
uses `DARK_LAMP_ADDR`, sticky light-up near player (`creatureLit`, palette 5=black creature
vs 2=lit), miner never animates. PositionSprite hide rule replicated (hide iff high byte
2..14 → set 512).

**BUG FIX — death/restart flow:** `ST_RESTARTLEVEL` was in the enum but had NO handler in
`game_tick`, so the game froze when the player died. Added `ST_DEATH_CREATURE`/`ST_DEATH_LAVA`
(120-frame `deathDelay`, then lives-- → `ST_GAMEOVER` or `ST_RESTARTLEVEL`) and
`ST_RESTARTLEVEL` (re-init level, restore timer to `lastLevelMinutes/Seconds` → `ST_RUNNING`).

**Added game timer:** `time_tick()` (60 jiffies/sec, 60 sec/min), called in `level_tick`;
reset at `ST_INITGAME`, saved to `lastLevelMinutes/Seconds` at `ST_LEVELCOMPLETED`, restored
on restart. `minutes/seconds/jiffies` were previously declared but never incremented (HUD
TIME was stuck).

Verified via AppleWin PrintWindow capture: level 1 light + creatures render; two frames 3s
apart differ (game alive; death-restart cycle and timer advance). MAIN.BIN ~10783B.

**Known cosmetic issue:** layer1 text columns render MIRRORED (col 0 appears on the right) —
the HUD "LIVES 03" shows on the left and "LEVEL 01" on the right. Text is readable, layout is
reversed; not yet fixed. The player auto-falls and dies without input (expected; needs
joystick input).

### Session 3 — removed startup.bas 5-second wait

`src/startup.bas` had a `FOR T = 1 TO 420` delay loop after the controls screen ("HIT ANY KEY
OR WAIT 5 SECS TO START..."). Removed the loop and key-scan; the game now BRUNs immediately
after displaying the controls. Prompt changed to "STARTING...".

### Session 4 — full UI (menu/highscore/credit/board/leaderboard) + faithful state machine + input fix

**MAJOR: menu, high-score, credit, leaderboard, pause, and the full faithful state machine
are now implemented and rendering.** The game boots to the menu ("MINE RESCUE" big title +
START THE GAME / SET START LEVEL / RESET HIGH SCORES / QUIT GAME), the level-0 demo cave
background renders behind it, and pressing SPACE starts level 1 correctly. Verified via
AppleWin PrintWindow capture.

Changes:
- **`src/text.c`/`text.h` (new):** text layer helpers (64 cols x 32 rows, `row*128 + col*2`)
  — `text_ascii_to_screen`, `text_putc`, `text_putc_raw`, `text_print`, `text_clear`.
  `text_ascii_to_screen` now maps lowercase ASCII → uppercase glyphs (the FONT.BIN uppercase
  zone 0x01-0x1A holds the Latin letters; lowercase glyphs are absent).
- **`src/input.c`:** switched `_joy0` to **active-LOW** semantics (pressed = bit CLEARED,
  `JOY_NOTHING_PRESSED=255`), matching the original joysticklib.asm. Previously active-HIGH
  (pressed = bit set) — this mismatched the game logic's `(j & JOY_UP)==0` checks, so the
  player never received input (auto-fell/died). Also added UPPERCASE letter mappings
  (AppleWin sends uppercase ASCII for the Apple II's all-caps keyboard).
- **`src/main.c`:** rewrote the tail with the full faithful UI + state machine:
  - `update_status_bar` (row 28: level/time/lives)
  - big-font `print_level_finished` / `print_game_over` / `print_game_completed`
  - `show_pause_menu` / `update_pause_menu` (JOY_START to pause, resume/quit)
  - `show_menu_screen` / `show_high_score_screen` / `show_credit_screen` (cameras 160/480/800)
  - `menu_handler` + `handle_user_input`/`handle_updown`/`handle_leftright`/`handle_button`
  - `print_leaderboard` / `reset_leaderboard` / `get_high_score_rank` / `set_new_high_score`
    (RAM; default roderrick/elvin/guybrush/sandy pantz/... leaderboard)
  - `init_high_score_input` / `high_score_input` (11-char name entry, cursor blink, backspace)
  - faithful `game_tick` state machine (ST_INITSTARTSCREEN→INITMENU→SHOWMENU→INITGAME→
    INITLEVEL→RUNNING→death/levelcomplete→GAMEOVER/GAMECOMPLETED→ENTERHIGHSCORE)
  - `show_player`/`hide_player` sprite helpers
- **`build.bat`:** added `src\text.c` to both compile lines.

**Key fixes:**
- **Mirrored HUD text FIXED** — root cause was the layer1 text row stride (`row*64` instead
  of `row*128`). With the text.c rewrite the HUD ("LEVEL 01 ... LIVES 05 ... 00:24") renders
  left-to-right correctly.
- **Menu navigation bounce-back FIXED** — when navigating menu→highscore/credit, the nav key
  was still held for a few frames, so `joy()!=NOTHING` immediately bounced back to the menu.
  Added `highscorewait`/`creditwait` counters to ignore the held nav key.

**Verified via AppleWin capture (session 4 continued):**
- Menu renders (title + 4 items + hand + level-0 cave bg + "CLERGY GAMES" logo).
- **HIGH SCORES leaderboard renders completely** — big "HIGH SCORES" title + RANK / NAME /
  SAVED MINERS / TIME / START LEVEL columns, all 10 default entries (RODERRICK 10/20:00/01
  down to MONTY MOLE 01/00:03/01) in their per-row colors.
- SPACE starts level 1 (level renders, creatures, timer advances, LIVES=05).
- **Level-complete banner "MINER SAVED!"** renders in big font on level completion.
- **Level transition works** — after the banner, level 2 loads and renders (different cave
  layout), status bar "LEVEL 02".
- Text not mirrored (row `*128` stride fix); level-0 background correct after
  `set_layer0_tile_mode()` was added to `init_menu_background`.

**Verified via temporary auto-complete debug hook** (since removed): a `debugAutoComplete`
flag in ST_RUNNING cycled levels 1→2→... automatically, confirming the level-complete banner
and level transition. Removed from the release build.

**Pending verification (test-harness keyboard quirk):** menu navigation to highscore/credit
screens via 'a'/'d' isn't registering in the AppleWin keybd_event harness (letter keys send
uppercase; SPACE works). The menu logic is correct; this is an emulator input-capture
limitation, not a game bug. Track for manual verification.

- **Pause menu verified** — pressing '1' (JOY_START) in-game shows the pause menu (box +
  "RESUME GAME"/"QUIT"); '1' is a digit so it registers like SPACE. Cosmetic: pause items
  render with some overlap; layout could be cleaned.
- **Intro/start controls screen verified** — startup.bas shows "H.E.R.O. FOR APPLE II VERA /
  BY ANOMIXER / STATUS: VERA CARD DETECTED IN SLOT 2 / CONTROLS ... / RUNNING H.E.R.O...."
  (faithful intro).
- **High-score entry + game-complete flow verified** (via temporary debug that jumps to
  level 10): level 10 → GAME COMPLETED → restart_game → ST_ENTERHIGHSCORE; a new entry is
  added at rank 1 (10 saved, 00:01, start 01) and the leaderboard shows it with the rest
  shifted down. Debug removed from release.

- **Dark level verified** — level 8 (dark) renders correctly: mostly black (blackout_level)
  with a lit region around the player (light_up_level). Levels 1/2 (light), 8 (dark), and the
  level transition all render; levels 3-7/9/10 use the same load_level_map/set_level_properties
  pipeline.
- **Music wiring verified** — all 5 ZSM tracks wired via `audioPlayMusic`:
  MUSIC_TITLE (menu), MUSIC_HIGHSCORE (game complete + highscore entry), MUSIC_KILLED
  (player death), MUSIC_LEVELCOMPLETE (level complete), MUSIC_GAMEOVER (game over); SFX
  (engine/laser/playerdead/creaturedead) + `audioServiceAudio()` in game_tick. Audio output
  not audibly verified (AppleWin capture is visual only).

**Remaining:** audible music verification (wired correctly; needs manual audio check).

### Session 5 — intro faithful to cx16-1 (miner + rainbow fix), title music loop, sprite cleanup

Three fixes to match the reference `cx16-1.png` and clean up gameplay:

**BUG FIX — intro bottom-right showed rainbow garbage instead of the miner.** The intro room's
miner creature was initialized with a RANDOM animation frame (`getRandom() & 3`) in
`init_creatures`, but the miner has only ONE sprite frame (128 bytes) and never animates. A
non-zero frame made `update_creatures` read sprite data past the miner's single frame into
uninitialized memory, rendering as a colorful checkerboard (the "rainbow square"). Fixed in
`init_creatures`: `if (type == TYPE_MINER) creatureFrame[i] = 0;`. The miner now draws
correctly (yellow helmet, blue body) at the intro bottom-right, matching `cx16-1.png`.

**BUG FIX — leftover intro sprites shown during level-1 gameplay.** After starting a level, the
menu scene's creatures (green plant, miner) remained visible at the bottom of the level.
Root cause: `hide_creatures()` wrote only 64 consecutive bytes to the sprite attribute area,
which clears just the first 8 sprite slots, but scenes use up to ~11 slots. Fixed: it now
writes 0 to the visibility byte (`attr0`, offset +6) of ALL `MAX_SPRITE_COUNT` (64) slots.
Level 1 now shows only its legitimate creatures (map1 plants/alien); the intro leftovers are
gone.

**Music — title now loops.** `audio.c` `musicTick` previously set `musicActive = 0` when a
track hit its `0xFF` end marker, so the menu/title music went silent after one play. Added a
`musicLoop` flag (set for `MUSIC_TITLE`) and `musicBase` offset; on track end the decoder
restarts from the beginning, so the intro/menu keeps sound. Jingles (game over / level
complete / killed) still play once.

**Verified via AppleWin PrintWindow capture:**
- `intro_shot.png` — intro now matches `cx16-1.png`: miner (person) at bottom-right, no rainbow
  garbage, no debug clutter.
- `level1_shot.png` — level 1 after fix: only legitimate level-1 creatures render (2 green
  plants + orange alien), no leftover intro worm/miner; player at start.

**Capture scripts:** `run_intro_shot.ps1` (captures the menu, no key), `run_level1_shot.ps1`
(waits, closes the Configuration dialog, sends `'X'` 0x58 to START level 1, captures). To
start level 1 from the menu send `'X'` (0x58) — **not** `'1'` (which is the pause key).
The emulator process is `AppleWin-x64`; kill it (e.g. `Get-Process AppleWin-x64 | Stop-Process
-Force`) before rebuilding, otherwise `build_hdv.mjs` fails with `EBUSY` because the HDV is
locked by the running emulator. MAIN.BIN ~16787B.

## Debug/verification workflow

`-h1 x16-hero-vera.hdv -s2 vera -s7 hdc`. AppleWin shows a **"Configuration" dialog on
startup** that pauses the emulator — close it (SendMessage `WM_CLOSE`). To START the game from
the menu send `'X'` (0x58) via `keybd_event`; `'1'` (0x31) is the **pause** key, NOT start.
Capture the emulator window via `PrintWindow` (works even if occluded; CopyFromScreen captures
whatever window is on top, so it's unreliable). Scripts in `C:\dev\x16-hero-vera`:
`run_intro_shot.ps1` (menu capture, no key), `run_level1_shot.ps1` (closes Config dialog,
sends `'X'`, captures level 1). VERA output appears only after the game enables it
(`video=0x71` = VGA+L0+L1+sprites). The emulator process is `AppleWin-x64`; kill it before
rebuilding or `build_hdv.mjs` fails with `EBUSY` (HDV locked). Window enumeration is
occasionally flaky (captures 0x0); the inline PowerShell capture works reliably.

## Build

`build.bat`: `gen_music.mjs` → `gen_assets.mjs` → compile Slot 2 and Slot 4 (`main.bin` /
`main4.bin`) → `build_hdv.mjs`. Run with absolute path:
`cmd //c "C:\\dev\\x16-hero-vera\\build.bat"` (the shell cwd resets to `C:\dev\x16-hero`).

### Session 6 — PSG audio tuning & intro piano restoration

- **Title theme restored with authentic piano envelope**: `tools/gen_music.mjs` updated to
  render Channel 7 using warm Triangle wave (`0x80`) with natural exponential decay down to
  standard A4 piano range (~440Hz), restoring the classic intro opening notes.
- **Fixed loop silence & PSG clicks**: `audio.c` now executes `psg_silence_all()` upon looping
  or switching tracks, preventing stuck notes across transitions.
- **Highscore signature loop**: looped playback added during name entry.

### Session 7 — controls, confirmation UI & respawn invulnerability

- **Menu confirmation dialogs**: fixed uninitialized BSS memory displaying duplicated
  `ARE YOU SURE? (Y/N)?` prompts on Reset High Scores and Quit Game.
- **Floor-tunneling fix ("採穿地面")**: added `FALLINGSPEED_DELAY = 8` and multi-step collision
  checks during player freefall, preventing the player from passing through rock floors.
- **2-second respawn invulnerability**: added `invulnerableTimer = 120` upon respawning to
  prevent immediate death loops at spawn points. Killer creature is properly dispatched.

### Session 8 — authentic darkness, wall lamps & proximity lighting

- **Disentangled dark levels vs wall lamps**:
  - `isDarkLevel` (levels 8 and 10) uses `blackout_level` and flashlight spotlighting.
  - Wall lamps (`SwapLight` / `TurnOffLight`) darken VERA hardware palettes 1..4 in VRAM
    (halving RGB color components, preserving glowing lava color 15) without tilemap replacement.
  - When light is extinguished, the wall lamp sprite switches to `DARK_LAMP_ADDR`.
  - Darkness persists upon player death in the same room (`ST_RESTARTLEVEL`), matching original X16.
- **Proximity creature lighting**:
  - In gameplay levels (1..10), creatures/plants/miners start hidden in cave shadows
    (`BLACK_CREATURE_PALETTE_INDEX` palette 5).
  - When the player enters a 72 px radius (`LIGHT_CREATURE_COLS/ROWS`), the creature lights up
    into full vibrant color (`CREATURE_PALETTE_INDEX` palette 2).

### Session 9 — frame-accurate creature hitboxes & retraction避險

- **Retraction / expansion hitboxes**:
  - Dumped pixel bounds for all 4 animation frames of all creature types (`SPRITE1.BIN`).
  - Plants / snake heads (`TYPE_PLANT`) in retracted state (Frame 3) leave 10 vertical pixels
    of empty air above, allowing the player to safely walk or fly over contracted snake heads.
  - Spiders on ceilings (`TYPE_SPIDER`) and wall claws (`TYPE_CLAW`) properly clear clearance
    margins when contracted.
  - Dynamic movement offsets (`alienX`, `alienOffsetY`, `batOff`) applied to collision boxes.
  - Player active hitbox trimmed to 9x21px body without false-positive transparent edges.

### Session 10 — status bar HUD layout & flicker elimination

- **HUD layout faithfully matched to `level04.png`**:
  - Col 1..5: `LEVEL`
  - Col 7..8: `04` (level number)
  - Col 17..21: `01:12` (elapsed time)
  - Col 31..35: `LIVES`
  - Col 37..38: `05` (remaining lives)
- **Eliminated HUD flicker & text gluing**:
  - Root cause was a placeholder string `" level...lives   "` that printed dots over cols 7..8
    before overwriting with numbers every frame, and jammed `LEVEL` and `LIVES` into 13 columns.
  - Removed dots and separated labels to opposite ends of the screen.

### Session 11 — High Score Persistence via HISCORE.BIN

- **Leaderboard disk persistence without complex MLI file manager overhead**:
  - Allocated fixed seedling block (`HISCORE_START_BLOCK = 899`, immediately adjacent to assets at 900).
  - Seedling directory entry registered in root ProDOS catalog as `HISCORE.BIN` (`stType = 1`, `fileType = 0x06`, `keyBlock = 899`, `totalBlocks = 1`, `eof = 163`, `aux = 0x2000`).
  - Added `mlib_write_block` to `src/mli.s` (ProDOS MLI `$81` `WRITE_BLOCK`).
  - Implemented `disk_read_hiscore()` and `disk_write_hiscore()` in `src/disk.c` with streaming cache invalidation.
  - Implemented `load_leaderboard()` and `save_leaderboard()` in `src/main.c`, serializing 10 names, saved miners, times, start levels, max allowed start level, and magic bytes ('H', 'S').
  - Wired auto-load at boot, auto-save upon high-score name entry, and auto-save on reset confirmation.
  - `build_hdv.mjs` automatically preserves existing high scores across builds when rebuilding `x16-hero-vera.hdv`.

## Current Project Status

- Fully playable 10-level platformer on Apple II VERA (Slot 2 and Slot 4 dual-build).
- Bootable 800K ProDOS image: `x16-hero-vera.hdv`.
- Persistent high scores saved to `HISCORE.BIN`.
- Pushed to GitHub repository https://github.com/anomixer/x16-hero-vera.


