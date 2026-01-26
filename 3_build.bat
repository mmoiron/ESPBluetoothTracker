@echo off
set IDF_PATH=C:\Espressif\frameworks\esp-idf-v5.5.2
set IDF_TOOLS_PATH=C:\Espressif
set IDF_PYTHON_ENV_PATH=C:\Espressif\python_env\idf5.5_py3.11_env
set PATH=%IDF_PYTHON_ENV_PATH%\Scripts;C:\Espressif\tools\idf-git\2.44.0\cmd;C:\Espressif\tools\idf-python\3.11.2;%PATH%
set PYTHONPATH=
set PYTHONHOME=
set PYTHONNOUSERSITE=True

call "%IDF_PATH%\export.bat" >nul 2>&1
cd /d "C:\Users\mmoiron\Documents\MyCodingProjects\ESPBluetoothTracker"
idf.py build
