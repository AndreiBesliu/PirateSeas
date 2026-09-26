@echo off
rem Convoi de doi negustori la ~500 m si o rada. Cartea navei e DESCHISA: continui de unde ai ramas. OWNER_VERIFY 41 si 42 (partea 1).
rem Flag-uri in plus se pot adauga dupa numele fisierului; le trece mai departe (%*).
start "" "%~dp0..\Packaged\Windows\PirateSeas.exe" -windowed -Convoy=2 -Port=1 -ConvoyX=40000 -ConvoyY=30000 -EnemyCount=0 %*
