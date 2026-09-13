// render_map.mjs — Render the expected layer0 view from TILES/PAL/MAP assets,
// for comparison against the AppleWin screenshot. 4bpp 16x16 tiles.
// Usage: node render_map.mjs <mapN.bin> <out.png> [vscrollPx]
import fs from "fs"
import path from "path"
import { fileURLToPath } from "url"
const __dirname = path.dirname(fileURLToPath(import.meta.url))
const dataDir = "C:/dev/x16-hero"
const mapFile = process.argv[2]
const outFile = process.argv[3]
const vscroll = parseInt(process.argv[4] || "0", 10)

// --- Load assets (skip 2-byte load-addr header where present) ---
const tilesRaw = fs.readFileSync(path.join(dataDir, "TILES.BIN")).subarray(2)
const palRaw   = fs.readFileSync(path.join(dataDir, "PAL.BIN")).subarray(2)
const mapRaw   = fs.readFileSync(mapFile).subarray(2)

// Palette 4:4:4 -> RGB888
const pal = new Array(256)
for (let i = 0; i < 256; i++) {
  const v = palRaw[i*2] | (palRaw[i*2+1] << 8)
  const r = ((v >> 8) & 0xF) << 4
  const g = ((v >> 4) & 0xF) << 4
  const b = (v & 0xF) << 4
  pal[i] = [r,g,b]
}

// Tile size 16x16, 4bpp = 128 bytes/tile
const TILES = 128
const W = 20   // viewport cols (320/16)
const H = 15   // viewport rows (240/16)
const COLS = 64
const ROWS = 32

const out = Buffer.alloc(W*16 * H*16 * 3)
for (let r = 0; r < H; r++) {
  const mapRow = Math.floor((vscroll + r*16) / 16)
  for (let c = 0; c < W; c++) {
    const idx = (mapRow * COLS + c) * 2
    const tile = mapRaw[idx]
    const tbase = tile * TILES
    for (let py = 0; py < 16; py++) {
      for (let px = 0; px < 16; px += 2) {
        const byte = tilesRaw[tbase + py*8 + px/2]
        const p0 = (byte >> 4) & 0xF
        const p1 = byte & 0xF
        for (let k = 0; k < 2; k++) {
          const pi = k===0 ? p0 : p1
          const col = pal[pi]
          const o = ((r*16+py)*W*16 + c*16 + px + k) * 3
          out[o]=col[0]; out[o+1]=col[1]; out[o+2]=col[2]
        }
      }
    }
  }
}

// Write raw BMP (24-bit)
const fileHdr = Buffer.alloc(14)
fileHdr.write("BM",0,2);
const imgSize = out.length
const dataOff = 14+40
fileHdr.writeUInt32LE(dataOff,10);
const infoHdr = Buffer.alloc(40)
infoHdr.writeUInt32LE(40,0)
infoHdr.writeInt32LE(W*16,4)
infoHdr.writeInt32LE(H*16,8)
infoHdr.writeUInt16LE(1,12)
infoHdr.writeUInt16LE(24,14)
infoHdr.writeUInt32LE(imgSize,20)
// bottom-up rows
const rows = Buffer.alloc(imgSize)
for (let y = 0; y < H*16; y++) {
  const src = (H*16-1-y)*W*16*3
  out.copy(rows, y*W*16*3, src, src+W*16*3)
}
fs.writeFileSync(outFile, Buffer.concat([fileHdr, infoHdr, rows]))
console.log(`wrote ${outFile} (${W*16}x${H*16}) vscroll=${vscroll}`)
