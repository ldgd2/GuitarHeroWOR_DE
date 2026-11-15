@echo off
rem Carga el entorno de compilacion de Visual Studio 2022 (cl/link/rc/ninja/Windows SDK) en esta consola.
rem Sirve cualquier edicion (Build Tools, Community, Professional...): se busca con vswhere.
rem Tambien deja en REWOR_LLVM la carpeta de LLVM/Clang (CMakePresets.json la usa): registro del instalador
rem oficial, clang del PATH o %ProgramFiles%\LLVM, en ese orden.
if not defined REWOR_LLVM call :llvm
if defined VSCMD_VER exit /b 0
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo [ERROR] No esta instalado Visual Studio 2022 ^(o sus Build Tools^). Ejecuta setup.bat.
    exit /b 1
)
set "VSDIR="
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -version [17.0^,18.0^) -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (
    echo [ERROR] Visual Studio 2022 no tiene las herramientas de C++ ^(VC.Tools.x86.x64^). Ejecuta setup.bat.
    exit /b 1
)
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul
exit /b %errorlevel%

:llvm
for /f "tokens=2,*" %%a in ('reg query "HKLM\SOFTWARE\LLVM\LLVM" /ve 2^>nul ^| findstr /i "REG_SZ"') do set "REWOR_LLVM=%%b"
if not defined REWOR_LLVM for /f "delims=" %%i in ('where clang.exe 2^>nul') do if not defined REWOR_LLVM set "REWOR_LLVM=%%~dpi.."
if not defined REWOR_LLVM if exist "%ProgramFiles%\LLVM\bin\clang.exe" set "REWOR_LLVM=%ProgramFiles%\LLVM"
if not defined REWOR_LLVM (
    echo [ERROR] No se encuentra LLVM/Clang. Ejecuta setup.bat.
    exit /b 1
)
for %%i in ("%REWOR_LLVM%") do set "REWOR_LLVM=%%~fi"
set "REWOR_LLVM=%REWOR_LLVM:\=/%"
exit /b 0
