@echo off
rem Ca 2, dar cu cartea INCHISA: nu citeste si nu scrie nimic.
rem Flag-uri in plus se pot adauga dupa numele fisierului; le trece mai departe (%*).
start "" "%~dp0..\Packaged\Windows\PirateSeas.exe" -windowed -Convoy=2 -Port=1 -ConvoyX=40000 -ConvoyY=30000 -EnemyCount=0 -Ledger=0 %*
