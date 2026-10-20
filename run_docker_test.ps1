Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host " Building and Starting Docker PubSub Communication Test..." -ForegroundColor Cyan
Write-Host "==========================================================" -ForegroundColor Cyan

$dockerComposeFile = "docker/docker-compose.yml"

# Clean up any lingering containers
docker compose -f $dockerComposeFile down -v --remove-orphans 2>$null

# Build and start services, abort when controller finishes
Write-Host "`n[1/3] Building images and running multi-container scenario..." -ForegroundColor Yellow
docker compose -f $dockerComposeFile up --build --abort-on-container-exit --exit-code-from controller
$exitCode = $LASTEXITCODE

Write-Host "`n[2/3] Inspecting Generated Log Files in ./logs/..." -ForegroundColor Yellow

if (Test-Path "logs/controller.log") {
    Write-Host "`n--- Controller Log Summary ---" -ForegroundColor Green
    Get-Content "logs/controller.log" -Tail 25
}

if (Test-Path "logs/node_LAMPE.log") {
    Write-Host "`n--- Node LAMPE Received Events ---" -ForegroundColor Green
    Get-Content "logs/node_LAMPE.log" -Tail 25
}

if (Test-Path "logs/node_VARME.log") {
    Write-Host "`n--- Node VARME Received Events ---" -ForegroundColor Green
    Get-Content "logs/node_VARME.log" -Tail 25
}

Write-Host "`n[3/3] Cleaning up Docker containers..." -ForegroundColor Yellow
docker compose -f $dockerComposeFile down -v 2>$null

if ($exitCode -eq 0) {
    Write-Host "`n==========================================================" -ForegroundColor Green
    Write-Host " DOCKER COMMUNICATION TEST COMPLETED SUCCESSFULLY! " -ForegroundColor Green
    Write-Host "==========================================================" -ForegroundColor Green
} else {
    Write-Host "`n==========================================================" -ForegroundColor Red
    Write-Host " DOCKER COMMUNICATION TEST FAILED (Exit code: $exitCode)" -ForegroundColor Red
    Write-Host "==========================================================" -ForegroundColor Red
}

exit $exitCode
