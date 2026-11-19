# One-click launcher for MetaBrain C++20 Backend + 3D Glassmorphic Frontend
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path

Write-Host "Starting MetaBrain C++20 Autonomous Backend (:8085)..." -ForegroundColor Cyan
Start-Process -FilePath "$Root\build2\metabrain.exe" -ArgumentList "--serve", "8085" -WorkingDirectory $Root

Start-Sleep -Seconds 1

Write-Host "Starting MetaBrain 3D Frontend Dev Server..." -ForegroundColor Green
Set-Location "$Root\frontend"
npm run dev
