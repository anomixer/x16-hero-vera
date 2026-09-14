@echo off
rem Build x16-hero for Apple II VERA: generate assets+music, compile C (Slot 2/4), build HDV.
setlocal
cd /d "%~dp0"

where mos-apple2e-clang >nul 2>&1
if not errorlevel 1 (
    set "CC=mos-apple2e-clang"
) else if exist "C:\dev\llvm-mos-sdk\install\bin\mos-apple2e-clang.bat" (
    set "CC=C:\dev\llvm-mos-sdk\install\bin\mos-apple2e-clang.bat"
) else if exist "C:\dev\llvm-mos-sdk\bin\mos-apple2e-clang.bat" (
    set "CC=C:\dev\llvm-mos-sdk\bin\mos-apple2e-clang.bat"
) else (
    set "CC=mos-apple2e-clang"
)

if not exist build mkdir build

echo [1/4] Generating music (tools\gen_music.mjs) ...
call node tools\gen_music.mjs
if errorlevel 1 goto fail

echo [2/4] Generating assets blob (tools\gen_assets.mjs) ...
call node tools\gen_assets.mjs
if errorlevel 1 goto fail

echo [3/4] Compiling src\main.c + audio.c + disk.c + input.c + mli.s (Slot 2 and Slot 4) ...
call "%CC%" -Oz -T src\link1000.ld -o build\main.bin src\main.c src\text.c src\audio.c src\disk.c src\input.c src\mli.s
if errorlevel 1 goto fail
call "%CC%" -Oz -T src\link1000.ld -DVERA_BASE=0xC400 -o build\main4.bin src\main.c src\text.c src\audio.c src\disk.c src\input.c src\mli.s
if errorlevel 1 goto fail

echo [4/4] Building ProDOS HDV (tools\build_hdv.mjs) ...
call node tools\build_hdv.mjs %*
if errorlevel 1 goto fail

echo.
echo OK: x16-hero-vera.hdv built.
endlocal & exit /b 0

:fail
endlocal & exit /b 1
