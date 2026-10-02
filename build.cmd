@echo off
setlocal
set "VSROOT=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
call "%VSROOT%\VC\Auxiliary\Build\vcvarsall.bat" arm64
if errorlevel 1 exit /b %errorlevel%
if not exist "%~dp0build" mkdir "%~dp0build"
if not exist "%~dp0dist" mkdir "%~dp0dist"
if not exist "%~dp0build\native-arm64" mkdir "%~dp0build\native-arm64"
if not exist "%~dp0build\native-x86" mkdir "%~dp0build\native-x86"
cl /nologo /EHsc /std:c++17 /MT /O2 /W4 /WX "%~dp0src\native_helper.cpp" /Fo:"%~dp0build\native-arm64\\" /Fe:"%~dp0dist\haltech-ftdi-arm64.exe"
if errorlevel 1 exit /b %errorlevel%
call "%VSROOT%\VC\Auxiliary\Build\vcvarsall.bat" x86
if errorlevel 1 exit /b %errorlevel%
cl /nologo /EHsc /std:c++17 /MT /O2 /W4 /WX /LD "%~dp0src\native_bridge.cpp" /Fo:"%~dp0build\native-x86\\" /link /DEF:"%~dp0src\ftd2xx.def" /OUT:"%~dp0dist\ftd2xx.dll" /IMPLIB:"%~dp0build\native-x86\ftd2xx.lib" /PDB:"%~dp0build\native-x86\ftd2xx.pdb"
exit /b %errorlevel%

