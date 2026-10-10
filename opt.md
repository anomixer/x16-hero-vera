# ProDOS file-based asset and high-score I/O

## Goal

Allow `MAIN.BIN`, `ASSETS`, and `HISCORE.BIN` to be copied to another ProDOS
volume without depending on the original disk image's allocation of blocks 899
and 900. Keep all asset disk reads in the boot preload; gameplay and level
transitions continue to use preloaded VRAM maps and assets.

## Implementation

- `src/mli.s` now wraps ProDOS `OPEN`, `READ`, `WRITE`, `CLOSE`, `SET_MARK`,
  `SET_EOF`, and `CREATE`, retaining the existing `$40-$4F` zero-page save and
  restore protection around every MLI call.
- `src/disk.c` opens relative path `ASSETS`, seeks to the generated asset
  offsets, and reads through a 512-byte program buffer. `disk_close_assets()`
  closes the asset file immediately after `load_resources()` completes.
- `HISCORE.BIN` is opened only for score load/save. Reads require the full
  requested record; failed/short reads return zeroed data so the existing
  magic check falls back to factory scores. A missing score file is created as
  a ProDOS binary file on the next save. Writes verify transferred length and
  set EOF to the record length.
- The ProDOS OPEN I/O buffer is a linker-allocated 1 KB array aligned to a
  256-byte boundary. The separate 512-byte transfer buffer is also aligned
  and remains distinct from the OPEN I/O buffer.
- `tools/build_hdv.mjs` still creates the same `ASSETS` and `HISCORE.BIN`
  entries for the standard HDV image. Their fixed placement is now only a disk
  image construction detail; runtime code follows ProDOS directory entries.
- Startup first uses ProDOS `GET_PREFIX` to try `ASSETS` in the active directory,
  then uses `ON_LINE` to search mounted volume roots. Score I/O uses the directory
  where `ASSETS` was found, including copied subdirectory installs.

## Manual validation checklist

1. Build both Slot 2 and Slot 4 versions and boot the standard HDV.
2. Copy `MAIN.BIN`, `ASSETS`, and `HISCORE.BIN` with A2 DeskTop to another
   ProDOS volume where the file allocations differ; launch `MAIN.BIN` from that
   directory and verify title assets and gameplay maps load.
3. Change a high score, restart, and verify it persists in `HISCORE.BIN`.
   Check that unrelated files on the destination volume remain intact.
4. Try a missing, short, and read-only `HISCORE.BIN`. Missing/invalid data
   should fall back to factory scores; saving on a read-only volume should not
   corrupt unrelated data.
5. Confirm the game does not read `ASSETS` after the boot preload. High-score
   file access at score load/save remains expected.

## Review notes

- The file wrapper checks MLI status and READ/WRITE transfer counts. Asset
  streaming stops on a failed or short read; the game currently has no dedicated
  on-screen disk-error screen, so a missing or damaged `ASSETS` file will leave
  an incomplete scene and should be treated as an installation error.
- ProDOS requires the OPEN I/O buffer to be a free, page-aligned 1 KB region.
  The linker now reserves it as part of the application image; confirm this
  behaves correctly on the target ProDOS/BASIC.SYSTEM setup.
- When the current prefix does not point to the copied subdirectory, fallback
  search covers mounted volume roots only. Launch from the containing directory
  or set the ProDOS prefix to that directory for subdirectory installs.
- The memory figures in this document set no new linker reservation. Recheck
  the linked image and worst-case soft-stack use if the game grows toward
  `$9C00` or if the stack base changes.

## References

- [`src/disk.c`](src/disk.c): file-based streaming and score I/O.
- [`src/mli.s`](src/mli.s): ProDOS MLI wrappers and zero-page preservation.
- [`src/link1000.ld`](src/link1000.ld): load range and soft-stack placement.
- [`tools/build_hdv.mjs`](tools/build_hdv.mjs): ProDOS file entries in the HDV.
- [ProDOS Technical Reference: Calls to the MLI](https://prodos8.com/docs/techref/calls-to-the-mli/)
- [ProDOS Technical Reference: File Use](https://prodos8.com/docs/techref/file-use/)
