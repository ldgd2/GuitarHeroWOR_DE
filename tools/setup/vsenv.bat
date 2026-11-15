@echo off
rem Carga el entorno de compilacion de Visual Studio 2022 (cl/link/rc/ninja/Windows SDK) en esta consola.
rem Sirve cualquier edicion (Build Tools, Community, Professional...): se busca con vswhere.
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
