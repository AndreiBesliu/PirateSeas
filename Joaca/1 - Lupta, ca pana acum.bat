@echo off
rem Lupta implicita: doua nave ale Coroanei, fara port, fara carte.
rem Flag-uri in plus se pot adauga dupa numele fisierului; le trece mai departe (%*).
start "" "%~dp0..\Packaged\Windows\PirateSeas.exe" -windowed %*
