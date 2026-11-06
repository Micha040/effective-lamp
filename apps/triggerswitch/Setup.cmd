@echo off
setlocal
title TriggerSwitch Setup
if not exist "%~dp0Setup.ps1" goto :notextracted
if not exist "%~dp0TriggerSwitch.cs" goto :notextracted
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Setup.ps1" %*
echo.
pause
exit /b

:notextracted
echo.
echo  Bitte zuerst die ZIP-Datei entpacken (Rechtsklick ^> "Alle extrahieren")
echo  und dann Setup.cmd im entpackten Ordner starten.
echo.
pause
exit /b 1
