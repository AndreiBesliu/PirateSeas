@echo off
rem Ca 2, dar cu cele doua nave ale Coroanei. Cartea e DESCHISA. OWNER_VERIFY 42 (partea 2).
rem Flag-uri in plus se pot adauga dupa numele fisierului; le trece mai departe (%*).
start "" "%~dp0..\Packaged\Windows\PirateSeas.exe" -windowed -Convoy=2 -Port=1 -ConvoyX=40000 -ConvoyY=30000 %*
