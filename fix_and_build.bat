@echo off
echo ============================================
echo   Fixing Python Environment and Building
echo ============================================
echo.

:: Set paths
set IDF_PATH=C:\Espressif\frameworks\esp-idf-v5.5.2
set IDF_TOOLS_PATH=C:\Espressif
set IDF_PYTHON_ENV_PATH=C:\Espressif\python_env\idf5.5_py3.11_env

:: Use the virtual environment Python
set PATH=%IDF_PYTHON_ENV_PATH%\Scripts;C:\Espressif\tools\idf-git\2.44.0\cmd;C:\Espressif\tools\idf-python\3.11.2;%PATH%

:: Clear Python variables
set PYTHONPATH=
set PYTHONHOME=
set PYTHONNOUSERSITE=True

echo Installing missing Python packages...
"%IDF_PYTHON_ENV_PATH%\Scripts\python.exe" -m pip install click --quiet

echo.
echo Running ESP-IDF export...
call "C:\Espressif\frameworks\esp-idf-v5.5.2\export.bat"

echo.
echo Changing to project directory...
cd /d "C:\Users\mmoiron\Documents\MyCodingProjects\ESPBluetoothTracker"

echo.
echo Setting target to ESP32...
call "%IDF_PYTHON_ENV_PATH%\Scripts\python.exe" "%IDF_PATH%\tools\idf.py" set-target esp32

echo.
echo Building project...
call "%IDF_PYTHON_ENV_PATH%\Scripts\python.exe" "%IDF_PATH%\tools\idf.py" build

echo.
echo ============================================
echo   Build complete!
echo ============================================
pause
