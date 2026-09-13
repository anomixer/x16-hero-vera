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
const dataDir = "C:/dev/x16-hero"
const buildDir = path.join(projectRoot, "build")

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
  const raw = fs.readFileSync(path.join(dataDir, `${name}.ZSM`));
  let ptr = 16, tick = 0;
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
    const command = raw[ptr++];

    if (command < 0x40) {
      // Native PSG write (voice 0 or 1)
      let val = raw[ptr++];
      if (name === "TITLE") {
        // Boost second-half native melody on voice 0 and counter-melody on voice 1
        // while preserving note-offs (vol = 0) and smooth envelope decays.
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

            if (ch === 7) {
              // Ch 7 is the signature intro chime/piano ("dong~ dong~ dong~ dong~").
              // MULT=0 in YM2151 carrier means it sounds 1 octave lower in warm piano range (~440 Hz).
              midi = Math.max(12, midi - 12);
              const fN = midiToFreqN(midi);
              addWrite(tick, baseReg + 0, fN & 0xFF);
              addWrite(tick, baseReg + 1, (fN >> 8) & 0x3F);
              addWrite(tick, baseReg + 3, 0x80); // Warm Triangle wave
              // Strike + natural piano decay envelope across the 16-frame note window
              const decay = [56, 48, 40, 32, 24, 16, 8, 0];
              for (let d = 0; d < decay.length; d++) {
                addWrite(tick + d * 2, baseReg + 2, ymChPan[ch] | decay[d]);
              }
            } else {
              const fN = midiToFreqN(midi);
              let wave = 0x3F;
              let vol = 36;
              if (name === "KILLED") {
                wave = 0x80; // Triangle wave for soft backing chord
                vol = 22;
              } else if (ch === 6) {
                wave = 0x80;
                vol = 52;
              } else if (ch === 4 || ch === 5) {
                wave = 0x3F;
                vol = 44;
              } else {
                wave = 0x3F;
                vol = 36;
              }

              addWrite(tick, baseReg + 0, fN & 0xFF);
              addWrite(tick, baseReg + 1, (fN >> 8) & 0x3F);
              addWrite(tick, baseReg + 3, wave);
              addWrite(tick, baseReg + 2, ymChPan[ch] | vol);
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

  for (let f = 0; f < frameEvents.length; f++) {
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
    for (const [reg, val] of regMap) {
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
  return Buffer.from(stream);
}

if (!fs.existsSync(buildDir)) fs.mkdirSync(buildDir, { recursive: true })

const blob = []
const table = []
let offset = 0

for (const { name } of TRACKS) {
  const data = compileZsm(name)
  blob.push(data)
  table.push({ name, offset, length: data.length })
  console.log(`  ${name}.ZSM -> ${data.length} bytes PSG stream (offset ${offset})`)
  offset += data.length
}

fs.writeFileSync(path.join(buildDir, "music.blob"), Buffer.concat(blob))
console.log(`  Total music blob: ${offset} bytes`)

// C header: track offsets/lengths so audio.c can address each stream in VRAM.
let cCode = `// AUTO-GENERATED by tools/gen_music.mjs — do not edit.
#pragma once
#include <stdint.h>

typedef struct {
    const char *name;
    uint16_t offset;
    uint16_t length;
} MusicTrack;

#define MUSIC_TRACK_COUNT ${table.length}
static const MusicTrack musicTracks[MUSIC_TRACK_COUNT] = {
${table.map((t) => `    { "${t.name}", ${t.offset}, ${t.length} },`).join("\n")}
};
`
fs.writeFileSync(path.join(projectRoot, "src", "music_table.h"), cCode)
console.log(`  Wrote src/music_table.h`)
