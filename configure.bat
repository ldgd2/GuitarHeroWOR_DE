@echo off
call "%~dp0tools\setup\vsenv.bat" || goto on_error

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
