@echo off
setlocal
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%i"
if not defined VSINSTALL exit /b 1
call "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
cd /d "%WXWIN%\build\msw"
nmake /f makefile.vc BUILD=release SHARED=0 TARGET_CPU=X64 COMPILER_VERSION= CXXFLAGS="/utf-8 /MP2"
exit /b %errorlevel%
