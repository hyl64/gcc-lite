@echo off
REM ============================================================================
REM  build_console.bat  -  build mi.exe (console control tool)
REM ============================================================================
setlocal
call "%~dp0find_vcvars.bat"
if errorlevel 1 exit /b 1

if not exist "%~dp0build" mkdir "%~dp0build"
pushd "%~dp0src"

cl /nologo /std:c++17 /EHsc /O2 /MT /W3 /permissive- /utf-8 ^
   /Fe:..\build\mi.exe ^
   /Fo:..\build\ ^
   test_bridge.cpp MiBridge.cpp ^
   user32.lib ole32.lib oleaut32.lib wbemuuid.lib advapi32.lib ^
   /link /SUBSYSTEM:CONSOLE /MANIFESTUAC:"level='requireAdministrator'"

if errorlevel 1 ( echo [X] build failed & popd & exit /b 1 )
popd
echo [OK] build\mi.exe
endlocal
