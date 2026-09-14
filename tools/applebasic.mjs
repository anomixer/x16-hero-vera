import fs from "fs"
import path from "path"

// Applesoft BASIC Tokenizer & Compiler for src/*.bas
export const compileApplesoftBasic = (srcDir, filename) => {
  const filePath = path.isAbsolute(filename) ? filename : path.join(srcDir, filename)
  const source = fs.readFileSync(filePath, "utf-8")
  const lines = source.split(/\r?\n/)

  // Applesoft BASIC Token table ordered by descending length for greedy matching
  const tokenTable = [
    { name: "HCOLOR=", token: 0x92 },
    { name: "NOTRACE", token: 0x9C },
    { name: "INVERSE", token: 0x9E },
    { name: "RESTORE", token: 0xAE },
    { name: "HIMEM:", token: 0xA3 },
    { name: "LOMEM:", token: 0xA4 },
    { name: "RETURN", token: 0xB1 },
    { name: "NORMAL", token: 0x9D },
    { name: "RESUME", token: 0xA6 },
    { name: "RECALL", token: 0xA7 },
    { name: "SPEED=", token: 0xA9 },
    { name: "COLOR=", token: 0xA0 },
    { name: "SHLOAD", token: 0x9A },
    { name: "RIGHT$", token: 0xE9 },
    { name: "SCALE=", token: 0x99 },
    { name: "PRINT", token: 0xBA },
    { name: "INPUT", token: 0x84 },
    { name: "GOSUB", token: 0xB0 },
    { name: "HPLOT", token: 0x93 },
    { name: "XDRAW", token: 0x95 },
    { name: "CLEAR", token: 0xBD },
    { name: "STORE", token: 0xA8 },
    { name: "TRACE", token: 0x9B },
    { name: "FLASH", token: 0x9F },
    { name: "ONERR", token: 0xA5 },
    { name: "SCRN(", token: 0xD7 },
    { name: "LEFT$", token: 0xE8 },
    { name: "CHR$", token: 0xE7 },
    { name: "STR$", token: 0xE4 },
    { name: "MID$", token: 0xEA },
    { name: "HGR2", token: 0x90 },
    { name: "PR#", token: 0x8A },
    { name: "IN#", token: 0x8B },
    { name: "CALL", token: 0x8C },
    { name: "PLOT", token: 0x8D },
    { name: "HLIN", token: 0x8E },
    { name: "VLIN", token: 0x8F },
    { name: "HTAB", token: 0x96 },
    { name: "HOME", token: 0x97 },
    { name: "ROT=", token: 0x98 },
    { name: "VTAB", token: 0xA2 },
    { name: "GOTO", token: 0xAB },
    { name: "DATA", token: 0x83 },
    { name: "NEXT", token: 0x82 },
    { name: "TEXT", token: 0x89 },
    { name: "POKE", token: 0xB9 },
    { name: "PEEK", token: 0xE2 },
    { name: "THEN", token: 0xC4 },
    { name: "CONT", token: 0xBB },
    { name: "LIST", token: 0xBC },
    { name: "WAIT", token: 0xB5 },
    { name: "LOAD", token: 0xB6 },
    { name: "SAVE", token: 0xB7 },
    { name: "STOP", token: 0xB3 },
    { name: "DRAW", token: 0x94 },
    { name: "STEP", token: 0xC7 },
    { name: "READ", token: 0x87 },
    { name: "TAB(", token: 0xC0 },
    { name: "SPC(", token: 0xC3 },
    { name: "SGN", token: 0xD2 },
    { name: "INT", token: 0xD3 },
    { name: "ABS", token: 0xD4 },
    { name: "USR", token: 0xD5 },
    { name: "FRE", token: 0xD6 },
    { name: "PDL", token: 0xD8 },
    { name: "POS", token: 0xD9 },
    { name: "SQR", token: 0xDA },
    { name: "RND", token: 0xDB },
    { name: "LOG", token: 0xDC },
    { name: "EXP", token: 0xDD },
    { name: "COS", token: 0xDE },
    { name: "SIN", token: 0xDF },
    { name: "TAN", token: 0xE0 },
    { name: "ATN", token: 0xE1 },
    { name: "LEN", token: 0xE3 },
    { name: "VAL", token: 0xE5 },
    { name: "ASC", token: 0xE6 },
    { name: "AND", token: 0xCD },
    { name: "NOT", token: 0xC6 },
    { name: "FOR", token: 0x81 },
    { name: "DEL", token: 0x85 },
    { name: "DIM", token: 0x86 },
    { name: "HGR", token: 0x91 },
    { name: "POP", token: 0xA1 },
    { name: "LET", token: 0xAA },
    { name: "RUN", token: 0xAC },
    { name: "REM", token: 0xB2 },
    { name: "DEF", token: 0xB8 },
    { name: "GET", token: 0xBE },
    { name: "NEW", token: 0xBF },
    { name: "END", token: 0x80 },
    { name: "GR", token: 0x88 },
    { name: "ON", token: 0xB4 },
    { name: "TO", token: 0xC1 },
    { name: "FN", token: 0xC2 },
    { name: "AT", token: 0xC5 },
    { name: "OR", token: 0xCE },
    { name: "IF", token: 0xAD },
    { name: "+", token: 0xC8 },
    { name: "-", token: 0xC9 },
    { name: "*", token: 0xCA },
    { name: "/", token: 0xCB },
    { name: "^", token: 0xCC },
    { name: ">", token: 0xCF },
    { name: "=", token: 0xD0 },
    { name: "<", token: 0xD1 },
    { name: "&", token: 0xAF },
  ]

  let mem = []
  let currAddr = 0x0801

  for (const rawLine of lines) {
    const trimmed = rawLine.trim()
    if (!trimmed) continue
    if (trimmed.startsWith(";") || trimmed.startsWith("//")) continue

    const match = trimmed.match(/^(\d+)\s*(.*)$/)
    if (!match) continue
    const lineNo = parseInt(match[1], 10)
    const code = match[2]

    let tokenized = []
    let inString = false
    let i = 0

    while (i < code.length) {
      const char = code[i]
      if (char === '"') {
        inString = !inString
        tokenized.push(char.charCodeAt(0))
        i++
        continue
      }

      if (inString) {
        tokenized.push(char.charCodeAt(0))
        i++
        continue
      }

      // Skip whitespace outside strings
      if (char === " " || char === "\t") {
        i++
        continue
      }

      // Match keyword token outside strings
      let matchedToken = null
      for (const tok of tokenTable) {
        const sub = code.substring(i, i + tok.name.length).toUpperCase()
        if (sub === tok.name) {
          matchedToken = tok
          break
        }
      }

      if (matchedToken) {
        tokenized.push(matchedToken.token)
        i += matchedToken.name.length
      } else {
        tokenized.push(char.charCodeAt(0))
        i++
      }
    }

    const lineLen = tokenized.length + 1
    const nextAddr = currAddr + 4 + lineLen
    mem.push(nextAddr & 0xFF, (nextAddr >> 8) & 0xFF)
    mem.push(lineNo & 0xFF, (lineNo >> 8) & 0xFF)
    mem.push(...tokenized, 0x00)
    currAddr = nextAddr
  }

  // End of program marker
  mem.push(0x00, 0x00)
  return new Uint8Array(mem)
}
