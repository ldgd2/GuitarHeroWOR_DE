@echo off
echo ========================================================
echo  Generador de Codigo C++ (re_wor_codegen)
echo ========================================================

if not exist "%~dp0re-wor\assets\default.xex" (
    echo.
    echo ========================================================
    echo  [ERROR] No se encuentra el ejecutable:
    echo          "%~dp0re-wor\assets\default.xex"
    echo ========================================================
    echo.
    echo  Por favor, copia tu archivo "default.xex" extraido de
    echo  la ISO de Xbox 360 en la carpeta:
    echo      re-wor\assets\default.xex
    echo.
    echo  Luego vuelve a hacer doble clic en este archivo.
    echo.
    pause
    exit /b 1
)

call "%~dp0build.bat" --target re_wor_codegen
