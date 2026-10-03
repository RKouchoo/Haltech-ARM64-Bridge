@echo off
setlocal
set "VSROOT=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
call "%VSROOT%\VC\Auxiliary\Build\vcvarsall.bat" x86
if errorlevel 1 exit /b %errorlevel%
if not exist "%~dp0build" mkdir "%~dp0build"
if not exist "%~dp0dist" mkdir "%~dp0dist"
if not exist "%~dp0build\vcp-x86" mkdir "%~dp0build\vcp-x86"
rem Ship the VCP transport verified with the Elite 2500. The native-helper
rem experiment remains in source and tests/native.cmd, outside the release path.
cl /nologo /EHsc /MT /O2 /W4 /WX /LD "%~dp0src\ftd2xx.cpp" advapi32.lib /Fo:"%~dp0build\vcp-x86\\" /link /DEF:"%~dp0src\ftd2xx.def" /OUT:"%~dp0dist\ftd2xx.dll" /IMPLIB:"%~dp0build\vcp-x86\ftd2xx.lib" /PDB:"%~dp0build\vcp-x86\ftd2xx.pdb"
exit /b %errorlevel%

