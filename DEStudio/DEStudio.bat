@echo off
rem Abre DE Studio en el navegador (servidor local en 127.0.0.1:8765). Cierra esta ventana para salir.
cd /d "%~dp0"
python server.py %*
pause
