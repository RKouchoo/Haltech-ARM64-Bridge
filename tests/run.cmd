@echo off
setlocal
set "VSROOT=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
call "%VSROOT%\VC\Auxiliary\Build\vcvarsall.bat" x86
if errorlevel 1 exit /b %errorlevel%
if not exist "%~dp0..\build\tests" mkdir "%~dp0..\build\tests"
cl /nologo /EHsc /std:c++17 /O2 /W4 /WX "%~dp0transport_tests.cpp" advapi32.lib /Fo:"%~dp0..\build\tests\\" /Fe:"%~dp0..\build\tests\transport_tests.exe" /link /IMPLIB:"%~dp0..\build\tests\transport_tests.lib"
if errorlevel 1 exit /b %errorlevel%
"%~dp0..\build\tests\transport_tests.exe"
if errorlevel 1 exit /b %errorlevel%
cl /nologo /EHsc /std:c++17 /O2 /W4 /WX "%~dp0exports_test.cpp" /Fo:"%~dp0..\build\tests\\" /Fe:"%~dp0..\build\tests\exports_test.exe"
if errorlevel 1 exit /b %errorlevel%
rem Build with build.cmd first: test the actual distributable as well as source.
"%~dp0..\build\tests\exports_test.exe" "%~dp0..\dist\ftd2xx.dll"
if errorlevel 1 exit /b %errorlevel%
call "%~dp0native.cmd"
exit /b %errorlevel%
