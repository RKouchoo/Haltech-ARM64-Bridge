@echo off
setlocal
set "VSROOT=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
call "%VSROOT%\VC\Auxiliary\Build\vcvarsall.bat" x86
if errorlevel 1 exit /b %errorlevel%
if not exist "%~dp0build" mkdir "%~dp0build"
if not exist "%~dp0dist" mkdir "%~dp0dist"
cl /nologo /EHsc /O2 /W4 /LD "%~dp0src\ftd2xx.cpp" advapi32.lib /Fo:"%~dp0build\\" /link /DEF:"%~dp0src\ftd2xx.def" /OUT:"%~dp0dist\ftd2xx.dll" /PDB:"%~dp0build\ftd2xx.pdb"
exit /b %errorlevel%

