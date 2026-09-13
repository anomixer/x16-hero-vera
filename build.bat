@echo off
rem Build x16-hero for Apple II VERA: generate assets+music, compile C (Slot 2/4), build HDV.
setlocal
set SDK=C:\dev\llvm-mos-sdk\install
set CC=%SDK%\bin\mos-apple2e-clang.bat
cd /d "%~dp0"

if not exist build mkdir build

echo [1/4] Generating music + assets ...
call node tools\gen_music.mjs
if errorlevel 1 goto fail
call node tools\gen_assets.mjs
if errorlevel 1 goto fail

echo [2/4] Compiling src\main.c + audio.c + disk.c + input.c + mli.s (Slot 2 and Slot 4) ...
call "%CC%" -Oz -T src\link1000.ld -o build\main.bin src\main.c src\text.c src\audio.c src\disk.c src\input.c src\mli.s
if errorlevel 1 goto fail
call "%CC%" -Oz -T src\link1000.ld -DVERA_BASE=0xC400 -o build\main4.bin src\main.c src\text.c src\audio.c src\disk.c src\input.c src\mli.s
if errorlevel 1 goto fail

echo [3/4] Building ProDOS HDV (x16-hero-vera.hdv) ...
call node tools\build_hdv.mjs
if errorlevel 1 goto fail

echo.
echo OK: x16-hero-vera.hdv built.
endlocal & exit /b 0

:fail
endlocal & exit /b 1
