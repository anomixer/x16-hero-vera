import fs from "fs"

const input = process.argv[2]
const output = process.argv[3]
if (!input || !output) throw new Error("usage: node render_psg_wav.mjs input.psg output.wav")

const SAMPLE_RATE = 48000
const FPS = 60
const SAMPLES_PER_FRAME = SAMPLE_RATE / FPS
const PSG_CLOCK = 25000000 / 512
const volumeLut = [0,4,8,12,16,17,18,20,21,22,23,25,26,28,30,31,33,35,37,40,42,45,47,50,53,56,60,63,67,71,75,80,85,90,95,101,107,113,120,127,135,143,151,160,170,180,191,202,214,227,241,255,270,286,303,321,341,361,382,405,429,455,482,511]
const data = fs.readFileSync(input)
const frames = []
let p = 0
while (p < data.length) {
  const count = data[p++]
  if (count === 0xff) break
  const writes = []
  for (let i = 0; i < count; i++) writes.push([data[p++], data[p++]])
  frames.push(writes)
}

const regs = new Uint8Array(64)
const phase = new Float64Array(16)
let noise = 1
const pcm = new Int16Array(frames.length * SAMPLES_PER_FRAME * 2)
let out = 0
for (const writes of frames) {
  for (const [reg, value] of writes) if (reg < 64) regs[reg] = value
  for (let s = 0; s < SAMPLES_PER_FRAME; s++) {
    noise = ((noise << 1) | ((((noise >>> 1) ^ (noise >>> 2) ^ (noise >>> 4) ^ (noise >>> 15)) & 1))) & 0xffff
    const noiseVal = (noise & 1) ? 1 : -1
    let mix = 0
    for (let ch = 0; ch < 16; ch++) {
      const ctrl = regs[ch * 4 + 2]
      const vol = ctrl & 0x3f
      if (!vol || !(ctrl & 0xc0)) continue
      const freq = regs[ch * 4] | (regs[ch * 4 + 1] << 8)
      phase[ch] += (freq / 131072) * (PSG_CLOCK / SAMPLE_RATE)
      phase[ch] -= Math.floor(phase[ch])
      const wave = regs[ch * 4 + 3]
      const type = wave >>> 6
      const pw = wave & 0x3f
      let sample
      if (type === 0) sample = phase[ch] < (pw + 1) / 128 ? 1 : -1
      else if (type === 1) sample = 2 * phase[ch] - 1
      else if (type === 2) sample = phase[ch] < 0.5 ? 4 * phase[ch] - 1 : 3 - 4 * phase[ch]
      else sample = noiseVal
      const voiceSig = sample * (volumeLut[vol] / 511)
      if (ctrl & 0xc0) mix += voiceSig
    }
    const value = Math.max(-32767, Math.min(32767, Math.round(Math.tanh(mix / 3) * 30000)))
    pcm[out++] = value
    pcm[out++] = value
  }
}

const header = Buffer.alloc(44)
header.write("RIFF", 0); header.writeUInt32LE(36 + pcm.byteLength, 4)
header.write("WAVE", 8); header.write("fmt ", 12); header.writeUInt32LE(16, 16)
header.writeUInt16LE(1, 20); header.writeUInt16LE(2, 22)
header.writeUInt32LE(SAMPLE_RATE, 24); header.writeUInt32LE(SAMPLE_RATE * 4, 28)
header.writeUInt16LE(4, 32); header.writeUInt16LE(16, 34)
header.write("data", 36); header.writeUInt32LE(pcm.byteLength, 40)
fs.writeFileSync(output, Buffer.concat([header, Buffer.from(pcm.buffer)]))
console.log(`${input}: ${frames.length} frames -> ${output}`)
