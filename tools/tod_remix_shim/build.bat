@echo off
setlocal

cd /d "%~dp0"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo vswhere.exe not found.
  exit /b 1
)

for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
  set "VSINSTALL=%%I"
)

if not defined VSINSTALL (
  echo Visual C++ x86/x64 build tools were not found.
  exit /b 1
)

call "%VSINSTALL%\VC\Auxiliary\Build\vcvars32.bat"
if errorlevel 1 exit /b 1

if not exist "..\..\scripts" (
  echo scripts directory not found.
  exit /b 1
)

set "OUT=..\..\scripts\TODRemixShim.asi"
if not "%~1"=="" set "OUT=%~1"

cl /nologo /std:c++17 /W4 /O2 /GR- /MT /LD TODRemixShim.cpp /link /NOLOGO /OUT:"%OUT%"
if errorlevel 1 exit /b 1

echo Built %OUT%
