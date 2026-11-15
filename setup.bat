@echo off
rem Prepara este PC para desarrollar GuitarHeroWOR_DE: instala herramientas, SDKs y dependencias, y compila todo.
rem Opciones: -Iso "D:\juegos\wor.iso"   -ConMovil   -SinCompilar   -SinInstalar   -GuardarParchesSDK
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\setup\setup.ps1" %*
pause
