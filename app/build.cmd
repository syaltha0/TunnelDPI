@ECHO OFF
REM TunnelDPI - derle, pkg\ paket klasorunu ve TunnelDPI-Setup.exe kurulumunu hazirla
REM Kullanim: build.cmd [zapret_klasoru]
REM   zapret_klasoru: zapret surum arsivinin acildigi klasor (icinde binaries\ ve files\fake\).
REM   Verilmezse ZAPRET_DIR ortam degiskenine bakilir; o da yoksa zapret motoru pakete girmez.
REM   tunneldpi-engine.exe ..\engine icinde derlenmis olmali. WinDivert.dll / WinDivert64.sys WINDIVERT_DIR
REM   (WinDivert arsivinin x64 klasoru) ya da dist\x86_64 icinden alinir.
REM   pkg\ her derlemede bastan olusturulur; dist\ (tasinabilir kullanim) degistirilmez.
SETLOCAL
PUSHD "%~dp0"

WHERE mingw32-make >NUL 2>&1 || (
    ECHO mingw32-make bulunamadi. Kurulum: winget install BrechtSanders.WinLibs.POSIX.UCRT
    GOTO :fail
)

mingw32-make || GOTO :fail

IF EXIST pkg RMDIR /S /Q pkg
FOR %%D IN (pkg pkg\engine pkg\zapret pkg\zapret\fake pkg\lists pkg\licenses) DO MKDIR %%D

COPY /Y TunnelDPI.exe pkg\ >NUL || GOTO :fail
COPY /Y hazir-ayarlar.ini pkg\ >NUL || GOTO :fail
COPY /Y hazir-ayarlar.ini.sig pkg\ >NUL || GOTO :fail
COPY /Y lists\*.txt pkg\lists\ >NUL
COPY /Y ..\LICENSE pkg\licenses\LICENSE.txt >NUL 2>&1
COPY /Y ..\NOTICE pkg\licenses\NOTICE.txt >NUL 2>&1

REM GoodbyeDPI motoru
COPY /Y ..\engine\tunneldpi-engine.exe pkg\engine\ >NUL 2>&1 || ECHO UYARI: ..\engine\tunneldpi-engine.exe yok; once motoru derleyin.
SET "WDIR=%WINDIVERT_DIR%"
IF "%WDIR%"=="" SET "WDIR=dist\x86_64"
COPY /Y "%WDIR%\WinDivert.dll" pkg\engine\ >NUL 2>&1 || ECHO UYARI: WinDivert.dll bulunamadi (WINDIVERT_DIR).
COPY /Y "%WDIR%\WinDivert64.sys" pkg\engine\ >NUL 2>&1

REM zapret motoru
SET "ZDIR=%~1"
IF "%ZDIR%"=="" SET "ZDIR=%ZAPRET_DIR%"
IF NOT "%ZDIR%"=="" (
    FOR %%F IN (winws.exe cygwin1.dll WinDivert.dll WinDivert64.sys) DO (
        COPY /Y "%ZDIR%\binaries\windows-x86_64\%%F" pkg\zapret\ >NUL || GOTO :fail
    )
    COPY /Y "%ZDIR%\files\fake\*.bin" pkg\zapret\fake\ >NUL
    COPY /Y "%ZDIR%\docs\LICENSE.txt" pkg\licenses\LICENSE-zapret.txt >NUL 2>&1
)
IF NOT EXIST pkg\zapret\winws.exe ECHO UYARI: zapret yok; zapret profilleri calismaz. build.cmd ^<zapret_klasoru^>

REM Kurulum paketi
WHERE python >NUL 2>&1 || (
    ECHO python bulunamadi; TunnelDPI-Setup.exe olusturulmadi.
    GOTO :done
)
python setup\pack.py pkg setup\payload.bin || GOTO :fail
mingw32-make setup || GOTO :fail

:done
ECHO.
ECHO Paket: %CD%\pkg
IF EXIST TunnelDPI-Setup.exe ECHO Kurulum: %CD%\TunnelDPI-Setup.exe
POPD
EXIT /B 0

:fail
POPD
EXIT /B 1
