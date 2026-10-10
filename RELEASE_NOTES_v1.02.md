# H.E.R.O. for Apple II VERA — v1.02

## What's new

- Game files can now be loaded from a ProDOS subdirectory or the mounted volume root, so the disk image can be copied to another disk and launched from its own folder.
- Assets and high scores use ProDOS file operations. High scores are saved alongside the game files and persist across launches.
- Updated the in-game credits to show version 1.02.

## Installation

Copy the game files into a ProDOS directory and launch the included startup entry from that directory. The game locates its asset and high-score files there. The included `x16-hero-vera.hdv` is a ready-to-use disk image.

## Notes

Game assets are loaded during startup; disk access during play is reserved for loading or saving high scores.
