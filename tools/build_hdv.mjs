// build_hdv.mjs — Package x16-hero into a bootable ProDOS 8 HDV (.hdv).
//
// Boot chain: ProDOS HDV -> BASIC.SYSTEM -> Applesoft STARTUP -> BRUN MAIN.BIN.
// The game's assets.blob is placed at FIXED blocks (ASSET_START_BLOCK=900, kept
// in sync with src/disk.c) and read directly by the 6502 via MLI READ_BLOCK.
// Only MAIN.BIN / MAIN4.BIN and STARTUP get named directory entries.
// Modeled on C:\dev\Time-Pilot\TimePilot-IIvera\tools\build_hdv.mjs.
import fs from "fs"
import path from "path"
import { fileURLToPath } from "url"
import { compileApplesoftBasic } from "../../veratest/src/applebasic.mjs"

const __dirname = path.dirname(fileURLToPath(import.meta.url))
const projectRoot = path.resolve(__dirname, "..")
const srcDir = path.join(projectRoot, "src")
const buildDir = path.join(projectRoot, "build")
const baseHdvPath = path.join(projectRoot, "800kb.hdv")
const BLOCK = 512
const HISCORE_START_BLOCK = 899
const ASSET_START_BLOCK = 900

const outFileName = "x16-hero-vera.hdv"
const outPath = path.join(projectRoot, outFileName)

if (!fs.existsSync(baseHdvPath)) throw new Error(`Base HDV not found: ${baseHdvPath}`)
for (const f of ["main.bin", "assets.blob"])
  if (!fs.existsSync(path.join(buildDir, f))) throw new Error(`build/${f} missing — run build.bat first`)

const disk = new Uint8Array(fs.readFileSync(baseHdvPath))
const TOTAL = disk.length / BLOCK

// ---- High Scores: fixed seedling block (HISCORE_START_BLOCK=899) ----
const HISCORE_SIZE = 163
let hiscoreData = null
if (fs.existsSync(outPath)) {
  try {
    const prevDisk = fs.readFileSync(outPath)
    if (prevDisk.length >= (HISCORE_START_BLOCK + 1) * BLOCK) {
      const prevBlock = prevDisk.subarray(HISCORE_START_BLOCK * BLOCK, (HISCORE_START_BLOCK + 1) * BLOCK)
      if (prevBlock[161] === 0x48 && prevBlock[162] === 0x53) {
        hiscoreData = new Uint8Array(prevBlock)
        console.log(`  Preserving existing HISCORE.BIN from ${outFileName}`)
      }
    }
  } catch (e) {}
}
if (!hiscoreData) {
  hiscoreData = new Uint8Array(BLOCK)
  let hp = 0
  const defaultNames = [
    "roderrick  ", "elvin a    ", "guybrush   ", "sandy pantz", "z mckracken",
    "bruce lee  ", "armakuni   ", "rockford   ", "giana      ", "monty mole "
  ]
  for (const n of defaultNames) {
    for (let i = 0; i < 11; i++) hiscoreData[hp++] = n.charCodeAt(i) || 32
    hiscoreData[hp++] = 0
  }
  const defaultSaved = [10, 9, 8, 7, 6, 5, 4, 3, 2, 1]
  for (const s of defaultSaved) hiscoreData[hp++] = s
  const defaultTimes = [20,0, 18,0, 16,0, 14,0, 12,0, 10,0, 8,0, 6,0, 4,0, 2,0]
  for (const t of defaultTimes) hiscoreData[hp++] = t
  const defaultStart = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]
  for (const st of defaultStart) hiscoreData[hp++] = st
  hiscoreData[hp++] = 3
  hiscoreData[hp++] = 0x48 // 'H'
  hiscoreData[hp++] = 0x53 // 'S'
}
disk.set(hiscoreData, HISCORE_START_BLOCK * BLOCK)
console.log(`  HISCORE.BIN ${HISCORE_SIZE}B at block ${HISCORE_START_BLOCK}`)

// ---- Assets: fixed block range (keep in sync with src/disk.c ASSET_START_BLOCK) ----
const assets = new Uint8Array(fs.readFileSync(path.join(buildDir, "assets.blob")))
const ASSET_BLOCKS = Math.ceil(assets.length / BLOCK)
disk.set(assets, ASSET_START_BLOCK * BLOCK)
disk.fill(0, ASSET_START_BLOCK * BLOCK + assets.length, (ASSET_START_BLOCK + ASSET_BLOCKS) * BLOCK)
console.log(`  assets.blob ${assets.length}B at blocks ${ASSET_START_BLOCK}..${ASSET_START_BLOCK + ASSET_BLOCKS - 1}`)

// ---- Block allocator (skip 0..99 system reserve + hiscore + asset range) ----
const used = new Set()
for (let b = 0; b < 100; b++) used.add(b)
used.add(HISCORE_START_BLOCK)
for (let b = ASSET_START_BLOCK; b < ASSET_START_BLOCK + ASSET_BLOCKS; b++) used.add(b)

const newlyAllocated = []
let nextFree = 100
const allocate = () => {
  while (used.has(nextFree)) nextFree++
  const b = nextFree++
  used.add(b)
  newlyAllocated.push(b)
  return b
}

function writeFile(name, fileType, aux, data) {
  const size = data.length
  let stType, keyBlock, totalBlocks
  if (size <= BLOCK) {
    stType = 1; keyBlock = allocate(); disk.set(data, keyBlock * BLOCK); totalBlocks = 1
  } else {
    stType = 2; keyBlock = allocate()
    const idx = new Uint8Array(BLOCK)
    const n = Math.ceil(size / BLOCK)
    for (let i = 0; i < n; i++) {
      const db = allocate()
      disk.set(data.subarray(i * BLOCK, Math.min(size, (i + 1) * BLOCK)), db * BLOCK)
      idx[i] = db & 0xFF
      idx[i + 256] = (db >> 8) & 0xFF
    }
    disk.set(idx, keyBlock * BLOCK)
    totalBlocks = 1 + n
  }
  return { name, stType, fileType, keyBlock, totalBlocks, eof: size, aux }
}

// Register an ALREADY-PLACED fixed block range as a ProDOS "sapling" file.
function addDataFile(name, fileType, baseBlock, numBlocks) {
  const idxBlock = allocate()
  const idx = new Uint8Array(BLOCK)
  for (let i = 0; i < numBlocks; i++) {
    const db = baseBlock + i
    idx[i] = db & 0xFF
    idx[i + 256] = (db >> 8) & 0xFF
  }
  disk.set(idx, idxBlock * BLOCK)
  return { name, stType: 2, fileType, keyBlock: idxBlock, totalBlocks: 1 + numBlocks, eof: numBlocks * BLOCK, aux: 0x2000 }
}

const startupBas = path.join(srcDir, "startup.bas")
const startupBytes = new Uint8Array(compileApplesoftBasic(srcDir, "startup.bas"))
const fStartup = writeFile("STARTUP", 0xFC, 0x0801, startupBytes)

function addMain(buildName, outName) {
  const raw = new Uint8Array(fs.readFileSync(path.join(buildDir, buildName)))
  const loadAddr = raw[0] | (raw[1] << 8)
  const body = raw.subarray(4)
  const f = writeFile(outName, 0x06, loadAddr, body)
  console.log(`  ${outName} ${body.length}B (load=$${loadAddr.toString(16).toUpperCase()}, key=${f.keyBlock})`)
  return f
}
const fMain = addMain("main.bin", "MAIN.BIN")
const appFiles = [fStartup, fMain]
if (fs.existsSync(path.join(buildDir, "main4.bin"))) appFiles.push(addMain("main4.bin", "MAIN4.BIN"))

const fHiscore = {
  name: "HISCORE.BIN",
  stType: 1,
  fileType: 0x06,
  keyBlock: HISCORE_START_BLOCK,
  totalBlocks: 1,
  eof: HISCORE_SIZE,
  aux: 0x2000
}
const dataFiles = [
  addDataFile("ASSETS", 0x06, ASSET_START_BLOCK, ASSET_BLOCKS),
  fHiscore
]

// ---- Rewrite the root directory (block 2) ----
const vol = disk.subarray(2 * BLOCK, 3 * BLOCK)
const nameOf = (off, len) => String.fromCharCode(...vol.subarray(off + 1, off + 1 + len))
const keep = []
for (let i = 1; i <= 12; i++) {
  const off = 4 + i * 39
  if (vol[off] === 0) continue
  const nameLen = vol[off] & 0x0F
  const name = nameOf(off, nameLen)
  if (name === "PRODOS" || name === "CLOCK.SYSTEM" || name === "BASIC.SYSTEM") keep.push(vol.slice(off, off + 39))
}
for (let i = 1; i <= 12; i++) vol.fill(0, 4 + i * 39, 4 + (i + 1) * 39)

let idx = 1
for (const raw of keep) { vol.set(raw, 4 + idx * 39); idx++ }
for (const e of [...appFiles, ...dataFiles]) {
  const off = 4 + idx * 39
  vol[off] = (e.stType << 4) | (e.name.length & 0x0F)
  vol.set([...e.name].map((c) => c.charCodeAt(0)), off + 1)
  vol[off + 0x10] = e.fileType
  vol[off + 0x11] = e.keyBlock & 0xFF
  vol[off + 0x12] = (e.keyBlock >> 8) & 0xFF
  vol[off + 0x13] = e.totalBlocks & 0xFF
  vol[off + 0x14] = (e.totalBlocks >> 8) & 0xFF
  vol[off + 0x15] = e.eof & 0xFF
  vol[off + 0x16] = (e.eof >> 8) & 0xFF
  vol[off + 0x17] = (e.eof >> 16) & 0xFF
  vol[off + 0x1E] = 0xC3
  vol[off + 0x1F] = e.aux & 0xFF
  vol[off + 0x20] = (e.aux >> 8) & 0xFF
  vol[off + 0x25] = 2
  vol[off + 0x26] = 0
  idx++
}
const entryCount = keep.length + appFiles.length + dataFiles.length
vol[0x25] = entryCount & 0xFF
vol[0x26] = (entryCount >> 8) & 0xFF

// ---- Mark used blocks in the bitmap (bit set = FREE, clear = USED) ----
const setUsed = (b) => {
  const byteIdx = Math.floor(b / 8)
  const bit = 7 - (b % 8)
  disk[6 * BLOCK + byteIdx] &= ~(1 << bit)
}
for (const b of newlyAllocated) setUsed(b)
setUsed(HISCORE_START_BLOCK)
for (let b = ASSET_START_BLOCK; b < ASSET_START_BLOCK + ASSET_BLOCKS; b++) setUsed(b)

fs.writeFileSync(outPath, disk)
console.log(`\n  Built ${outPath} (${disk.length} bytes)`)
console.log(`  Root files: ${keep.length} system + ${appFiles.map(f => f.name).join(" + ")} + ASSETS + HISCORE.BIN`)

