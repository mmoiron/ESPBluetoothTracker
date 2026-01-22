@echo off
echo ============================================
echo   ESP32 Bluetooth Presence Detector - Flash
echo ============================================
echo.

:: Set minimal paths
set IDF_PATH=C:\Espressif\frameworks\esp-idf-v5.5.2
set IDF_TOOLS_PATH=C:\Espressif
set IDF_PYTHON_ENV_PATH=C:\Espressif\python_env\idf5.5_py3.11_env

:: Minimal PATH - only what we need
set PATH=%IDF_PYTHON_ENV_PATH%\Scripts;C:\Espressif\tools\idf-git\2.44.0\cmd;C:\Espressif\tools\cmake\3.30.2\bin;C:\Espressif\tools\ninja\1.12.1;C:\Espressif\tools\xtensa-esp-elf\esp-14.2.0_20241119\xtensa-esp-elf\bin;C:\WINDOWS\system32;C:\WINDOWS

:: Clear Python variables
set PYTHONPATH=
set PYTHONHOME=
set PYTHONNOUSERSITE=True

cd /d "C:\Users\mmoiron\Documents\MyCodingProjects\ESPBluetoothTracker"

echo Flashing to ESP32 on COM3 and opening monitor...
echo (Press Ctrl+] to exit monitor)
echo.

"%IDF_PYTHON_ENV_PATH%\Scripts\python.exe" "%IDF_PATH%\tools\idf.py" -p COM3 flash monitor

pause
