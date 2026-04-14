@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
for %%I in ("%SCRIPT_DIR%..\..") do set "NEAR_ROOT=%%~fI"

set "APP_ROOT="
if exist "%NEAR_ROOT%\install\bin\extended_gaussianViewer_app_rwdi.exe" set "APP_ROOT=%NEAR_ROOT%\install"
if not defined APP_ROOT if exist "%NEAR_ROOT%\install\bin\extended_gaussianViewer_app.exe" set "APP_ROOT=%NEAR_ROOT%\install"
if not defined APP_ROOT if exist "%NEAR_ROOT%\install\bin\extended_gaussianViewer_app_msr.exe" set "APP_ROOT=%NEAR_ROOT%\install"
if not defined APP_ROOT if exist "%NEAR_ROOT%\install\bin\extended_gaussianViewer_app_d.exe" set "APP_ROOT=%NEAR_ROOT%\install"
if not defined APP_ROOT if exist "%NEAR_ROOT%\bin\extended_gaussianViewer_app_rwdi.exe" set "APP_ROOT=%NEAR_ROOT%"
if not defined APP_ROOT if exist "%NEAR_ROOT%\bin\extended_gaussianViewer_app.exe" set "APP_ROOT=%NEAR_ROOT%"
if not defined APP_ROOT if exist "%NEAR_ROOT%\bin\extended_gaussianViewer_app_msr.exe" set "APP_ROOT=%NEAR_ROOT%"
if not defined APP_ROOT if exist "%NEAR_ROOT%\bin\extended_gaussianViewer_app_d.exe" set "APP_ROOT=%NEAR_ROOT%"

if not defined APP_ROOT (
  echo Failed to locate an install root near "%SCRIPT_DIR%".
  echo Expected either:
  echo   repo-root\install\bin\extended_gaussianViewer_app_*.exe
  echo or
  echo   install\bin\extended_gaussianViewer_app_*.exe
  exit /b 1
)

set "BIN_DIR=%APP_ROOT%\bin"

set "EXE=%BIN_DIR%\extended_gaussianViewer_app_rwdi.exe"
if not exist "%EXE%" set "EXE=%BIN_DIR%\extended_gaussianViewer_app.exe"
if not exist "%EXE%" set "EXE=%BIN_DIR%\extended_gaussianViewer_app_msr.exe"
if not exist "%EXE%" set "EXE=%BIN_DIR%\extended_gaussianViewer_app_d.exe"

if not exist "%EXE%" (
  echo Failed to find installed viewer executable under "%BIN_DIR%".
  echo Build and install the project first.
  exit /b 1
)

set "CONTENT_ROOT=%NEAR_ROOT%"
if not exist "%CONTENT_ROOT%\manifests" (
  for %%I in ("%APP_ROOT%\..") do set "CONTENT_ROOT=%%~fI"
)

set "DEFAULT_MANIFEST=%CONTENT_ROOT%\manifests\mc_small_aerial_c36_neighbors_3x3.json"
set "DEFAULT_DATA_ROOT=%CONTENT_ROOT%\swaptest\mc_small_aerial_c36"
set "EXTRA_ARGS="

if "%~1"=="" (
  if exist "%DEFAULT_MANIFEST%" if exist "%DEFAULT_DATA_ROOT%" (
    set "EXTRA_ARGS=--manifest \"%DEFAULT_MANIFEST%\""
  )
)

"%EXE%" --appPath "%APP_ROOT%" %EXTRA_ARGS% %*
set "EXIT_CODE=%ERRORLEVEL%"

exit /b %EXIT_CODE%
