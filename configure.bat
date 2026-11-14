@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"

cd /d "%~dp0re-wor"

echo ========================================================
echo  Configurando CMake con Clang y Ninja...
echo ========================================================
cmake --preset win-amd64-relwithdebinfo
if errorlevel 1 goto on_error

echo.
echo ========================================================
echo  [EXITO] CMake configurado correctamente.
echo ========================================================
goto end

:on_error
echo.
echo ========================================================
echo  [ERROR] La configuracion de CMake ha fallado. Revisa los mensajes arriba.
echo ========================================================

:end
echo.
pause
