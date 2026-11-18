@echo off
title MetaBrain 3D Visualizer (UE 5.8.4)
start "" "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%~dp0MetaBrainUE.uproject" /Game/MetaBrain/Maps/L_MetaBrain -game -ResX=1600 -ResY=900 -Windowed -WinX=60 -WinY=40 -NoSplash -notrace
