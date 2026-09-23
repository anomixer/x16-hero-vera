// gen_music.mjs — Convert the 5 x16-hero ZSM tracks into VERA PSG register streams.
//
// Modeled on C:\dev\veratest\src\slideshow\slideshow_hdv.mjs compileZsmStream()
// (proven on Apple II VERA).  The X16 ZSM format is decoded here at build time:
//   - skip the 16-byte header
//   - 0x40 EXTCMD (PCM/FM extension)  -> dropped (Apple II port has no PCM/FM)
//   - 0x41..0x7F FM write batch        -> skipped
//   - 0x80                             -> end of stream
//   - 0x81..0xFF                       -> delay 1..127 ticks
//   - < 0x40                            -> PSG register write (voice*4+reg, value)
// Output is a compact stream: { 0x80|voice*4+reg, value } pairs interleaved with
// delay bytes (0..126 = wait that many frames).  All tracks are packed into one
// blob (build/music.blob) with a track index emitted as src/music_table.h.
import fs from "fs"
import path from "path"
import { fileURLToPath } from "url"

const __dirname = path.dirname(fileURLToPath(import.meta.url))
const projectRoot = path.resolve(__dirname, "..")
const defaultDataDir = path.join(projectRoot, "assets")
const fallbackDataDir = "C:/dev/x16-hero"
const dataDir = fs.existsSync(defaultDataDir) ? defaultDataDir : fallbackDataDir
const buildDir = path.join(projectRoot, "build")

function findZsmFile(dir, name) {
  const direct = path.join(dir, `${name}.ZSM`)
  if (fs.existsSync(direct)) return direct
  try {
    const entries = fs.readdirSync(dir)
    const found = entries.find(e => e.toLowerCase() === `${name}.zsm`.toLowerCase())
    if (found) return path.join(dir, found)
  } catch (e) {}
  return direct
}

const TRACKS = [
  { name: "TITLE",        file: "TITLE.ZSM" },
  { name: "HIGHSCORE",    file: "HIGHSCORE.ZSM" },
  { name: "GAMEOVER",     file: "GAMEOVER.ZSM" },
  { name: "KILLED",       file: "KILLED.ZSM" },
  { name: "LEVELCOMPLETE", file: "LEVELCOMPLETE.ZSM" },
]

const PSG_AUDIO_RATE = 25000000 / 512; // 48828.125 Hz VERA internal clock

function midiToFreqN(midiNote) {
  const f = 440 * Math.pow(2, (midiNote - 69) / 12);
  return Math.max(0, Math.min(0xFFFF, Math.round(f * 131072 / PSG_AUDIO_RATE)));
}

// OPM Key Code note mapping:
// 14: C, 0: C#, 1: D, 2: D#, 4: E, 5: F, 6: F#, 8: G, 9: G#, 10: A, 12: A#, 13: B
const noteMap = { 14: 0, 0: 1, 1: 2, 2: 3, 4: 4, 5: 5, 6: 6, 8: 7, 9: 8, 10: 9, 12: 10, 13: 11 };

function compileZsm(name) {
  const raw = fs.readFileSync(findZsmFile(dataDir, name));
  const loopOffset = raw[3] | (raw[4] << 8) | (raw[5] << 16);
  let ptr = 16, tick = 0;
  let loopTick = -1;
  const ymChNotes = [0, 0, 0, 0, 0, 0, 0, 0];
  const ymChKFs   = [0, 0, 0, 0, 0, 0, 0, 0];
  const ymChTL    = Array.from({ length: 8 }, () => [0, 0, 0, 0]);
  const ymChPan   = [0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0];

  const frameEvents = [];
  function addWrite(t, reg, val) {
    while (frameEvents.length <= t) frameEvents.push([]);
    frameEvents[t].push({ reg, val });
  }

  while (ptr < raw.length) {
    if (loopOffset > 0 && ptr >= loopOffset && loopTick < 0) {
      loopTick = tick;
    }
    const command = raw[ptr++];

    if (command < 0x40) {
      // Native PSG write (voice 0 or 1)
      let val = raw[ptr++];
      if (name === "TITLE") {
        // Keep the native PSG part close to the CX16 mix.  Only the title
        // voices receive the same small loudness compensation as veramusic.
        if (command === 2) {
          const pan = val & 0xC0;
          const vol = val & 0x3F;
          val = pan | (vol === 0 ? 0 : Math.min(63, Math.round(vol * 63 / 53)));
        } else if (command === 6) {
          const pan = val & 0xC0;
          const vol = val & 0x3F;
          val = pan | (vol === 0 ? 0 : Math.min(60, Math.round(vol * 60 / 53)));
        }
      }
      addWrite(tick, command, val);
      continue;
    }

    if (command === 0x40) {
      const ext = raw[ptr++];
      ptr += ext & 0x3F;
      continue;
    }

    if (command < 0x80) {
      // YM2151 FM write batch: translate to PSG voices 2..9!
      const count = command & 0x3F;
      for (let i = 0; i < count; i++) {
        const reg = raw[ptr++], val = raw[ptr++];
        if (reg >= 0x20 && reg <= 0x27) {
          const ch = reg - 0x20;
          const p = val & 0xC0;
          ymChPan[ch] = (p === 0xC0 ? 0xC0 : (p === 0x80 ? 0x80 : 0x40));
        } else if (reg >= 0x28 && reg <= 0x2F) {
          ymChNotes[reg - 0x28] = val;
        } else if (reg >= 0x30 && reg <= 0x37) {
          ymChKFs[reg - 0x30] = val;
        } else if (reg >= 0x60 && reg <= 0x7F) {
          const op = (reg - 0x60) >> 3;
          const ch = (reg - 0x60) & 7;
          ymChTL[ch][op] = val & 0x7F;
        } else if (reg === 0x08) {
          const ch = val & 7;
          const ops = (val >> 3) & 0x0F;
          const psgVoice = 2 + ch; // YM2151 ch 0..7 -> VERA PSG voices 2..9
          const baseReg = psgVoice * 4;

          if (ops > 0) {
            // Key On
            const kc = ymChNotes[ch];
            const oct = (kc >> 4) & 7;
            const nCode = kc & 0x0F;
            const semi = noteMap[nCode] ?? 0;
            let midi = (oct + 1) * 12 + semi;

            if (name === "HIGHSCORE" && ch === 6 && oct >= 6) {
              /* ZSMKit plays this high KC=113 voice as a YM2151 high-feedback
               * metallic percussion hit, not as a pitched melody note.  VERA
               * has no FM operator/feedback path, so use a short, restrained
               * noise envelope instead of an exposed 2.3 kHz triangle tone. */
              addWrite(tick, baseReg + 0, 0x20);
              addWrite(tick, baseReg + 1, 0x08);
              addWrite(tick, baseReg + 3, 0xC0); // Noise waveform
              addWrite(tick + 0, baseReg + 2, ymChPan[ch] | 24);
              addWrite(tick + 1, baseReg + 2, ymChPan[ch] | 10);
              addWrite(tick + 2, baseReg + 2, 0x00);
            } else {
              const fN = midiToFreqN(midi);
              let wave = 0x3F; // Pulse ~50%
              let vol = 36;
              if (name === "KILLED") {
                wave = 0x80; // Triangle wave for soft backing chord
                vol = 24;
              } else if (name === "GAMEOVER") {
                wave = (ch === 0 || ch === 1) ? 0x80 : 0x3F;
                vol = 40;
              } else if (ch === 7) {
                // Highscore/general lead voice in the reference converter.
                wave = 0x3F;
                vol = 46;
              } else if (ch === 6) {
                wave = 0x80; // Bass
                vol = 52;
              } else if (ch === 4 || ch === 5) {
                wave = 0x3F;
                vol = 44;
              }

              if (name === "TITLE") {
                // Keep original YM ch5/ch6/ch7 ownership as PSG 7/8/9.
                // No synthetic retriggers or extra layers: their independent
                // key-on/key-off clocks are the four-note phrase timing.
                if (ch === 5 || ch === 6) {
                  wave = 0x3F;
                  vol = 40;
                } else if (ch === 7) {
                  wave = 0x80;
                  vol = 38;
                }
              }

              addWrite(tick, baseReg + 0, fN & 0xFF);
              addWrite(tick, baseReg + 1, (fN >> 8) & 0x3F);
              addWrite(tick, baseReg + 3, wave);
              addWrite(tick, baseReg + 2, ymChPan[ch] | vol);
              if (name === "TITLE" && ch === 7) {
                const stringTail = [38, 36, 33, 29, 24, 18, 11, 5, 0];
                for (let d = 1; d < stringTail.length; d++) {
                  addWrite(tick + d * 2, baseReg + 2, ymChPan[ch] | stringTail[d]);
                }
              } else if (name === "TITLE" && (ch === 5 || ch === 6)) {
                const pianoTail = [40, 38, 34, 28, 20, 11, 0];
                for (let d = 1; d < pianoTail.length; d++) {
                  addWrite(tick + d * 2, baseReg + 2, ymChPan[ch] | pianoTail[d]);
                }
              }
            }
          } else {
            // Key Off: mute voice
            addWrite(tick, baseReg + 2, 0x00);
          }
        }
      }
      continue;
    }

    if (command === 0x80) break;

    const delay = command & 0x7F;
    tick += delay;
  }

  // Pack frameEvents into compact stream: { 0x80|reg, val } pairs and delay bytes (0..126)
  const stream = [];
  let delayCount = 0;
  let loopStreamOffset = 0;

  for (let f = 0; f < frameEvents.length; f++) {
    if (loopTick >= 0 && f === loopTick) {
      loopStreamOffset = stream.length;
    }
    const writes = frameEvents[f];
    if (!writes || writes.length === 0) {
      delayCount++;
      continue;
    }

    while (delayCount > 0) {
      const chunk = Math.min(delayCount, 127);
      stream.push(chunk - 1);
      delayCount -= chunk;
    }

    // Deduplicate writes to the same register in this frame (preserves last write,
    // eliminating redundant Key Off mutes when Key On occurs in the same tick).
    const regMap = new Map();
    for (const w of writes) {
      regMap.set(w.reg, w.val);
    }

    // Hardware VERA safety: order register writes within each frame so that:
    // 1. Channel mutes (volume = 0) happen first across all voices.
    // 2. Frequency (lo, hi) and waveform are configured before turning volume on.
    // 3. Channel volume > 0 is ALWAYS written last.
    // This prevents intermediate frequency/waveform glitches (e.g. 4.5kHz clicks/pops).
    const entries = Array.from(regMap.entries());
    entries.sort(([regA, valA], [regB, valB]) => {
      const vA = Math.floor(regA / 4), vB = Math.floor(regB / 4);
      const rA = regA % 4, rB = regB % 4;
      function prio(r, val) {
        if (r === 2) return (val & 0x3F) === 0 ? 0 : 4;
        if (r === 0) return 1;
        if (r === 1) return 2;
        if (r === 3) return 3;
        return 5;
      }
      const pA = prio(rA, valA), pB = prio(rB, valB);
      if (pA === 0 && pB !== 0) return -1;
      if (pB === 0 && pA !== 0) return 1;
      if (vA !== vB) return vA - vB;
      return pA - pB;
    });

    for (const [reg, val] of entries) {
      stream.push(0x80 | (reg & 0x3F), val & 0xFF);
    }
    delayCount = 1; // 1 frame delay to reach next tick
  }

  while (delayCount > 0) {
    const chunk = Math.min(delayCount, 127);
    stream.push(chunk - 1);
    delayCount -= chunk;
  }

  stream.push(0xFF);
  return { buf: Buffer.from(stream), loopStreamOffset };
}

if (!fs.existsSync(buildDir)) fs.mkdirSync(buildDir, { recursive: true })

const blob = []
const table = []
let offset = 0

for (const { name } of TRACKS) {
  const data = compileZsm(name)
  blob.push(data.buf)
  table.push({ name, offset, length: data.buf.length, loop_offset: data.loopStreamOffset })
  console.log(`  ${name}.ZSM -> ${data.buf.length} bytes PSG stream (offset ${offset}, loop_offset ${data.loopStreamOffset})`)
  offset += data.buf.length
}

fs.writeFileSync(path.join(buildDir, "music.blob"), Buffer.concat(blob))
console.log(`  Total music blob: ${offset} bytes`)

// C header: track offsets/lengths/loop_offsets so audio.c can address each stream in VRAM.
let cCode = `// AUTO-GENERATED by tools/gen_music.mjs — do not edit.
#pragma once
#include <stdint.h>

typedef struct {
    const char *name;
    uint16_t offset;
    uint16_t length;
    uint16_t loop_offset;
} MusicTrack;

#define MUSIC_TRACK_COUNT ${table.length}
static const MusicTrack musicTracks[MUSIC_TRACK_COUNT] = {
${table.map((t) => `    { "${t.name}", ${t.offset}, ${t.length}, ${t.loop_offset} },`).join("\n")}
};
`
fs.writeFileSync(path.join(projectRoot, "src", "music_table.h"), cCode)
console.log(`  Wrote src/music_table.h`)
