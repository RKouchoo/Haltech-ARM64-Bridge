@echo off
setlocal
rem Experimental native-helper tests only. Never build into or load dist here:
rem the release DLL is the hardware-verified VCP bridge, not this transport.
set "VSROOT=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
set "TESTDIR=%~dp0..\build\tests\native"
if not exist "%TESTDIR%" mkdir "%TESTDIR%"
call "%VSROOT%\VC\Auxiliary\Build\vcvarsall.bat" arm64
if errorlevel 1 exit /b %errorlevel%
cl /nologo /EHsc /std:c++17 /MT /O2 /W4 /WX /LD "%~dp0fake_ftd2xx.cpp" /Fo:"%TESTDIR%\\" /link /DEF:"%~dp0fake_ftd2xx.def" /OUT:"%TESTDIR%\fake-ftd2xx.dll" /IMPLIB:"%TESTDIR%\fake-ftd2xx.lib"
if errorlevel 1 exit /b %errorlevel%
cl /nologo /EHsc /std:c++17 /MT /O2 /W4 /WX /DBRIDGE_TEST_BACKEND "%~dp0..\src\native_helper.cpp" /Fo:"%TESTDIR%\\" /Fe:"%TESTDIR%\haltech-ftdi-arm64.exe"
if errorlevel 1 exit /b %errorlevel%
call "%VSROOT%\VC\Auxiliary\Build\vcvarsall.bat" x86
if errorlevel 1 exit /b %errorlevel%
cl /nologo /EHsc /std:c++17 /MT /O2 /W4 /WX /LD "%~dp0..\src\native_bridge.cpp" /Fo:"%TESTDIR%\\" /link /DEF:"%~dp0..\src\native_bridge.def" /OUT:"%TESTDIR%\ftd2xx.dll" /IMPLIB:"%TESTDIR%\ftd2xx.lib"
if errorlevel 1 exit /b %errorlevel%
cl /nologo /EHsc /std:c++17 /MT /O2 /W4 /WX "%~dp0native_integration.cpp" /Fo:"%TESTDIR%\\" /Fe:"%TESTDIR%\native_integration.exe"
if errorlevel 1 exit /b %errorlevel%
"%TESTDIR%\native_integration.exe" "%TESTDIR%\ftd2xx.dll"
if errorlevel 1 exit /b %errorlevel%
if not exist "%TESTDIR%\missing-helper" mkdir "%TESTDIR%\missing-helper"
if not exist "%TESTDIR%\missing-backend" mkdir "%TESTDIR%\missing-backend"
copy /y "%TESTDIR%\ftd2xx.dll" "%TESTDIR%\missing-helper\ftd2xx.dll" >nul
copy /y "%TESTDIR%\ftd2xx.dll" "%TESTDIR%\missing-backend\ftd2xx.dll" >nul
copy /y "%TESTDIR%\haltech-ftdi-arm64.exe" "%TESTDIR%\missing-backend\haltech-ftdi-arm64.exe" >nul
"%TESTDIR%\native_integration.exe" "%TESTDIR%\missing-helper\ftd2xx.dll" --expect-startup-failure
if errorlevel 1 exit /b %errorlevel%
"%TESTDIR%\native_integration.exe" "%TESTDIR%\missing-backend\ftd2xx.dll" --expect-startup-failure
if errorlevel 1 exit /b %errorlevel%
exit /b %errorlevel%
