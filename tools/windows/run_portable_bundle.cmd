@echo off
setlocal

set "BUNDLE_ROOT=%~dp0"
if "%BUNDLE_ROOT:~-1%"=="\" set "BUNDLE_ROOT=%BUNDLE_ROOT:~0,-1%"

set "APP_ROOT=%BUNDLE_ROOT%\install"
set "BIN_DIR=%APP_ROOT%\bin"

set "SELECTED_EXE="
if exist "%BUNDLE_ROOT%\selected_viewer_exe.txt" (
  set /p SELECTED_EXE=<"%BUNDLE_ROOT%\selected_viewer_exe.txt"
)

if defined SELECTED_EXE (
  set "EXE=%BIN_DIR%\%SELECTED_EXE%"
) else (
  set "EXE=%BIN_DIR%\extended_gaussianViewer_app_rwdi.exe"
  if not exist "%EXE%" set "EXE=%BIN_DIR%\extended_gaussianViewer_app.exe"
  if not exist "%EXE%" set "EXE=%BIN_DIR%\extended_gaussianViewer_app_msr.exe"
  if not exist "%EXE%" set "EXE=%BIN_DIR%\extended_gaussianViewer_app_d.exe"
)

if not exist "%EXE%" (
  echo Failed to find viewer executable under "%BIN_DIR%".
  exit /b 1
)

set "DEFAULT_MANIFEST=%BUNDLE_ROOT%\manifests\mc_small_aerial_c36_neighbors_3x3.json"
set "DEFAULT_DATA_ROOT=%BUNDLE_ROOT%\swaptest\mc_small_aerial_c36"
set "EXTRA_ARGS="

if "%~1"=="" (
  if exist "%DEFAULT_MANIFEST%" if exist "%DEFAULT_DATA_ROOT%" (
    set "EXTRA_ARGS=--manifest \"%DEFAULT_MANIFEST%\""
  )
)

"%EXE%" --appPath "%APP_ROOT%" %EXTRA_ARGS% %*
set "EXIT_CODE=%ERRORLEVEL%"

exit /b %EXIT_CODE%
