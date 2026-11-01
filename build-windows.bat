@echo off
setlocal
cd /d "%~dp0"

rem Override these environment variables to use other dependency locations.
if not defined QT_DIR set "QT_DIR=%CD%\build\qt\6.8.3\msvc2022_64"
if not defined ICU_DIR set "ICU_DIR=%CD%\build\icu"
if not defined BUILD_DIR set "BUILD_DIR=build\windows-compatible"
if not defined NSIS_EXE set "NSIS_EXE=%ProgramFiles(x86)%\NSIS\makensis.exe"

if not defined VCToolsInstallDir (
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    if not exist "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" goto :no_vs
    for /f "usebackq delims=" %%I in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do call "%%I\VC\Auxiliary\Build\vcvars64.bat"
    if not defined VCToolsInstallDir goto :no_vs
)

cmake -S . -B "%BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_PREFIX_PATH=%QT_DIR%" "-DICU_ROOT=%ICU_DIR%"
if errorlevel 1 exit /b 1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%CD%\packaging\build-windows.ps1" -BuildDir "%BUILD_DIR%" -QtBin "%QT_DIR%\bin" -IcuBin "%ICU_DIR%\bin64" -Nsis "%NSIS_EXE%"
if errorlevel 1 exit /b 1
exit /b 0

:no_vs
echo Visual Studio C++ x64 build tools were not found.
exit /b 1
