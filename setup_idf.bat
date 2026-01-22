@echo off
echo ============================================
echo   ESP-IDF Python Environment Setup
echo ============================================
echo.

:: Set paths
set IDF_PATH=C:\Espressif\frameworks\esp-idf-v5.5.2
set IDF_TOOLS_PATH=C:\Espressif
set PATH=C:\Espressif\tools\idf-git\2.44.0\cmd;C:\Espressif\tools\idf-python\3.11.2;%PATH%

:: Clear Python variables
set PYTHONPATH=
set PYTHONHOME=
set PYTHONNOUSERSITE=True

echo Installing ESP-IDF Python environment...
echo This may take several minutes...
echo.

cd /d %IDF_PATH%
call install.bat

echo.
echo ============================================
echo   Setup complete! Now run build_project.bat
echo ============================================
pause
