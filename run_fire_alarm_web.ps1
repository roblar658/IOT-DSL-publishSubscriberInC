# ==============================================================================
# Starter C Webserver og åpner Three.js 3D Brannalarmsystem i standard nettleser
# Backend er implementert i ren C (WinSock / POSIX sockets + SSE)
# ==============================================================================

Write-Host "Bygger og starter C Backend Webserver for 3D Brannalarmsystem..." -ForegroundColor Cyan

if (-not (Test-Path ".\pubsub_dsl.exe")) {
    Write-Host "Kompilerer pubsub_dsl.exe med gcc..." -ForegroundColor Yellow
    gcc -Wall -Wextra -O2 -Isrc -o pubsub_dsl.exe src/*.c -lws2_32
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Kompilering feilet!" -ForegroundColor Red
        exit 1
    }
}

Write-Host "Starter .\pubsub_dsl.exe web 3000..." -ForegroundColor Green
$serverProcess = Start-Process .\pubsub_dsl.exe -ArgumentList "web 3000" -PassThru -NoNewWindow
Start-Sleep -Seconds 1

Write-Host "Åpner http://localhost:3000 i nettleseren..." -ForegroundColor Green
Start-Process "http://localhost:3000"

Write-Host "C Webserver kjører med prosess-ID $($serverProcess.Id). Trykk Ctrl+C i dette vinduet for å avslutte serveren." -ForegroundColor Yellow
Wait-Process -Id $serverProcess.Id
