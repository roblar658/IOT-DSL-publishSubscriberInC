@echo off
setlocal enabledelayedexpansion

echo ============================================================
echo   PubSub DSL Windows Installer
echo ============================================================
echo.

set "INSTALL_DIR=%LOCALAPPDATA%\PubSubDSL"

echo [1/4] Checking build artifacts...
if not exist "pubsub_dsl.exe" (
    echo pubsub_dsl.exe not found. Compiling from source with gcc...
    where gcc >nul 2>nul
    if %errorlevel% neq 0 (
        echo ERROR: gcc not found in PATH. Please install MinGW-w64 or GCC.
        pause
        exit /b 1
    )
    gcc -Wall -Wextra -O2 -Isrc -o pubsub_dsl.exe src\logger.c src\net.c src\lexer.c src\ast.c src\parser.c src\runtime.c src\node_listener.c src\main.c -lws2_32
    if %errorlevel% neq 0 (
        echo ERROR: Compilation failed.
        pause
        exit /b 1
    )
    echo Compilation successful!
) else (
    echo Found pubsub_dsl.exe ready for installation.
)

echo.
echo [2/4] Installing files to: %INSTALL_DIR%
if not exist "%INSTALL_DIR%" mkdir "%INSTALL_DIR%"
if not exist "%INSTALL_DIR%\scripts" mkdir "%INSTALL_DIR%\scripts"

copy /Y "pubsub_dsl.exe" "%INSTALL_DIR%\" >nul
copy /Y "README.md" "%INSTALL_DIR%\" >nul
copy /Y "uninstall.bat" "%INSTALL_DIR%\" >nul
if exist "scripts" xcopy /E /I /Y "scripts" "%INSTALL_DIR%\scripts" >nul

echo.
echo [3/4] Adding %INSTALL_DIR% to User PATH...
powershell -NoProfile -ExecutionPolicy Bypass -Command "[Environment]::SetEnvironmentVariable('Path', [Environment]::GetEnvironmentVariable('Path', 'User') + ';%INSTALL_DIR%', 'User')" 2>nul
if %errorlevel% equ 0 (
    echo PATH updated successfully!
) else (
    echo Note: Could not update PATH automatically. You can manually add %INSTALL_DIR% to your PATH.
)

echo.
echo [4/4] Verifying installation...
"%INSTALL_DIR%\pubsub_dsl.exe" test >nul 2>nul
if %errorlevel% equ 0 (
    echo Self-test passed!
) else (
    echo Warning: Self-test returned an unexpected status code.
)

echo.
echo ============================================================
echo   Installation Completed Successfully!
echo ============================================================
echo Location : %INSTALL_DIR%\pubsub_dsl.exe
echo Usage    : Open a new terminal and type 'pubsub_dsl --help'
echo.
pause
