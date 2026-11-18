@echo off
title MetaBrain 3D Visualizer (UE 5.8.4)
set "SRC=%~dp0"
set "PREV=%~dp0Saved\LivePreview"
if not exist "%PREV%\Scripts\build_assets\fonts" mkdir "%PREV%\Scripts\build_assets\fonts"
copy /Y "%SRC%MetaBrainUE.uproject" "%PREV%\MetaBrainUE.uproject" >nul
xcopy /E /I /Y /Q "%SRC%Config" "%PREV%\Config" >nul
xcopy /E /I /Y /Q "%SRC%Content" "%PREV%\Content" >nul
xcopy /E /I /Y /Q "%SRC%Binaries" "%PREV%\Binaries" >nul
xcopy /E /I /Y /Q "%SRC%Scripts\build_assets\fonts" "%PREV%\Scripts\build_assets\fonts" >nul
start "" "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%PREV%\MetaBrainUE.uproject" /Game/MetaBrain/Maps/L_MetaBrain -game -ResX=1600 -ResY=900 -Windowed -WinX=60 -WinY=40 -NoSplash -notrace
