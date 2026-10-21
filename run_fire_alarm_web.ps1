# ==============================================================================
# Starter C Webserver og åpner Three.js 3D Brannalarmsystem i standard nettleser
# Inkluderer Win32 SCADA Konsoll UI for sending av meldinger og sanntidslogg (UTF-8)
# Backend er implementert i ren C (WinSock / POSIX sockets + SSE)
# ==============================================================================

[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
[Console]::InputEncoding = [System.Text.Encoding]::UTF8

Write-Host "Bygger og starter C Backend Webserver & Win32 SCADA Hub..." -ForegroundColor Cyan

gcc -Wall -Wextra -O2 -Isrc -o pubsub_dsl.exe src/*.c -lws2_32
if ($LASTEXITCODE -ne 0) {
    Write-Host "Kompilering feilet!" -ForegroundColor Red
    exit 1
}

Write-Host "Åpner http://localhost:3000 i nettleseren..." -ForegroundColor Green
Start-Process "http://localhost:3000"

Write-Host "Starter Win32 SCADA Kontrollpanel..." -ForegroundColor Green
.\pubsub_dsl.exe ui 3000
