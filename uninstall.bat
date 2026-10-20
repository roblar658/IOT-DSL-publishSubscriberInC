@echo off
setlocal enabledelayedexpansion

echo ============================================================
echo   PubSub DSL Windows Uninstaller
echo ============================================================
echo.

set "INSTALL_DIR=%LOCALAPPDATA%\PubSubDSL"

echo Removing %INSTALL_DIR% from User PATH...
powershell -NoProfile -ExecutionPolicy Bypass -Command "$current = [Environment]::GetEnvironmentVariable('Path', 'User'); $dir = [Regex]::Escape('%INSTALL_DIR%'); $new = ($current -split ';' | Where-Object { $_ -ne '%INSTALL_DIR%' -and $_ -ne '' }) -join ';'; [Environment]::SetEnvironmentVariable('Path', $new, 'User')" 2>nul

echo Removing installed files...
if exist "%INSTALL_DIR%" (
    rmdir /S /Q "%INSTALL_DIR%"
    echo Directory %INSTALL_DIR% removed.
) else (
    echo Install directory was not found.
)

echo.
echo ============================================================
echo   PubSub DSL has been completely uninstalled.
echo ============================================================
pause
