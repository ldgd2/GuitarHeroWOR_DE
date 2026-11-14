@echo off
rem Gestor de mods y conversor de canciones (wormod)
cd /d "%~dp0tools\wormod"
where pythonw >NUL 2>NUL && (start "" pythonw app.py) || (python app.py ^& pause)
