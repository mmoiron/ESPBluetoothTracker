@echo off
echo ============================================
echo   ESP32 Bluetooth Presence Detector - Build
echo ============================================
echo.

:: Set IDF_PATH
set IDF_PATH=C:\Espressif\frameworks\esp-idf-v5.5.2

:: Set IDF_TOOLS_PATH
set IDF_TOOLS_PATH=C:\Espressif

:: Add Git to PATH
set PATH=C:\Espressif\tools\idf-git\2.44.0\cmd;%PATH%

:: Add Python to PATH
set PATH=C:\Espressif\tools\idf-python\3.11.2;%PATH%

:: Clear Python variables that might interfere
set PYTHONPATH=
set PYTHONHOME=
set PYTHONNOUSERSITE=True

echo Using IDF_PATH: %IDF_PATH%
echo.

:: Call the ESP-IDF export script
cd /d %IDF_PATH%
call export.bat

:: Go to project directory
cd /d C:\Users\mmoiron\Documents\MyCodingProjects\ESPBluetoothTracker

echo.
echo Setting target to ESP32...
call idf.py set-target esp32

echo.
echo Building project...
call idf.py build

echo.
echo ============================================
echo   Build complete!
echo ============================================
echo.
echo To flash to ESP32 on COM3, run flash_project.bat
echo.
pause
