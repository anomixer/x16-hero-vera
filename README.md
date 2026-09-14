# H.E.R.O. (Mine Rescue) — Apple II VERA Port

A faithful port of the Commander X16 game [x16-hero](https://github.com/joolin1/x16-hero) (a sequel/tribute to Activision's classic *H.E.R.O.*) to the **Apple II with VERA FPGA card** platform.

Developed in C compiled with [llvm-mos](https://github.com/llvm-mos/llvm-mos) (`mos-apple2e-clang`), using the proven [TimePilot-IIvera](https://github.com/anomixer/Time-Pilot) hardware scaffolding.

---

## Features

- **10 Full Cave Levels + Demo Room**: Traverse subterranean mine shafts, rescue trapped miners, avoid deadly lava, and navigate tight caverns.
- **Flight & Jetpack Physics**: Faithful helicopter backpack flight, walking, falling, and dynamite placement.
- **Laser & Explosives**: Blast through destructible cave walls and exterminate subterranean critters with your helmet laser or placed bombs.
- **Dynamic Lighting & Darkness Mechanics**:
  - Dark levels feature flashlight spotlighting around the hero.
  - Smashing wall lamps dynamically dims the entire cave environment via VERA hardware palette darkening.
  - Proximity illumination: unreached creatures and miners linger in cave shadows until approached.
- **Frame-Accurate Creature Hitboxes**: Plants, spiders, and claws expand and contract rhythmically—sneak over retracted snake heads and under raised spiders safely.
- **Full PSG Soundtrack & Sound Effects**: 5 converted music tracks (Title, Level Complete, Killed, Game Over, High Scores) with loop support and multi-voice sound effects.
- **100% Zero Runtime Disk Access**: All maps (11 cave scenes), sprites, tiles, and music streams are preloaded into VERA VRAM at boot. Level transitions and restarts perform instantaneous hardware VRAM-to-VRAM block copies (~16ms) without any disk latency, motor spin-up, or head seeking.
- **Rich User Interface**:
  - Full titles, animated menu hand selector, start level selector (1..10), pause menu.
  - High score leaderboard with in-game signature entry.
  - Persistent high scores automatically saved to and loaded from `HISCORE.BIN` on the ProDOS volume.
  - Authentic HUD displaying Level, Elapsed Time, and Remaining Lives.

---

## Hardware Requirements & Compatibility

- **Apple IIe / IIgs** equipped with a **VERA FPGA Expansion Card** (Slot 2 default `$C200`, or Slot 4 `$C400`).
- **AppleWin** emulator with VERA card support:
  ```batch
  AppleWin.exe -s2 vera -s7 hdc -h1 x16-hero-vera.hdv
  ```

---

## Memory Architecture & VRAM Layout

The game operates on a zero-disk dual-memory architecture:

### 1. Apple II 6502 RAM (64 KB)
- **`$0000 - $004F`**: Apple II ROM, LLVM-MOS virtual registers, Applesoft reserved.
- **`$0040 - $004F`**: **ProDOS MLI Scratchpad** (strictly isolated to prevent OS clobbering).
- **`$0050 - $00BF`**: C Language Zero Page variables (112 bytes).
- **`$0100 - $0FFF`**: Hardware stack, input buffer, Text Page 1, ProDOS driver buffers.
- **`$1000 - $13FF`**: ProDOS QUIT and BASIC.SYSTEM preserved regions.
- **`$1400 - $666B`**: **`MAIN.BIN` Game Engine** (~20.6 KB, `.text`, `.rodata`, `.data`, `.bss`).
- **`$666C - $BDFF`**: Free RAM (~22.4 KB headroom).
- **`$BE00`**: LLVM-MOS C soft stack base (grows downward).
- **`$BF00 - $BFFF`**: ProDOS MLI entry vector and system global page.

### 2. VERA 128 KB VRAM
- **Bank 0 (`$0:0000 - $0:FFFF`) — Active Rendering Space**:
  - `$0:0000 - $0:0FFF` (4.0 KB): **Layer 1 Text Map** (`L1_MAP_ADDR`, HUD and menus).
  - `$0:2000 - $0:4D7F` (11.6 KB): **Layer 0 Background Tiles** (`TILES_ADDR`, 16x16 tiles).
  - `$0:6000 - $0:6CFF` (3.3 KB): **Player Sprites** (`PLAYER_SPRITES_ADDR`).
  - `$0:7400 - $0:94FF` (8.4 KB): **Creature & Item Sprites** (`CREATURE_SPRITES_ADDR`).
  - `$0:9800 - $0:9EE0` (1.7 KB): **Layer 1 Font Charset** (`NEW_CHAR_ADDR`).
  - `$0:A000 - $0:AFFF` (2~4 KB): **Active Level Map** (`L0_MAP_ADDR`, copied via `vram_copy`).
- **Bank 1 (`$1:0000 - $1:FFFF`) — Music, Preloaded Maps & Hardware Registers**:
  - `$1:0000 - $1:8C54` (35.9 KB): **5 PSG Music Streams** (`MUSIC`).
  - `$1:9000 - $1:EFFF` (24.0 KB): **Master Maps Preload Buffer** (`VRAM_MAPS_MASTER_ADDR`):
    - `$1:9000 - $1:9FFF`: `MAP0` Menu Cave (4 KB).
    - `$1:A000 - $1:EFFF`: `MAP1`–`MAP10` Gameplay Caves (10 × 2 KB = 20 KB).
  - `$1:F9C0 - $1:F9FF` (64 B): **VERA Hardware 16-Channel PSG Registers**.
  - `$1:FA00 - $1:FBFF` (512 B): **VERA Hardware Palettes** (16 palettes × 16 colors).
  - `$1:FC00 - $1:FDFF` (512 B): **Sprite Attribute Table** (64 hardware sprites).

---

## Controls

| Action | Keyboard | Joystick |
| :--- | :--- | :--- |
| **Move Left / Right** | `Left` / `Right` Arrow (or `A` / `D`) | Joystick Left / Right |
| **Hover / Fly Up** | `Up` Arrow (or `W`) | Joystick Up |
| **Drop Dynamite / Bomb** | `J` / `Z` | Joystick Down |
| **Fire Helmet Laser** | `Space` / `X` | Button 0 / Fire 1 |
| **Start Game / Pause** | `1` / `Return` (`Enter`) | Button 1 / Start |

---

## Project Structure

```
x16-hero-vera/
├── assets/          # Raw binary tilemaps, sprites, fonts, palettes & ZSM tracks
├── src/
│   ├── main.c           # Main game logic, state machine, entity management & rendering
│   ├── apple2e.h        # Slot-agnostic Apple II VERA registers & hardware definitions
│   ├── audio.c, .h      # PSG sound driver and music playback engine
│   ├── disk.c, .h       # ProDOS MLI asset streaming interface
│   ├── input.c, .h      # Keyboard and joystick polling
│   ├── text.c, .h       # Layer 1 8x8 text renderer (menus, HUD, leaderboards)
│   ├── mli.s            # ProDOS MLI low-level assembly wrapper
│   └── startup.bas      # Applesoft BASIC bootloader (slot probe & binary launcher)
├── tools/
│   ├── build_hdv.mjs    # Generates bootable ProDOS 800K HDV disk image
│   ├── gen_assets.mjs   # Packs tilemaps, sprites, fonts, and palettes into assets.blob
│   ├── gen_music.mjs    # Converts ZSM music files into PSG register event streams
│   └── applebasic.mjs   # Standalone Applesoft BASIC tokenizer & compiler
├── music/               # Converted standalone .psg files for PC preview (psgplay.exe)
├── build.bat            # One-click build script (music -> assets -> compile -> HDV)
├── AGENTS.md            # Detailed technical porting and architecture logs
└── README.md            # This documentation
```

---

## Building from Source

### Prerequisites
1. **llvm-mos SDK** (`mos-apple2e-clang`) installed and added to your `PATH`.
2. **Node.js** (v18+ recommended) for running asset and disk generation tools.

### Build Command
Simply execute the build batch file:
```batch
build.bat
```

This will automatically:
1. Parse and compile all `.zsm` music tracks into compact PSG register streams.
2. Pack all tilemaps, fonts, palettes, and sprite sheets into `assets.blob`.
3. Compile both Slot 2 (`MAIN.BIN`) and Slot 4 (`MAIN4.BIN`) binaries.
4. Construct the bootable ProDOS disk image `x16-hero-vera.hdv`.

---

## Credits

- **Original Game**: [x16-hero](https://github.com/joolin1/x16-hero) by Clergy Games for the Commander X16.
- **Apple II VERA Port**: Ported by **anomixer**.
- Built on the infrastructure and PSG sound engine of [TimePilot-IIvera](https://github.com/anomixer/Time-Pilot).
