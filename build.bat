@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"

cd /d "%~dp0re-wor"

if not exist "generated\re_wor_init.h" (
    if "%~1"=="" (
        echo.
        echo [!] AVISO: No se encontro "re-wor\generated\re_wor_init.h".
        echo [!] Para compilar el juego completo, primero debes colocar tu
        echo     archivo "default.xex" en "re-wor\assets\" y ejecutar generate_code.bat
        echo.
    )
)

echo ========================================================
echo  Ejecutando CMake Build...
echo ========================================================
cmake --build out/build/win-amd64-relwithdebinfo %*
if errorlevel 1 goto on_error

echo.
echo ========================================================
echo  [EXITO] Operacion completada satisfactoriamente.
echo ========================================================
goto end

:on_error
echo.
echo ========================================================
echo  [ERROR] La compilacion ha fallado. Revisa los mensajes arriba.
echo ========================================================

:end
echo.
pause
