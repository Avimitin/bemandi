@echo off
setlocal
title bemandi resource measurement
echo.
echo Run this on the IIDX cabinet while the game is already running.
echo Select the current receiver state. This only labels the measurement;
echo it does not enable or disable streaming for you.
echo.
echo 1. NDI receiver connected - streaming ON
echo 2. NDI receiver disconnected - streaming OFF
echo.
choice /C 12 /N /M "Choose 1 or 2: "
if errorlevel 2 (
    set "BEMANDI_MEASURE_LABEL=streaming-off"
) else (
    set "BEMANDI_MEASURE_LABEL=streaming-on"
)
echo.
echo Return to the game when prompted. Recording lasts about two minutes.
echo Results get unique names, so both runs will be kept.
echo.
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0bemandi-measure.ps1" -Seconds 120 -StartDelaySeconds 15 -Label "%BEMANDI_MEASURE_LABEL%"
if errorlevel 1 (
    echo.
    echo Measurement failed. Keep the error above for troubleshooting.
) else (
    echo.
    echo Measurement finished. The results are beside this batch file.
)
echo.
pause
endlocal
