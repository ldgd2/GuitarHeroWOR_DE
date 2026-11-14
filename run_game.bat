@echo off
setlocal
cd /d "%~dp0re-wor"

set "GAME_EXE=out\build\win-amd64-relwithdebinfo\Guitar Hero Warriors of Rock.exe"
set "ASSETS_DIR=%~dp0re-wor\assets"
set "TRACE_LOG=%~dp0re-wor\re_wor_trace.log"

if not exist "%GAME_EXE%" (
    echo.
    echo ========================================================
    echo  [ERROR] No se encuentra el ejecutable:
    echo          "%GAME_EXE%"
    echo.
    echo  Ejecuta primero build.bat para compilar el juego.
    echo ========================================================
    pause
    exit /b 1
)

echo ========================================================
echo  Iniciando Guitar Hero: Warriors of Rock (re-wor)
echo  Ruta de assets: %ASSETS_DIR%
echo  Mods activos:
echo    - mods\
echo    - assets\data\pak\mods\
echo ========================================================
echo [Consola] Monitoreando logs paso a paso en tiempo real...
echo ========================================================
echo.

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0re-wor\stream_game.ps1" -GameExe "%GAME_EXE%" -AssetsDir "%ASSETS_DIR%" -TraceLog "%TRACE_LOG%" -ExtraArgs "%*"

echo.
echo ========================================================
echo  El juego se ha cerrado.
echo ========================================================
pause
