@echo off
cd /d "%~dp0"
title Zure Visualizer Input Bridge

echo ==================================================
echo  Zure Visualizer - Input Bridge Launcher
echo ==================================================
echo.

REM --- Find a usable Python command ---
set "PYCMD="
where py >nul 2>&1 && set "PYCMD=py"
if not defined PYCMD (
    where python >nul 2>&1 && set "PYCMD=python"
)
if not defined PYCMD (
    where python3 >nul 2>&1 && set "PYCMD=python3"
)

if not defined PYCMD (
    echo [ERROR] Python was not found in PATH.
    echo.
    echo Please install Python 3.8 or newer from:
    echo     https://www.python.org/downloads/
    echo.
    echo IMPORTANT: During install, check the box
    echo     "Add Python to PATH"
    echo.
    pause
    exit /b 1
)

echo Python command : %PYCMD%
%PYCMD% --version
echo Working dir    : %CD%
echo.

REM --- Check dependencies ---
%PYCMD% -c "import websockets, pynput" >nul 2>&1
if errorlevel 1 (
    echo Installing dependencies ^(websockets, pynput^) ...
    echo.
    %PYCMD% -m pip install --user websockets pynput
    if errorlevel 1 (
        echo.
        echo [ERROR] Failed to install dependencies.
        echo Try running manually:
        echo     %PYCMD% -m pip install --user websockets pynput
        echo.
        pause
        exit /b 1
    )
    echo.
)

REM --- Run the bridge ---
echo Starting bridge ...
echo.
%PYCMD% input_bridge.py
set "EXITCODE=%ERRORLEVEL%"

echo.
echo ==================================================
echo  Bridge stopped ^(exit code: %EXITCODE%^)
echo ==================================================
pause
